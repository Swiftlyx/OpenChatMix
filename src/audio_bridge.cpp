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
#include <cmath>
#include <memory>
#include <vector>

#include "app.h"
#include "log.h"

using Microsoft::WRL::ComPtr;

namespace {

constexpr REFERENCE_TIME kCaptureBuffer = 200000;  // 20 ms in 100 ns units.
constexpr DWORD kStartTimeoutMs = 3000;
constexpr DWORD kStopTimeoutMs = 2000;
constexpr DWORD kWaitMs = 200;
constexpr UINT32 kDriftAllowanceMs = 30;  // Extra queue tolerated before old audio is dropped.

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

// Interleaved float frames waiting to be played.
class FrameQueue {
public:
    FrameQueue(size_t capacityFrames, int channels)
        : samples_(capacityFrames * channels), capacity_(capacityFrames), channels_(channels) {}

    size_t Size() const { return size_; }

    // Copies frames, mapping source channels onto the output layout; null data means silence.
    void Push(const float* data, size_t frames, int sourceChannels) {
        for (size_t frame = 0; frame < frames; ++frame) {
            if (size_ == capacity_) {
                Drop(1);
            }
            float* out = &samples_[((head_ + size_) % capacity_) * channels_];
            for (int channel = 0; channel < channels_; ++channel) {
                float value = 0.0f;
                if (data) {
                    const int source = channel < sourceChannels ? channel : (sourceChannels == 1 ? 0 : -1);
                    value = source >= 0 ? data[frame * sourceChannels + source] : 0.0f;
                }
                out[channel] = value;
            }
            ++size_;
        }
    }

    void Drop(size_t frames) {
        frames = std::min(frames, size_);
        head_ = (head_ + frames) % capacity_;
        size_ -= frames;
    }

    // Writes frames with a linear gain ramp so volume changes do not click.
    void Pop(float* out, size_t frames, float fromGain, float toGain) {
        for (size_t frame = 0; frame < frames; ++frame) {
            const float gain = fromGain + (toGain - fromGain) * (frame + 1) / static_cast<float>(frames);
            const float* in = &samples_[((head_ + frame) % capacity_) * channels_];
            for (int channel = 0; channel < channels_; ++channel) {
                out[frame * channels_ + channel] = in[channel] * gain;
            }
        }
        Drop(frames);
    }

private:
    std::vector<float> samples_;
    size_t capacity_;
    int channels_;
    size_t head_ = 0;
    size_t size_ = 0;
};

// Equalizer of one stream: a cascade of biquads per channel (transposed direct form II) and a soft
// limiter, since presets can boost by more than 10 dB.
class EqProcessor {
public:
    EqProcessor(UINT32 rate, int channels) : rate_(rate), channels_(channels) {}

    // Recomputes the coefficients when the parameters change; filter states carry over to avoid clicks.
    void Update(const std::shared_ptr<const ParametricEq>& eq) {
        if (eq == current_) {
            return;
        }
        current_ = eq;
        std::array<bool, kStages> wasActive{};
        for (const Stage& stage : active_) {
            wasActive[stage.band] = true;
        }
        active_.clear();
        if (!eq) {
            return;
        }
        if (state_.empty()) {
            state_.assign(static_cast<size_t>(kStages) * channels_ * 2, 0.0);
        }
        preamp_ = static_cast<float>(std::pow(10.0, eq->preamp / 20.0));
        smartVolume_ = eq->effects.smartVolume;
        for (int band = 0; band < kParametricBands; ++band) {
            const EqBand& settings = eq->bands[band];
            if (!settings.enabled || (UsesGain(settings.type) && std::fabs(settings.gain) < 0.01f)) {
                continue;
            }
            // A band that was bypassed starts from silence, not from its state of an earlier run.
            if (!wasActive[band]) {
                std::fill_n(state_.begin() + static_cast<size_t>(band) * channels_ * 2, channels_ * 2, 0.0);
            }
            active_.push_back({MakeBiquad(settings, rate_), band});
        }
        // Effects use fixed state slots after the bands; a toggled effect starts from silence.
        const auto effects = EffectBands(eq->effects);
        for (int index = 0; index < kEffectBands; ++index) {
            const int slot = kParametricBands + index;
            if (effects[index].gain < 0.01f) {
                continue;
            }
            if (!wasActive[slot]) {
                std::fill_n(state_.begin() + static_cast<size_t>(slot) * channels_ * 2, channels_ * 2, 0.0);
            }
            active_.push_back({MakeBiquad(effects[index], rate_), slot});
        }
    }

    void Process(float* samples, size_t frames) {
        if (!current_) {
            return;
        }
        for (size_t frame = 0; frame < frames; ++frame) {
            for (int channel = 0; channel < channels_; ++channel) {
                double x = samples[frame * channels_ + channel] * preamp_;
                for (const Stage& stage : active_) {
                    double* z = &state_[(static_cast<size_t>(stage.band) * channels_ + channel) * 2];
                    const Biquad& f = stage.filter;
                    const double y = f.b0 * x + z[0];
                    z[0] = f.b1 * x - f.a1 * y + z[1];
                    z[1] = f.b2 * x - f.a2 * y;
                    x = y;
                }
                samples[frame * channels_ + channel] = static_cast<float>(x);
            }
        }
        if (smartVolume_ != SmartVolume::Off) {
            Level(samples, frames);
        }
        for (size_t i = 0; i < frames * channels_; ++i) {
            samples[i] = SoftLimit(samples[i]);
        }
    }

private:
    struct Stage {
        Biquad filter;
        int band;  // State slot: bands first, then effects.
    };
    static constexpr int kStages = kParametricBands + kEffectBands;

    // Smart volume: follows the loudness over ~0.4 s and moves the gain toward the target level,
    // quickly down and slowly up. Near-silence holds the gain, so noise is not raised.
    void Level(float* samples, size_t frames) {
        constexpr float kTargets[kSmartVolumeCount] = {0.0f, -26.0f, -21.0f, -16.0f};
        constexpr float kMaxBoost = 9.0f;
        constexpr float kMaxCut = -12.0f;
        constexpr float kSilence = -55.0f;
        const size_t count = frames * channels_;
        if (count == 0) {
            return;
        }
        double energy = 0.0;
        for (size_t i = 0; i < count; ++i) {
            energy += static_cast<double>(samples[i]) * samples[i];
        }
        const float seconds = static_cast<float>(frames) / rate_;
        loudness_ += (static_cast<float>(energy / count) - loudness_) * (1.0f - std::exp(-seconds / 0.4f));
        const float levelDb = 10.0f * std::log10(loudness_ + 1e-12f);
        if (levelDb > kSilence) {
            const float wanted = std::clamp(kTargets[static_cast<int>(smartVolume_)] - levelDb, kMaxCut, kMaxBoost);
            const float time = wanted < gainDb_ ? 0.15f : 2.5f;
            gainDb_ += (wanted - gainDb_) * (1.0f - std::exp(-seconds / time));
        }
        // The gain ramps across the block to avoid steps.
        const float start = gain_;
        gain_ = std::pow(10.0f, gainDb_ / 20.0f);
        for (size_t frame = 0; frame < frames; ++frame) {
            const float gain = start + (gain_ - start) * (frame + 1) / frames;
            for (int channel = 0; channel < channels_; ++channel) {
                samples[frame * channels_ + channel] *= gain;
            }
        }
    }

    // Transparent below -1 dBFS, then approaches full scale smoothly instead of clipping.
    static float SoftLimit(float x) {
        constexpr float kKnee = 0.89f;
        const float magnitude = std::fabs(x);
        if (magnitude <= kKnee) {
            return x;
        }
        const float limited = kKnee + (1.0f - kKnee) * std::tanh((magnitude - kKnee) / (1.0f - kKnee));
        return x < 0.0f ? -limited : limited;
    }

    UINT32 rate_;
    int channels_;
    std::shared_ptr<const ParametricEq> current_;
    std::vector<Stage> active_;
    std::vector<double> state_;  // z1, z2 per stage slot and channel.
    float preamp_ = 1.0f;
    SmartVolume smartVolume_ = SmartVolume::Off;
    float loudness_ = 0.0f;  // Mean square.
    float gainDb_ = 0.0f;
    float gain_ = 1.0f;
};

// Called on audio service threads; only posts to the UI thread.
class VolumeNotifier final : public IAudioEndpointVolumeCallback {
public:
    explicit VolumeNotifier(HWND target) : target_(target) {}

    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG refs = InterlockedDecrement(&refs_);
        if (refs == 0) {
            delete this;
        }
        return refs;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IAudioEndpointVolumeCallback)) {
            *out = static_cast<IAudioEndpointVolumeCallback*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA) override {
        PostMessageW(target_, WM_APP_DEVICE_VOLUME, 0, 0);
        return S_OK;
    }

private:
    LONG refs_ = 1;
    HWND target_;
};

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

}  // namespace

AudioBridge::~AudioBridge() {
    Stop();
}

bool AudioBridge::Start(const std::wstring& sourceId, const std::wstring& targetId, HWND notifyWindow) {
    Stop();
    source_ = sourceId;
    target_ = targetId;
    notify_ = notifyWindow;
    error_ = BridgeError::None;
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
        WaitForSingleObject(thread_, kStopTimeoutMs);
        CloseHandle(thread_);
        thread_ = nullptr;
    }
    for (HANDLE* event : {&stopEvent_, &readyEvent_}) {
        if (*event) {
            CloseHandle(*event);
            *event = nullptr;
        }
    }
    running_ = false;
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
    UINT32 periodFrames = 0;
    bool initialized = false;
    if (sameRate) {
        ComPtr<IAudioClient3> render3;
        UINT32 defaultPeriod = 0;
        UINT32 fundamental = 0;
        UINT32 minPeriod = 0;
        UINT32 maxPeriod = 0;
        if (SUCCEEDED(render.As(&render3)) &&
            SUCCEEDED(render3->GetSharedModeEnginePeriod(renderFormat, &defaultPeriod, &fundamental, &minPeriod,
                                                         &maxPeriod)) &&
            SUCCEEDED(render3->InitializeSharedAudioStream(AUDCLNT_STREAMFLAGS_EVENTCALLBACK, minPeriod, renderFormat,
                                                          nullptr))) {
            initialized = true;
            periodFrames = minPeriod;
        } else {
            // A failed initialization can leave the client unusable.
            render.Reset();
            targetDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &render);
        }
    }
    if (!initialized && render) {
        // Different sample rates: Windows resamples the source format to the output.
        const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                            (sameRate ? 0 : AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY);
        initialized = SUCCEEDED(render->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 0, 0, renderFormat, nullptr));
    }
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
    const UINT32 targetQueue = std::min(bufferFrames, std::max<UINT32>(2 * periodFrames, rate / 200));
    const UINT32 maxQueue = targetQueue + rate * kDriftAllowanceMs / 1000;
    FrameQueue queue(rate, channels);
    EqProcessor equalizer(rate, channels);
    Log("bridge: %u Hz, %d ch, period %u frames, buffer %u frames", rate, channels, periodFrames, bufferFrames);

    if (FAILED(capture->Start()) || FAILED(render->Start())) {
        error_ = BridgeError::Unsupported;
        CloseHandle(renderEvent);
        return;
    }
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
        while (SUCCEEDED(hr = captureClient->GetNextPacketSize(&packet)) && packet > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(hr = captureClient->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) {
                break;
            }
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

        // The two devices run on separate clocks; drop old audio instead of drifting behind.
        if (queue.Size() + padding > maxQueue) {
            queue.Drop(queue.Size() + padding - targetQueue);
        }
        const UINT32 wanted = padding < targetQueue ? targetQueue - padding : 0;
        const UINT32 frames = std::min<UINT32>({bufferFrames - padding, wanted, static_cast<UINT32>(queue.Size())});
        if (frames > 0) {
            BYTE* out = nullptr;
            if (SUCCEEDED(renderClient->GetBuffer(frames, &out))) {
                const float target = std::clamp(volume_.load(), 0.0f, 1.0f);
                auto* samples = reinterpret_cast<float*>(out);
                queue.Pop(samples, frames, gain, target);
                gain = target;
                equalizer.Update(eq_.load());
                equalizer.Process(samples, frames);
                float peak = 0.0f;
                for (size_t i = 0; i < static_cast<size_t>(frames) * channels; ++i) {
                    peak = std::max(peak, std::fabs(samples[i]));
                }
                float previous = peak_.load();
                while (peak > previous && !peak_.compare_exchange_weak(previous, peak)) {
                }
                renderClient->ReleaseBuffer(frames, 0);
            }
        }
        latencyMs_ = static_cast<int>((queue.Size() + padding + frames) * 1000 / rate + capturePeriod / 10000);
    }

    capture->Stop();
    render->Stop();
    CloseHandle(renderEvent);
}

bool AudioRouter::Start(const std::wstring& gameId, const std::wstring& chatId, const std::wstring& outputId,
                        HWND notifyWindow) {
    const bool game = game_.Start(gameId, outputId, notifyWindow);
    const bool chat = chat_.Start(chatId, outputId, notifyWindow);
    if (!game || !chat) {
        Stop();
        return false;
    }
    WatchVolume(gameId, notifyWindow, gameVolume_);
    WatchVolume(chatId, notifyWindow, chatVolume_);
    UpdateDeviceVolumes();
    Log("routing device gains: game %.3f, chat %.3f", gameVolume_.gain, chatVolume_.gain);
    return true;
}

void AudioRouter::Stop() {
    for (DeviceVolume* volume : {&gameVolume_, &chatVolume_}) {
        if (volume->control && volume->callback) {
            volume->control->UnregisterControlChangeNotify(volume->callback.Get());
        }
        *volume = {};
    }
    game_.Stop();
    chat_.Stop();
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
    volume.callback.Attach(new VolumeNotifier(notifyWindow));
    if (FAILED(volume.control->RegisterControlChangeNotify(volume.callback.Get()))) {
        volume.callback.Reset();
    }
}

void AudioRouter::UpdateDeviceVolumes() {
    for (DeviceVolume* volume : {&gameVolume_, &chatVolume_}) {
        volume->gain = volume->control ? DeviceGain(volume->control.Get()) : 1.0f;
    }
    ApplyVolumes();
}

BridgeError AudioRouter::Error() const {
    return game_.Error() != BridgeError::None ? game_.Error() : chat_.Error();
}

void AudioRouter::SetLevels(Levels levels) {
    levels_ = levels;
    ApplyVolumes();
}

void AudioRouter::ApplyVolumes() {
    game_.SetVolume(levels_.game / 100.0f * gameVolume_.gain);
    chat_.SetVolume(levels_.chat / 100.0f * chatVolume_.gain * gameVolume_.gain);
}

void AudioRouter::SetEqualizers(std::shared_ptr<const ParametricEq> game, std::shared_ptr<const ParametricEq> chat) {
    game_.SetEqualizer(std::move(game));
    chat_.SetEqualizer(std::move(chat));
}

int AudioRouter::LatencyMs() const {
    return std::max(game_.LatencyMs(), chat_.LatencyMs());
}
