#include "mic_chain.h"

#include <audioclient.h>
#include <audiopolicy.h>
#include <avrt.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>

#include <pmmintrin.h>
#include <xmmintrin.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "app.h"
#include "audio_devices.h"
#include "drift_queue.h"
#include "log.h"
#include "plugin_host.h"

using Microsoft::WRL::ComPtr;

namespace {

constexpr REFERENCE_TIME kCaptureBuffer = 500000;  // 50 ms in 100 ns units: room if a wakeup comes late.
constexpr REFERENCE_TIME kRenderBuffer = 600000;   // 60 ms.
constexpr DWORD kStartTimeoutMs = 3000;
constexpr DWORD kStopTimeoutMs = 2000;
// The outputs are topped up at least this often, also when the microphone's packets come late.
constexpr DWORD kFeedMs = 5;
constexpr DWORD kPluginSwapTimeoutMs = 500;
constexpr ULONGLONG kUnderrunLogMs = 30000;  // Underruns are logged at most this often.
constexpr size_t kBlock = MicProcessor::kBlock;
// The cable (and the headphones while listening) are kept this far ahead of their reader: two blocks
// and a little margin, so they never run dry between capture packets. The clock difference between
// microphone and output is absorbed by resampling (DriftQueue); only after a stall is audio skipped.
constexpr UINT32 kTargetQueue = 2 * kBlock + MicProcessor::kRate * 6 / 1000;
constexpr double kLatencyTarget = kTargetQueue + kBlock / 2.0;
constexpr double kStallLimit = kLatencyTarget + MicProcessor::kRate * 80 / 1000.0;

// 48 kHz mono float; with AUTOCONVERTPCM Windows converts from and to the devices' own formats.
WAVEFORMATEXTENSIBLE StreamFormat() {
    WAVEFORMATEXTENSIBLE format{};
    format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    format.Format.nChannels = 1;
    format.Format.nSamplesPerSec = MicProcessor::kRate;
    format.Format.wBitsPerSample = 32;
    format.Format.nBlockAlign = 4;
    format.Format.nAvgBytesPerSec = MicProcessor::kRate * 4;
    format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    format.Samples.wValidBitsPerSample = 32;
    format.dwChannelMask = SPEAKER_FRONT_CENTER;
    format.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    return format;
}

// A render stream fed with processed blocks, kept kTargetQueue ahead of the device.
struct Sink {
    ComPtr<IAudioClient> client;
    ComPtr<IAudioRenderClient> render;
    UINT32 bufferFrames = 0;
    DriftQueue queue{1, MicProcessor::kRate, MicProcessor::kRate};
    std::vector<float> scratch;
    unsigned underruns = 0;  // Times the device ran dry while sound played.
    bool playing = false;

    bool Open(IMMDevice* device, const WAVEFORMATEX* format, DWORD flags) {
        return SUCCEEDED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client)) &&
               SUCCEEDED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, kRenderBuffer, 0, format, nullptr)) &&
               SUCCEEDED(client->GetBufferSize(&bufferFrames)) && SUCCEEDED(client->GetService(IID_PPV_ARGS(&render)));
    }

    // Writes what keeps the device kTargetQueue ahead; returns the frames queued in the device, or
    // fails when the stream is gone.
    HRESULT Feed(UINT32& padding, double elapsedSeconds) {
        const HRESULT hr = client->GetCurrentPadding(&padding);
        if (FAILED(hr)) {
            return hr;
        }
        const double latency = queue.Size() + padding;
        if (latency > kStallLimit) {
            queue.Drop(latency - kLatencyTarget);
        }
        queue.Steer(queue.Size() + padding, kLatencyTarget, elapsedSeconds);
        if (padding == 0 && playing) {
            ++underruns;
        }
        const UINT32 wanted = padding < kTargetQueue ? std::min(kTargetQueue - padding, bufferFrames - padding) : 0;
        UINT32 frames = 0;
        if (wanted > 0 && queue.Size() >= 4.0) {
            scratch.resize(wanted);
            frames = static_cast<UINT32>(queue.Pop(scratch.data(), wanted, 1.0f, 1.0f));
            BYTE* out = nullptr;
            if (frames > 0 && SUCCEEDED(render->GetBuffer(frames, &out))) {
                std::copy_n(scratch.data(), frames, reinterpret_cast<float*>(out));
                render->ReleaseBuffer(frames, 0);
                padding += frames;
            }
        }
        playing = padding > 0;
        return S_OK;
    }
};

}  // namespace

MicChain::~MicChain() {
    Stop();
}

bool MicChain::Start(const std::wstring& inputId, const std::wstring& outputId, const std::wstring& monitorId,
                     HWND notifyWindow) {
    Stop();
    input_ = inputId;
    output_ = outputId;
    monitor_ = monitorId == outputId ? L"" : monitorId;
    notify_ = notifyWindow;
    error_ = BridgeError::None;
    pluginsSeen_ = pluginsGeneration_.load();
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    readyEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = stopEvent_ && readyEvent_ ? CreateThread(nullptr, 0, ThreadProc, this, 0, nullptr) : nullptr;
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

void MicChain::Stop() {
    if (thread_) {
        SetEvent(stopEvent_);
        if (WaitForSingleObject(thread_, kStopTimeoutMs) != WAIT_OBJECT_0) {
            // The thread still waits on the events; leaking them is safer than closing them under it.
            Log("mic: thread did not stop");
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
    running_ = false;
    latencyMs_ = 0;
}

void MicChain::SetSettings(MicDspSettings settings) {
    settings_.store(std::make_shared<const MicDspSettings>(std::move(settings)));
}

void MicChain::SetPlugins(MicPluginList plugins) {
    plugins_.store(std::make_shared<const MicPluginList>(std::move(plugins)));
    const uint64_t generation = ++pluginsGeneration_;
    const ULONGLONG deadline = GetTickCount64() + kPluginSwapTimeoutMs;
    while (running_ && pluginsSeen_.load() < generation && GetTickCount64() < deadline) {
        Sleep(1);
    }
}

void MicChain::Fail(BridgeError error) {
    error_ = error;
    if (running_.exchange(false)) {
        PostMessageW(notify_, WM_APP_MIC_STOPPED, 0, 0);
    }
}

DWORD WINAPI MicChain::ThreadProc(void* self) {
    auto* chain = static_cast<MicChain*>(self);
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD taskIndex = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
    chain->Run();
    // Plugins may not be swapped out while the thread still holds them.
    chain->pluginsSeen_ = UINT64_MAX;
    if (mmcss) {
        AvRevertMmThreadCharacteristics(mmcss);
    }
    if (SUCCEEDED(apartment)) {
        CoUninitialize();
    }
    SetEvent(chain->readyEvent_);
    return 0;
}

void MicChain::Run() {
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> inputDevice;
    ComPtr<IMMDevice> outputDevice;
    const bool inputFound =
        SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) &&
        SUCCEEDED(input_.empty() ? enumerator->GetDefaultAudioEndpoint(eCapture, eCommunications, &inputDevice)
                                 : enumerator->GetDevice(input_.c_str(), &inputDevice));
    if (!inputFound || output_.empty() || FAILED(enumerator->GetDevice(output_.c_str(), &outputDevice))) {
        Log("mic: device missing");
        error_ = BridgeError::DeviceMissing;
        return;
    }

    const WAVEFORMATEXTENSIBLE format = StreamFormat();
    const auto* wave = reinterpret_cast<const WAVEFORMATEX*>(&format);
    constexpr DWORD kConvert = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    ComPtr<IAudioClient> capture;
    ComPtr<IAudioCaptureClient> captureClient;
    HANDLE captureEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!captureEvent || FAILED(inputDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &capture)) ||
        FAILED(capture->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK | kConvert, kCaptureBuffer,
                                   0, wave, nullptr)) ||
        FAILED(capture->SetEventHandle(captureEvent)) || FAILED(capture->GetService(IID_PPV_ARGS(&captureClient)))) {
        Log("mic: capture stream unavailable");
        error_ = BridgeError::Unsupported;
        if (captureEvent) {
            CloseHandle(captureEvent);
        }
        return;
    }

    Sink cable;
    if (!cable.Open(outputDevice.Get(), wave, kConvert)) {
        Log("mic: output stream unavailable");
        error_ = BridgeError::Unsupported;
        CloseHandle(captureEvent);
        return;
    }
    // Windows lowers other sounds while a call runs; the processed microphone must not be one of them.
    for (IAudioClient* client : {cable.client.Get()}) {
        ComPtr<IAudioSessionControl> session;
        ComPtr<IAudioSessionControl2> session2;
        if (SUCCEEDED(client->GetService(IID_PPV_ARGS(&session))) && SUCCEEDED(session.As(&session2))) {
            session2->SetDuckingPreference(TRUE);
        }
    }
    // Listening is optional: the chain runs without it if the device cannot be opened.
    Sink monitor;
    ComPtr<IMMDevice> monitorDevice;
    const bool monitoring = !monitor_.empty() && SUCCEEDED(enumerator->GetDevice(monitor_.c_str(), &monitorDevice)) &&
                            monitor.Open(monitorDevice.Get(), wave, kConvert);
    if (!monitor_.empty() && !monitoring) {
        Log("mic: monitor stream unavailable");
    }

    MicProcessor processor;
    std::vector<float> pending;  // Captured samples short of a whole block.
    float block[kBlock];
    REFERENCE_TIME capturePeriod = 0;
    capture->GetDevicePeriod(&capturePeriod, nullptr);
    REFERENCE_TIME renderLatency = 0;
    cable.client->GetStreamLatency(&renderLatency);
    Log("mic: running, output buffer %u frames%s", cable.bufferFrames, monitoring ? ", monitored" : "");

    if (FAILED(capture->Start()) || FAILED(cable.client->Start()) || (monitoring && FAILED(monitor.client->Start()))) {
        error_ = BridgeError::Unsupported;
        CloseHandle(captureEvent);
        return;
    }
    running_ = true;
    SetEvent(readyEvent_);
    ULONGLONG underrunLogged = GetTickCount64();
    unsigned captureGaps = 0;
    bool captured = false;  // A packet arrived since the stream started.
    LARGE_INTEGER frequency{};
    LARGE_INTEGER lastFeed{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&lastFeed);
    const auto millisecondsSince = [&](const LARGE_INTEGER& start) {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        return static_cast<double>(now.QuadPart - start.QuadPart) * 1000.0 / frequency.QuadPart;
    };
    // Reported with the gaps, to tell a slow plugin (slow blocks) from a thread that was not scheduled
    // in time (long passes without slow blocks).
    double slowestBlockMs = 0.0;
    double longestPassMs = 0.0;  // Time between two passes of the loop.
    LARGE_INTEGER passStart = lastFeed;

    const HANDLE waits[] = {stopEvent_, captureEvent};
    for (;;) {
        if (WaitForMultipleObjects(2, waits, FALSE, kFeedMs) == WAIT_OBJECT_0) {
            break;
        }
        longestPassMs = std::max(longestPassMs, millisecondsSince(passStart));
        QueryPerformanceCounter(&passStart);
        UINT32 packet = 0;
        HRESULT hr = S_OK;
        while (SUCCEEDED(hr = captureClient->GetNextPacketSize(&packet)) && packet > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(hr = captureClient->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) {
                break;
            }
            // Windows lost audio before we read it: the thread woke too late (CPU load, a slow plugin).
            // The first packet after the start always carries the flag and is not counted.
            if ((flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) && captured) {
                ++captureGaps;
            }
            captured = true;
            const auto* samples = reinterpret_cast<const float*>(data);
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                pending.insert(pending.end(), frames, 0.0f);
            } else {
                pending.insert(pending.end(), samples, samples + frames);
            }
            captureClient->ReleaseBuffer(frames);
        }

        // Whole blocks through the effects and plugins.
        const uint64_t generation = pluginsGeneration_.load();
        std::shared_ptr<const MicPluginList> plugins = plugins_.load();
        int pluginDelay = 0;
        for (const AudioPlugin* plugin : *plugins) {
            pluginDelay += plugin->LatencySamples();
        }
        const std::shared_ptr<const MicDspSettings> settings = settings_.load();
        size_t used = 0;
        float peak = 0.0f;
        while (pending.size() - used >= kBlock) {
            LARGE_INTEGER blockStart{};
            QueryPerformanceCounter(&blockStart);
            std::memcpy(block, pending.data() + used, sizeof(block));
            used += kBlock;
            for (const MicStage stage : settings->order) {
                if (stage != MicStage::Plugins) {
                    processor.Run(stage, block, *settings);
                    continue;
                }
                for (AudioPlugin* plugin : *plugins) {
                    plugin->Process(block, kBlock);
                }
            }
            MicProcessor::Limit(block, kBlock);
            slowestBlockMs = std::max(slowestBlockMs, millisecondsSince(blockStart));
            for (const float sample : block) {
                peak = std::max(peak, std::fabs(sample));
            }
            cable.queue.Push(block, kBlock, 1);
            if (monitoring) {
                monitor.queue.Push(block, kBlock, 1);
            }
        }
        pending.erase(pending.begin(), pending.begin() + used);
        plugins.reset();
        pluginsSeen_ = generation;
        float previous = peak_.load();
        while (peak > previous && !peak_.compare_exchange_weak(previous, peak)) {
        }

        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const double elapsed = static_cast<double>(now.QuadPart - lastFeed.QuadPart) / frequency.QuadPart;
        lastFeed = now;
        UINT32 padding = 0;
        if (SUCCEEDED(hr)) {
            hr = cable.Feed(padding, elapsed);
        }
        if (FAILED(hr)) {
            Log("mic: stream stopped, hr 0x%08lx", static_cast<unsigned long>(hr));
            Fail(BridgeError::Stopped);
            break;
        }
        UINT32 monitorPadding = 0;
        if (monitoring) {
            // A lost headphone stream must not stop the microphone.
            monitor.Feed(monitorPadding, elapsed);
        }
        latencyMs_ = static_cast<int>((padding + cable.queue.Size() + kBlock + pluginDelay) * 1000 / MicProcessor::kRate +
                                      (capturePeriod + renderLatency) / 10000);
        if ((cable.underruns > 0 || monitor.underruns > 0 || captureGaps > 0) &&
            GetTickCount64() - underrunLogged > kUnderrunLogMs) {
            Log("mic: cable ran dry %u times, headphones %u times, microphone gaps %u; speed %.4f; slowest block "
                "%.1f ms, longest pass %.1f ms",
                cable.underruns, monitor.underruns, captureGaps, cable.queue.Speed(), slowestBlockMs, longestPassMs);
            cable.underruns = monitor.underruns = captureGaps = 0;
            slowestBlockMs = longestPassMs = 0.0;
            underrunLogged = GetTickCount64();
        }
    }

    capture->Stop();
    cable.client->Stop();
    if (monitoring) {
        monitor.client->Stop();
    }
    CloseHandle(captureEvent);
}

LevelKeeper::~LevelKeeper() {
    Stop();
}

void LevelKeeper::Start(const std::vector<std::wstring>& endpointIds, HWND window, UINT message) {
    Stop();
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) {
        return;
    }
    for (const std::wstring& id : endpointIds) {
        ComPtr<IMMDevice> device;
        Watched watched;
        if (id.empty() || FAILED(enumerator->GetDevice(id.c_str(), &device)) ||
            FAILED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, &watched.control))) {
            continue;
        }
        watched.callback.Attach(CreateVolumeNotifier(window, message));
        if (FAILED(watched.control->RegisterControlChangeNotify(watched.callback.Get()))) {
            watched.callback.Reset();
        }
        watched_.push_back(std::move(watched));
    }
    Enforce();
}

void LevelKeeper::Stop() {
    for (Watched& watched : watched_) {
        if (watched.callback) {
            watched.control->UnregisterControlChangeNotify(watched.callback.Get());
        }
    }
    watched_.clear();
}

void LevelKeeper::Enforce() {
    for (Watched& watched : watched_) {
        float level = 1.0f;
        if (SUCCEEDED(watched.control->GetMasterVolumeLevelScalar(&level)) && level < 0.999f) {
            watched.control->SetMasterVolumeLevelScalar(1.0f, nullptr);
            Log("mic: level raised from %d%% to 100%%", static_cast<int>(std::lround(level * 100.0f)));
        }
    }
}
