#include "audio_tap.h"

#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <mmdeviceapi.h>
#include <ksmedia.h>
#include <wrl/client.h>

#include <algorithm>

#include "log.h"

using Microsoft::WRL::ComPtr;

namespace {

constexpr REFERENCE_TIME kBufferDuration = 1000000;  // 100 ms, in 100 ns units.
constexpr DWORD kPollMs = 10;                        // Loopback delivers whole 10 ms engine periods.
constexpr ULONGLONG kStaleMs = 120;                  // Without packets for this long nothing plays.
constexpr DWORD kActivateWaitMs = 2000;              // Activation usually completes within milliseconds.
constexpr WORD kProcessChannels = 2;
constexpr DWORD kProcessRate = 48000;

bool IsFloat(const WAVEFORMATEX* format) {
    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        return format->wBitsPerSample == 32;
    }
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        return extensible->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT && format->wBitsPerSample == 32;
    }
    return false;
}

// Activation may complete on any thread, so the handler is agile; it only signals that activation completed.
class ActivationDone final : public IActivateAudioInterfaceCompletionHandler {
public:
    ActivationDone() : event_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}

    HANDLE Event() const { return event_; }

    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG refs = InterlockedDecrement(&refs_);
        if (refs == 0) {
            delete this;
        }
        return refs;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        // IAgileObject only marks the object; it adds no methods to IUnknown.
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IAgileObject) ||
            iid == __uuidof(IActivateAudioInterfaceCompletionHandler)) {
            *out = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation*) override {
        SetEvent(event_);
        return S_OK;
    }

private:
    ~ActivationDone() {
        if (event_) {
            CloseHandle(event_);
        }
    }

    LONG refs_ = 1;
    HANDLE event_;
};

// Loopback of everything the device plays, in its mix format.
bool OpenDeviceLoopback(const std::wstring& endpointId, ComPtr<IAudioClient>& client, int& channels, int& rate) {
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    ComPtr<IAudioClient> audio;
    WAVEFORMATEX* format = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) ||
        FAILED(enumerator->GetDevice(endpointId.c_str(), &device)) ||
        FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &audio)) ||
        FAILED(audio->GetMixFormat(&format))) {
        Log("audio tap: device unavailable");
        return false;
    }
    const bool usable = IsFloat(format) && SUCCEEDED(audio->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                                                       AUDCLNT_STREAMFLAGS_LOOPBACK, kBufferDuration,
                                                                       0, format, nullptr));
    channels = format->nChannels;
    rate = static_cast<int>(format->nSamplesPerSec);
    CoTaskMemFree(format);
    if (!usable) {
        Log("audio tap: loopback capture unavailable");
        return false;
    }
    client = std::move(audio);
    return true;
}

// Loopback of what the process and its children play, wherever they play. Such a client has no mix format of its
// own; Windows converts to the one asked for.
HRESULT OpenProcessLoopback(DWORD processId, HANDLE stop, ComPtr<IAudioClient>& client) {
    AUDIOCLIENT_ACTIVATION_PARAMS params{};
    params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    params.ProcessLoopbackParams.TargetProcessId = processId;
    params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
    PROPVARIANT activation{};
    activation.vt = VT_BLOB;
    activation.blob.cbSize = sizeof(params);
    activation.blob.pBlobData = reinterpret_cast<BYTE*>(&params);
    ComPtr<ActivationDone> done;
    done.Attach(new ActivationDone());
    if (!done->Event()) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
    HRESULT hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient),
                                             &activation, done.Get(), &operation);
    if (FAILED(hr)) {
        return hr;
    }
    const HANDLE waits[] = {done->Event(), stop};
    if (WaitForMultipleObjects(2, waits, FALSE, kActivateWaitMs) != WAIT_OBJECT_0) {
        return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }
    HRESULT result = E_FAIL;
    ComPtr<IUnknown> unknown;
    if (FAILED(hr = operation->GetActivateResult(&result, &unknown))) {
        return hr;
    }
    ComPtr<IAudioClient> audio;
    if (FAILED(result) || FAILED(hr = unknown.As(&audio))) {
        return FAILED(result) ? result : hr;
    }
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    format.nChannels = kProcessChannels;
    format.nSamplesPerSec = kProcessRate;
    format.wBitsPerSample = 32;
    format.nBlockAlign = kProcessChannels * sizeof(float);
    format.nAvgBytesPerSec = kProcessRate * format.nBlockAlign;
    hr = audio->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
                           kBufferDuration, 0, &format, nullptr);
    if (SUCCEEDED(hr)) {
        client = std::move(audio);
    }
    return hr;
}

}  // namespace

AudioTap::~AudioTap() {
    Stop();
}

bool AudioTap::Start(const std::wstring& endpointId, std::vector<DWORD> processIds) {
    Stop();
    if (endpointId.empty() && processIds.empty()) {
        return false;
    }
    endpointId_ = endpointId;
    processIds_ = std::move(processIds);
    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = stop_ ? CreateThread(nullptr, 0, ThreadProc, this, 0, nullptr) : nullptr;
    if (!thread_) {
        Stop();
        return false;
    }
    return true;
}

void AudioTap::Stop() {
    if (thread_) {
        SetEvent(stop_);
        WaitForSingleObject(thread_, INFINITE);
        CloseHandle(thread_);
        thread_ = nullptr;
    }
    if (stop_) {
        CloseHandle(stop_);
        stop_ = nullptr;
    }
    AcquireSRWLockExclusive(&lock_);
    sources_.clear();
    rate_ = 0;
    ReleaseSRWLockExclusive(&lock_);
}

int AudioTap::Latest(float* out, size_t count) const {
    std::fill(out, out + count, 0.0f);
    const ULONGLONG now = GetTickCount64();
    AcquireSRWLockShared(&lock_);
    const int rate = rate_;
    for (const Source& source : sources_) {
        const size_t size = source.ring.size();
        if (!size || now - source.lastData > kStaleMs) {
            continue;
        }
        for (size_t i = 0; i < count; ++i) {
            out[i] += source.ring[(source.write + size - count % size + i) % size];
        }
    }
    ReleaseSRWLockShared(&lock_);
    return rate;
}

DWORD WINAPI AudioTap::ThreadProc(void* self) {
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    static_cast<AudioTap*>(self)->Run();
    if (SUCCEEDED(apartment)) {
        CoUninitialize();
    }
    return 0;
}

void AudioTap::Run() {
    std::vector<ComPtr<IAudioClient>> clients;
    int channels = kProcessChannels;
    int rate = static_cast<int>(kProcessRate);
    for (const DWORD processId : processIds_) {
        ComPtr<IAudioClient> client;
        if (const HRESULT hr = OpenProcessLoopback(processId, stop_, client); FAILED(hr)) {
            if (WaitForSingleObject(stop_, 0) == WAIT_OBJECT_0) {
                return;
            }
            Log("audio tap: process loopback unavailable, hr 0x%08lx; the device instead",
                static_cast<unsigned long>(hr));
            clients.clear();
            break;
        }
        clients.push_back(std::move(client));
    }
    if (clients.empty()) {
        ComPtr<IAudioClient> client;
        if (endpointId_.empty() || !OpenDeviceLoopback(endpointId_, client, channels, rate)) {
            return;
        }
        clients.push_back(std::move(client));
    }
    std::vector<ComPtr<IAudioCaptureClient>> captures(clients.size());
    for (size_t i = 0; i < clients.size(); ++i) {
        if (FAILED(clients[i]->GetService(IID_PPV_ARGS(&captures[i]))) || FAILED(clients[i]->Start())) {
            Log("audio tap: loopback capture unavailable");
            return;
        }
    }
    AcquireSRWLockExclusive(&lock_);
    sources_.assign(clients.size(), Source{std::vector<float>(static_cast<size_t>(rate), 0.0f)});
    rate_ = rate;
    ReleaseSRWLockExclusive(&lock_);
    while (WaitForSingleObject(stop_, kPollMs) == WAIT_TIMEOUT) {
        for (size_t i = 0; i < captures.size(); ++i) {
            UINT32 packet = 0;
            while (SUCCEEDED(captures[i]->GetNextPacketSize(&packet)) && packet > 0) {
                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;
                if (FAILED(captures[i]->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) {
                    break;
                }
                Append(i, reinterpret_cast<const float*>(data), frames, channels, flags & AUDCLNT_BUFFERFLAGS_SILENT);
                captures[i]->ReleaseBuffer(frames);
            }
        }
    }
    for (const ComPtr<IAudioClient>& client : clients) {
        client->Stop();
    }
}

void AudioTap::Append(size_t index, const float* frames, UINT32 count, int channels, bool silent) {
    AcquireSRWLockExclusive(&lock_);
    Source& source = sources_[index];
    const size_t size = source.ring.size();
    for (UINT32 frame = 0; frame < count && size; ++frame) {
        float sum = 0.0f;
        if (!silent) {
            for (int channel = 0; channel < channels; ++channel) {
                sum += frames[static_cast<size_t>(frame) * channels + channel];
            }
        }
        source.ring[source.write] = channels ? sum / static_cast<float>(channels) : 0.0f;
        source.write = (source.write + 1) % size;
    }
    source.lastData = GetTickCount64();
    ReleaseSRWLockExclusive(&lock_);
}
