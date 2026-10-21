#include "audio_bridge.h"

#include <audioclient.h>
#include <avrt.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <pmmintrin.h>
#include <xmmintrin.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <memory>
#include <vector>

#include "app.h"
#include "audio_devices.h"
#include "drift_queue.h"
#include "eq_processor.h"
#include "log.h"

using Microsoft::WRL::ComPtr;

namespace {

constexpr REFERENCE_TIME kCaptureBuffer = 500000;  // 50 ms in 100 ns units: room if a wakeup comes late.
constexpr REFERENCE_TIME kRenderBuffer = 400000;   // 40 ms: what the target needs, with room to spare.
constexpr DWORD kStartTimeoutMs = 3000;
constexpr DWORD kStopTimeoutMs = 2000;
constexpr DWORD kWaitMs = 200;
constexpr UINT32 kMarginMs = 6;             // Render headroom beyond one capture packet and one period.
constexpr UINT32 kStallAllowanceMs = 80;    // Beyond the target, after a stall, old audio is skipped.
constexpr ULONGLONG kUnderrunLogMs = 30000; // Underruns are logged at most this often.
// Render wakeups (one period each) without a capture packet after which the source counts as silent:
// loopback capture delivers nothing while no app plays on the device.
constexpr int kSilentWakeups = 6;
// Once the source fell silent, plugins get silence until their latency and tail have played out and their output
// stayed below the floor for a while, but no longer than the limit.
constexpr UINT32 kFlushLimitMs = 3000;
constexpr UINT32 kFlushQuietMs = 50;
constexpr float kFlushFloor = 1e-5f;  // -100 dBFS.

struct CoTaskMemDeleter {
    void operator()(void* pointer) const { CoTaskMemFree(pointer); }
};
using FormatPtr = std::unique_ptr<WAVEFORMATEX, CoTaskMemDeleter>;

bool IsFloat32(const WAVEFORMATEX* format) {
    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        return format->wBitsPerSample == 32;
    }
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        return extensible->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT && format->wBitsPerSample == 32;
    }
    return false;
}

// Gain a device applies at its Windows volume, in the device's own decibel scale. Levels above 0 dB
// (Virtual Audio Cable reaches +12 dB at 100%) are not reproduced, so no setting plays louder than the
// source. 0% is silence even where the device's range stops short of it.
float DeviceGain(IAudioEndpointVolume* volume) {
    BOOL muted = FALSE;
    float scalar = 1.0f;
    float decibels = 0.0f;
    if (FAILED(volume->GetMute(&muted)) || FAILED(volume->GetMasterVolumeLevelScalar(&scalar)) ||
        FAILED(volume->GetMasterVolumeLevel(&decibels))) {
        return 1.0f;
    }
    if (muted || scalar <= 0.0f) {
        return 0.0f;
    }
    return std::pow(10.0f, std::min(decibels, 0.0f) / 20.0f);
}

// Runs the plugins over interleaved frames in blocks of at most maxBlock.
void RunPlugins(const PluginList& plugins, float* samples, size_t frames, int channels, size_t maxBlock) {
    for (size_t done = 0; done < frames; done += maxBlock) {
        const size_t block = std::min(maxBlock, frames - done);
        for (AudioPlugin* plugin : plugins) {
            plugin->Process(samples + done * channels, block, channels);
        }
    }
}

}  // namespace

UINT32 AudioBridge::PluginBlock(UINT32 rate) {
    return std::bit_ceil(std::max(rate / 50, 1u));
}

void AudioBridge::SetPlugins(PluginList plugins, UINT32 rate) {
    // The old list leaves first when the rate changes, so no pass pairs it with the new rate.
    if (rate != pluginRate_.load()) {
        plugins_.Set({});
        pluginRate_ = rate;
    }
    plugins_.Set(std::move(plugins));
}

AudioBridge::~AudioBridge() {
    Stop();
}

bool AudioBridge::Start(const std::wstring& sourceId, const std::wstring& targetId, HWND notifyWindow) {
    Stop();
    source_ = sourceId;
    target_ = targetId;
    notify_ = notifyWindow;
    error_ = BridgeError::None;
    plugins_.Attach();
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    readyEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = CreateThread(nullptr, 0, ThreadProc, this, 0, nullptr);
    if (!thread_) {
        error_ = BridgeError::Unsupported;
        Stop();
        return false;
    }
    const HANDLE waits[] = {readyEvent_, thread_};
    WaitForMultipleObjects(2, waits, FALSE, kStartTimeoutMs);
    if (!running_) {
        Stop();
        return false;
    }
    return true;
}

void AudioBridge::Stop() {
    if (thread_) {
        SetEvent(stopEvent_);
        if (WaitForSingleObject(thread_, kStopTimeoutMs) != WAIT_OBJECT_0) {
            // The thread still waits on the events; leaking them is safer than closing them under it.
            Log("bridge: thread did not stop");
            stopEvent_ = nullptr;
            readyEvent_ = nullptr;
        } else {
            CloseHandle(thread_);
        }
        thread_ = nullptr;
    }
    for (HANDLE* event : {&stopEvent_, &readyEvent_}) {
        if (*event) {
            CloseHandle(*event);
            *event = nullptr;
        }
    }
    plugins_.Detach();
    running_ = false;
    rate_ = 0;
    latencyMs_ = 0;
}

void AudioBridge::Fail(BridgeError error) {
    error_ = error;
    if (running_.exchange(false)) {
        PostMessageW(notify_, WM_APP_ROUTING, 0, 0);
    }
}

DWORD WINAPI AudioBridge::ThreadProc(void* self) {
    auto* bridge = static_cast<AudioBridge*>(self);
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD taskIndex = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    // Decaying filter states would otherwise reach denormal numbers, which are very slow.
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
    bridge->Run();
    bridge->plugins_.Detach();
    if (mmcss) {
        AvRevertMmThreadCharacteristics(mmcss);
    }
    if (SUCCEEDED(apartment)) {
        CoUninitialize();
    }
    SetEvent(bridge->readyEvent_);
    return 0;
}

void AudioBridge::Run() {
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> sourceDevice;
    ComPtr<IMMDevice> targetDevice;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) ||
        FAILED(enumerator->GetDevice(source_.c_str(), &sourceDevice)) ||
        FAILED(enumerator->GetDevice(target_.c_str(), &targetDevice))) {
        Log("bridge: device missing");
        error_ = BridgeError::DeviceMissing;
        return;
    }

    // Loopback capture of the source playback device.
    ComPtr<IAudioClient> capture;
    ComPtr<IAudioCaptureClient> captureClient;
    WAVEFORMATEX* rawSourceFormat = nullptr;
    if (FAILED(sourceDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &capture)) ||
        FAILED(capture->GetMixFormat(&rawSourceFormat))) {
        error_ = BridgeError::Unsupported;
        return;
    }
    const FormatPtr sourceFormat(rawSourceFormat);
    if (!IsFloat32(sourceFormat.get()) ||
        FAILED(capture->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, kCaptureBuffer, 0,
                                   sourceFormat.get(), nullptr)) ||
        FAILED(capture->GetService(IID_PPV_ARGS(&captureClient)))) {
        Log("bridge: loopback capture unavailable");
        error_ = BridgeError::Unsupported;
        return;
    }

    // Event-driven render stream on the output, as small an engine period as the driver allows.
    ComPtr<IAudioClient> render;
    WAVEFORMATEX* rawTargetFormat = nullptr;
    if (FAILED(targetDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &render)) ||
        FAILED(render->GetMixFormat(&rawTargetFormat))) {
        error_ = BridgeError::Unsupported;
        return;
    }
    const FormatPtr targetFormat(rawTargetFormat);
    const bool sameRate = targetFormat->nSamplesPerSec == sourceFormat->nSamplesPerSec && IsFloat32(targetFormat.get());
    const WAVEFORMATEX* renderFormat = sameRate ? targetFormat.get() : sourceFormat.get();
    // The default engine period: loopback capture delivers whole engine periods (10 ms) anyway, so a
    // smaller render period only adds wakeups and risk of running dry.
    UINT32 periodFrames = 0;
    const DWORD streamFlags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                        (sameRate ? 0 : AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY);
    const bool initialized =
        render && SUCCEEDED(render->Initialize(AUDCLNT_SHAREMODE_SHARED, streamFlags, kRenderBuffer, 0, renderFormat, nullptr));
    HANDLE renderEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ComPtr<IAudioRenderClient> renderClient;
    UINT32 bufferFrames = 0;
    if (!initialized || !renderEvent || FAILED(render->SetEventHandle(renderEvent)) ||
        FAILED(render->GetBufferSize(&bufferFrames)) || FAILED(render->GetService(IID_PPV_ARGS(&renderClient)))) {
        Log("bridge: render stream unavailable");
        error_ = BridgeError::Unsupported;
        if (renderEvent) {
            CloseHandle(renderEvent);
        }
        return;
    }

    const UINT32 rate = renderFormat->nSamplesPerSec;
    const int channels = renderFormat->nChannels;
    const int sourceChannels = sourceFormat->nChannels;
    if (periodFrames == 0) {
        REFERENCE_TIME defaultPeriod = 0;
        render->GetDevicePeriod(&defaultPeriod, nullptr);
        periodFrames = static_cast<UINT32>(defaultPeriod * rate / 10000000);
    }
    REFERENCE_TIME capturePeriod = 0;
    capture->GetDevicePeriod(&capturePeriod, nullptr);
    // What the output's own stream holds back in the engine, besides the frames queued here.
    REFERENCE_TIME renderLatency = 0;
    render->GetStreamLatency(&renderLatency);
    // The output is kept one capture packet plus one period ahead, so it never runs dry between
    // packets; the queue absorbs the clock difference of the two devices by resampling (DriftQueue).
    const UINT32 capturePacket = static_cast<UINT32>(capturePeriod * rate / 10000000);
    const UINT32 renderTarget = std::min(bufferFrames, capturePacket + periodFrames + rate * kMarginMs / 1000);
    const double latencyTarget = renderTarget + capturePacket / 2.0;
    const double stallLimit = latencyTarget + rate * kStallAllowanceMs / 1000.0;
    DriftQueue queue(channels, rate, rate);
    EqProcessor equalizer(rate, channels);
    std::vector<float> scratch;
    const size_t pluginBlock = PluginBlock(rate);
    const UINT32 flushLimit = rate * kFlushLimitMs / 1000;
    const UINT32 flushQuietFrames = rate * kFlushQuietMs / 1000;
    Log("bridge: %u Hz, %d ch, period %u frames, buffer %u frames, target %u frames", rate, channels, periodFrames,
        bufferFrames, renderTarget);
    unsigned underruns = 0;
    unsigned captureGaps = 0;
    ULONGLONG underrunLogged = GetTickCount64();
    bool playing = false;
    int quietWakeups = 0;  // Wakeups since the last capture packet.
    bool holding = true;   // The source is silent or has just come back; nothing is played.
    bool flushing = false;  // Holding, while the plugins still play out what they hold.
    UINT32 flushed = 0;     // Frames of silence run through the plugins since the source fell silent,
    UINT32 flushQuiet = 0;  // and of those, frames since their output was last above the floor.
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    double slowestPluginsMs = 0.0;  // Longest pass through the plugins, reported with the underruns.

    if (FAILED(capture->Start()) || FAILED(render->Start())) {
        error_ = BridgeError::Unsupported;
        CloseHandle(renderEvent);
        return;
    }
    rate_ = rate;
    running_ = true;
    SetEvent(readyEvent_);

    float gain = volume_;
    const HANDLE waits[] = {stopEvent_, renderEvent};
    for (;;) {
        if (WaitForMultipleObjects(2, waits, FALSE, kWaitMs) == WAIT_OBJECT_0) {
            break;
        }

        UINT32 packet = 0;
        HRESULT hr = S_OK;
        bool received = false;
        while (SUCCEEDED(hr = captureClient->GetNextPacketSize(&packet)) && packet > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(hr = captureClient->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) {
                break;
            }
            // Windows lost audio before it was read: the thread woke too late. The first packet after
            // the start and after silence always carries the flag and is not counted.
            if ((flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) && !holding) {
                ++captureGaps;
            }
            received = true;
            const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
            queue.Push(silent ? nullptr : reinterpret_cast<const float*>(data), frames, sourceChannels);
            captureClient->ReleaseBuffer(frames);
        }

        UINT32 padding = 0;
        if (SUCCEEDED(hr)) {
            hr = render->GetCurrentPadding(&padding);
        }
        if (FAILED(hr)) {
            Log("bridge: stream stopped, hr 0x%08lx", static_cast<unsigned long>(hr));
            Fail(BridgeError::Stopped);
            break;
        }

        uint64_t generation = 0;
        std::shared_ptr<const PluginList> plugins = plugins_.Acquire(generation);
        const bool processing = !plugins->empty() && pluginRate_.load() == rate;
        int64_t pluginDelay = 0;
        int64_t pluginTail = 0;
        if (processing) {
            for (const AudioPlugin* plugin : *plugins) {
                pluginDelay += plugin->LatencySamples();
                pluginTail += plugin->TailSamples();
            }
        }

        // While the source is silent the queue holds: steering toward an empty queue would wind the
        // speed up to its limit, and the next sound would play off pitch for seconds. Playback resumes
        // once a render target is queued, so it starts at the target latency.
        quietWakeups = received ? 0 : quietWakeups + 1;
        if (quietWakeups > kSilentWakeups) {
            if (!holding) {
                queue.Hold();
                holding = true;
                flushing = processing;
                flushed = flushQuiet = 0;
            }
        } else if (holding && queue.Size() + padding >= renderTarget) {
            holding = false;
            flushing = false;
        }
        // The two devices run on separate clocks: the queue plays a hair faster or slower to hold
        // the latency, and only skips audio after a stall left far too much behind.
        if (!holding) {
            const double latency = queue.Size() + padding;
            if (latency > stallLimit) {
                queue.Drop(latency - latencyTarget);
            }
            queue.Steer(queue.Size() + padding, latencyTarget, static_cast<double>(periodFrames) / rate);
        }
        // An output that ran dry while its source still delivered is heard as a click; logged to tell
        // them apart. Running out at the end of a sound is not one.
        if (padding == 0 && playing && quietWakeups <= 1) {
            ++underruns;
        }
        const UINT32 wanted = padding < renderTarget ? renderTarget - padding : 0;
        UINT32 frames = 0;
        const bool fromQueue = !holding && wanted > 0 && queue.Size() >= 4.0;
        const bool flush = holding && flushing && processing && wanted > 0;
        if (fromQueue || flush) {
            const UINT32 room = std::min(wanted, bufferFrames - padding);
            scratch.resize(static_cast<size_t>(room) * channels);
            const float target = std::clamp(volume_.load(), 0.0f, 1.0f);
            if (flush) {
                std::fill(scratch.begin(), scratch.end(), 0.0f);
                frames = room;
            } else {
                // With plugins the volume comes after them: a compressor must not undo the ChatMix balance.
                frames = static_cast<UINT32>(
                    queue.Pop(scratch.data(), room, processing ? 1.0f : gain, processing ? 1.0f : target));
            }
            if (processing && frames > 0) {
                LARGE_INTEGER start{};
                LARGE_INTEGER end{};
                QueryPerformanceCounter(&start);
                RunPlugins(*plugins, scratch.data(), frames, channels, pluginBlock);
                QueryPerformanceCounter(&end);
                slowestPluginsMs = std::max(
                    slowestPluginsMs, static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0 / frequency.QuadPart);
                if (flush) {
                    float loudest = 0.0f;
                    for (size_t i = 0; i < static_cast<size_t>(frames) * channels; ++i) {
                        loudest = std::max(loudest, std::fabs(scratch[i]));
                    }
                    flushed += frames;
                    flushQuiet = loudest > kFlushFloor ? 0 : flushQuiet + frames;
                    const int64_t minimum = std::min<int64_t>(pluginDelay + pluginTail, flushLimit);
                    if ((flushed >= minimum && flushQuiet >= flushQuietFrames) || flushed >= flushLimit) {
                        flushing = false;
                    }
                }
                for (UINT32 frame = 0; frame < frames; ++frame) {
                    const float ramp = gain + (target - gain) * (frame + 1) / static_cast<float>(frames);
                    for (int channel = 0; channel < channels; ++channel) {
                        scratch[static_cast<size_t>(frame) * channels + channel] *= ramp;
                    }
                }
            }
            gain = target;
            BYTE* out = nullptr;
            if (frames > 0 && SUCCEEDED(renderClient->GetBuffer(frames, &out))) {
                auto* samples = reinterpret_cast<float*>(out);
                std::copy_n(scratch.data(), static_cast<size_t>(frames) * channels, samples);
                equalizer.Update(eq_.load());
                equalizer.Process(samples, frames);
                float peak = 0.0f;
                for (size_t i = 0; i < static_cast<size_t>(frames) * channels; ++i) {
                    peak = std::max(peak, std::fabs(samples[i]));
                }
                for (std::atomic<float>* slot : {&peak_, &meterPeak_}) {
                    float previous = slot->load();
                    while (peak > previous && !slot->compare_exchange_weak(previous, peak)) {
                    }
                }
                renderClient->ReleaseBuffer(frames, 0);
            }
        }
        playing = frames > 0 || padding > 0;
        plugins.reset();
        plugins_.Release(generation);
        if ((underruns > 0 || captureGaps > 0) && GetTickCount64() - underrunLogged > kUnderrunLogMs) {
            Log("bridge: output ran dry %u times, capture gaps %u; speed %.4f; slowest plugin pass %.1f ms", underruns,
                captureGaps, queue.Speed(), slowestPluginsMs);
            underruns = captureGaps = 0;
            slowestPluginsMs = 0.0;
            underrunLogged = GetTickCount64();
        }
        // While holding, the latency the next sound starts with.
        const double queued =
            (holding ? latencyTarget : queue.Size() + padding + frames) + static_cast<double>(pluginDelay);
        latencyMs_ = static_cast<int>(queued * 1000 / rate + (capturePeriod + renderLatency) / 10000);
    }

    capture->Stop();
    render->Stop();
    CloseHandle(renderEvent);
}

bool AudioRouter::Start(const RoutingPlan& plan, HWND notifyWindow) {
    mediaRouted_ = !plan.media.empty();
    chatWithGame_ = plan.chatOutput == plan.gameOutput;
    mediaWithGame_ = plan.mediaOutput == plan.gameOutput;
    const bool game = game_.Start(plan.game, plan.gameOutput, notifyWindow);
    const bool chat = chat_.Start(plan.chat, plan.chatOutput, notifyWindow);
    const bool media = !mediaRouted_ || media_.Start(plan.media, plan.mediaOutput, notifyWindow);
    if (!game || !chat || !media) {
        Stop();
        return false;
    }
    WatchVolume(plan.game, notifyWindow, gameVolume_);
    WatchVolume(plan.chat, notifyWindow, chatVolume_);
    if (mediaRouted_) {
        WatchVolume(plan.media, notifyWindow, mediaVolume_);
    }
    UpdateDeviceVolumes();
    Log("routing device gains: game %.3f, chat %.3f%s", gameVolume_.gain, chatVolume_.gain,
        mediaRouted_ ? ", media routed" : "");
    return true;
}

void AudioRouter::Stop() {
    for (DeviceVolume* volume : {&gameVolume_, &chatVolume_, &mediaVolume_}) {
        if (volume->control && volume->callback) {
            volume->control->UnregisterControlChangeNotify(volume->callback.Get());
        }
        *volume = {};
    }
    game_.Stop();
    chat_.Stop();
    media_.Stop();
}

void AudioRouter::WatchVolume(const std::wstring& id, HWND notifyWindow, DeviceVolume& volume) {
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) ||
        FAILED(enumerator->GetDevice(id.c_str(), &device)) ||
        FAILED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, &volume.control))) {
        // Without a volume control the device plays at full level, as before it was read.
        volume.control.Reset();
        Log("routing: no volume control for a device");
        return;
    }
    volume.callback.Attach(CreateVolumeNotifier(notifyWindow, WM_APP_DEVICE_VOLUME));
    if (FAILED(volume.control->RegisterControlChangeNotify(volume.callback.Get()))) {
        volume.callback.Reset();
    }
}

void AudioRouter::UpdateDeviceVolumes() {
    for (DeviceVolume* volume : {&gameVolume_, &chatVolume_, &mediaVolume_}) {
        volume->gain = volume->control ? DeviceGain(volume->control.Get()) : 1.0f;
    }
    ApplyVolumes();
}

BridgeError AudioRouter::Error() const {
    for (const AudioBridge* bridge : {&game_, &chat_, &media_}) {
        if (bridge->Error() != BridgeError::None) {
            return bridge->Error();
        }
    }
    return BridgeError::None;
}

void AudioRouter::SetLevels(Levels levels) {
    levels_ = levels;
    ApplyVolumes();
}

void AudioRouter::SetMediaLevel(float level) {
    mediaLevel_ = level;
    ApplyVolumes();
}

void AudioRouter::ApplyVolumes() {
    game_.SetVolume(levels_.game / 100.0f * gameVolume_.gain);
    chat_.SetVolume(levels_.chat / 100.0f * chatVolume_.gain * (chatWithGame_ ? gameVolume_.gain : 1.0f));
    media_.SetVolume(mediaLevel_ * mediaVolume_.gain * (mediaWithGame_ ? gameVolume_.gain : 1.0f));
}

AudioBridge* AudioRouter::BridgeOf(EqChannel channel) {
    switch (channel) {
    case EqChannel::Game: return &game_;
    case EqChannel::Chat: return &chat_;
    case EqChannel::Media: return &media_;
    case EqChannel::Mic: break;
    }
    return nullptr;
}

void AudioRouter::SetPlugins(EqChannel channel, PluginList plugins, UINT32 rate) {
    if (AudioBridge* bridge = BridgeOf(channel)) {
        bridge->SetPlugins(std::move(plugins), rate);
    }
}

UINT32 AudioRouter::Rate(EqChannel channel) const {
    switch (channel) {
    case EqChannel::Game: return game_.Rate();
    case EqChannel::Chat: return chat_.Rate();
    case EqChannel::Media: return mediaRouted_ ? media_.Rate() : 0;
    case EqChannel::Mic: break;
    }
    return 0;
}

void AudioRouter::SetEqualizers(std::shared_ptr<const ParametricEq> game, std::shared_ptr<const ParametricEq> chat,
                                std::shared_ptr<const ParametricEq> media) {
    game_.SetEqualizer(std::move(game));
    chat_.SetEqualizer(std::move(chat));
    media_.SetEqualizer(std::move(media));
}

int AudioRouter::LatencyMs() const {
    return std::max({game_.LatencyMs(), chat_.LatencyMs(), mediaRouted_ ? media_.LatencyMs() : 0});
}
