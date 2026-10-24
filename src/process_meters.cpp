#include "process_meters.h"

#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

#include "log.h"

using Microsoft::WRL::ComPtr;

namespace {

constexpr REFERENCE_TIME kBufferDuration = 2000000;  // 200 ms, in 100 ns units.
constexpr WORD kChannels = 2;
constexpr DWORD kSampleRate = 48000;
constexpr ULONGLONG kStaleMs = 60;     // Without packets for this long the app plays nothing.
constexpr ULONGLONG kUnreadMs = 2000;  // A capture nobody read for this long closes.
constexpr LONGLONG kPruneMs = 1000;

std::atomic<bool> g_failureLogged{false};

}  // namespace

struct ProcessMeters::Capture {
    enum class State { Opening, Open, Failed };
    class Completion;

    ~Capture() {
        if (client) {
            client->Stop();
        }
    }

    void Open(DWORD pid, const std::shared_ptr<Capture>& self);
    void Opened(IActivateAudioInterfaceAsyncOperation* operation);
    void Fail(HRESULT hr);

    std::atomic<State> state{State::Opening};
    // Set before state turns Open.
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> reader;
    // Under the owner's lock_.
    ULONGLONG lastRead = 0;
    ULONGLONG lastPacket = 0;
    float latest = 0.0f;
};

// Activation may complete on any thread, so the handler is agile.
class ProcessMeters::Capture::Completion final : public IActivateAudioInterfaceCompletionHandler {
public:
    explicit Completion(std::shared_ptr<Capture> capture) : capture_(std::move(capture)) {}

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
    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation) override {
        if (capture_) {
            capture_->Opened(operation);
            capture_.reset();
        }
        return S_OK;
    }

private:
    ~Completion() = default;

    LONG refs_ = 1;
    std::shared_ptr<Capture> capture_;
};

void ProcessMeters::Capture::Open(DWORD pid, const std::shared_ptr<Capture>& self) {
    AUDIOCLIENT_ACTIVATION_PARAMS params{};
    params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    params.ProcessLoopbackParams.TargetProcessId = pid;
    params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
    PROPVARIANT activation{};
    activation.vt = VT_BLOB;
    activation.blob.cbSize = sizeof(params);
    activation.blob.pBlobData = reinterpret_cast<BYTE*>(&params);
    ComPtr<Completion> completion;
    completion.Attach(new Completion(self));
    ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
    const HRESULT hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient),
                                                   &activation, completion.Get(), &operation);
    if (FAILED(hr)) {
        Fail(hr);
    }
}

void ProcessMeters::Capture::Opened(IActivateAudioInterfaceAsyncOperation* operation) {
    HRESULT result = E_FAIL;
    ComPtr<IUnknown> unknown;
    HRESULT hr = operation->GetActivateResult(&result, &unknown);
    if (SUCCEEDED(hr)) {
        hr = result;
    }
    ComPtr<IAudioClient> audio;
    if (SUCCEEDED(hr)) {
        hr = unknown.As(&audio);
    }
    if (SUCCEEDED(hr)) {
        // A process loopback client has no mix format of its own; Windows converts to the one asked for.
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        format.nChannels = kChannels;
        format.nSamplesPerSec = kSampleRate;
        format.wBitsPerSample = 32;
        format.nBlockAlign = kChannels * sizeof(float);
        format.nAvgBytesPerSec = kSampleRate * format.nBlockAlign;
        hr = audio->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
                               kBufferDuration, 0, &format, nullptr);
    }
    ComPtr<IAudioCaptureClient> capture;
    if (SUCCEEDED(hr)) {
        hr = audio->GetService(IID_PPV_ARGS(&capture));
    }
    if (SUCCEEDED(hr)) {
        hr = audio->Start();
    }
    if (FAILED(hr)) {
        Fail(hr);
        return;
    }
    client = std::move(audio);
    reader = std::move(capture);
    state.store(State::Open, std::memory_order_release);
}

void ProcessMeters::Capture::Fail(HRESULT hr) {
    if (!g_failureLogged.exchange(true)) {
        Log("process loopback unavailable, hr 0x%08lx; app meters use their sessions", static_cast<unsigned long>(hr));
    }
    state.store(State::Failed, std::memory_order_release);
}

ProcessMeters::~ProcessMeters() {
    Close();
}

std::optional<float> ProcessMeters::Peak(DWORD pid) {
    const ULONGLONG now = GetTickCount64();
    std::shared_ptr<Capture> opening;
    std::optional<float> peak = 0.0f;
    AcquireSRWLockExclusive(&lock_);
    std::shared_ptr<Capture>& slot = captures_[pid];
    if (!slot) {
        slot = std::make_shared<Capture>();
        opening = slot;
        Arm();
    }
    Capture& capture = *slot;
    capture.lastRead = now;
    switch (capture.state.load(std::memory_order_acquire)) {
    case Capture::State::Opening:
        break;
    case Capture::State::Failed:
        peak.reset();
        break;
    case Capture::State::Open: {
        // Every packet since the last read; a read with none new, e.g. a second one right after, keeps the last
        // peak until the app has played nothing for a while.
        float loudest = 0.0f;
        bool fresh = false;
        UINT32 packet = 0;
        HRESULT hr = S_OK;
        while (SUCCEEDED(hr = capture.reader->GetNextPacketSize(&packet)) && packet > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(hr = capture.reader->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) {
                break;
            }
            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                const float* samples = reinterpret_cast<const float*>(data);
                for (UINT32 i = 0; i < frames * kChannels; ++i) {
                    loudest = std::max(loudest, std::fabs(samples[i]));
                }
            }
            capture.reader->ReleaseBuffer(frames);
            fresh = true;
        }
        if (FAILED(hr)) {
            capture.Fail(hr);
            peak.reset();
            break;
        }
        if (fresh) {
            capture.latest = loudest;
            capture.lastPacket = now;
        } else if (now - capture.lastPacket > kStaleMs) {
            capture.latest = 0.0f;
        }
        peak = capture.latest;
        break;
    }
    }
    ReleaseSRWLockExclusive(&lock_);
    if (opening) {
        opening->Open(pid, opening);
    }
    return peak;
}

void ProcessMeters::Close() {
    std::map<DWORD, std::shared_ptr<Capture>> closing;
    AcquireSRWLockExclusive(&lock_);
    closing.swap(captures_);
    armed_ = false;
    PTP_TIMER timer = timer_;
    timer_ = nullptr;
    ReleaseSRWLockExclusive(&lock_);
    // With no captures left a running prune does not arm the timer again.
    if (timer) {
        SetThreadpoolTimer(timer, nullptr, 0, 0);
        WaitForThreadpoolTimerCallbacks(timer, TRUE);
        CloseThreadpoolTimer(timer);
    }
}

void ProcessMeters::Arm() {
    if (armed_) {
        return;
    }
    if (!timer_) {
        timer_ = CreateThreadpoolTimer(Prune, this, nullptr);
        if (!timer_) {
            return;
        }
    }
    // Negative: relative to now, in 100 ns units.
    ULARGE_INTEGER due;
    due.QuadPart = static_cast<ULONGLONG>(-kPruneMs * 10000);
    FILETIME time{due.LowPart, due.HighPart};
    SetThreadpoolTimer(timer_, &time, 0, 0);
    armed_ = true;
}

void CALLBACK ProcessMeters::Prune(PTP_CALLBACK_INSTANCE, void* context, PTP_TIMER) {
    auto* self = static_cast<ProcessMeters*>(context);
    std::vector<std::shared_ptr<Capture>> closing;  // Stopped outside the lock.
    const ULONGLONG now = GetTickCount64();
    AcquireSRWLockExclusive(&self->lock_);
    self->armed_ = false;
    for (auto it = self->captures_.begin(); it != self->captures_.end();) {
        if (now - it->second->lastRead > kUnreadMs) {
            closing.push_back(std::move(it->second));
            it = self->captures_.erase(it);
        } else {
            ++it;
        }
    }
    if (!self->captures_.empty()) {
        self->Arm();
    }
    ReleaseSRWLockExclusive(&self->lock_);
}
