#include "audio_tap.h"

#include <audioclient.h>
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

}  // namespace

AudioTap::~AudioTap() {
    Stop();
}

bool AudioTap::Start(const std::wstring& endpointId) {
    Stop();
    if (endpointId.empty()) {
        return false;
    }
    endpointId_ = endpointId;
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
    rate_ = 0;
    ReleaseSRWLockExclusive(&lock_);
}

int AudioTap::Latest(float* out, size_t count) const {
    AcquireSRWLockShared(&lock_);
    const int rate = rate_;
    const bool stale = GetTickCount64() - lastData_.load() > kStaleMs;
    if (rate == 0 || ring_.empty() || stale) {
        std::fill(out, out + count, 0.0f);
    } else {
        const size_t size = ring_.size();
        for (size_t i = 0; i < count; ++i) {
            out[i] = ring_[(write_ + size - count % size + i) % size];
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
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> capture;
    WAVEFORMATEX* format = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) ||
        FAILED(enumerator->GetDevice(endpointId_.c_str(), &device)) ||
        FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client)) ||
        FAILED(client->GetMixFormat(&format))) {
        Log("audio tap: device unavailable");
        return;
    }
    const bool usable = IsFloat(format) &&
                        SUCCEEDED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
                                                     kBufferDuration, 0, format, nullptr)) &&
                        SUCCEEDED(client->GetService(IID_PPV_ARGS(&capture))) && SUCCEEDED(client->Start());
    const int channels = format->nChannels;
    const int rate = static_cast<int>(format->nSamplesPerSec);
    CoTaskMemFree(format);
    if (!usable) {
        Log("audio tap: loopback capture unavailable");
        return;
    }
    AcquireSRWLockExclusive(&lock_);
    ring_.assign(static_cast<size_t>(rate), 0.0f);
    write_ = 0;
    rate_ = rate;
    ReleaseSRWLockExclusive(&lock_);
    while (WaitForSingleObject(stop_, kPollMs) == WAIT_TIMEOUT) {
        UINT32 packet = 0;
        while (SUCCEEDED(capture->GetNextPacketSize(&packet)) && packet > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) {
                break;
            }
            Append(reinterpret_cast<const float*>(data), frames, channels, flags & AUDCLNT_BUFFERFLAGS_SILENT);
            capture->ReleaseBuffer(frames);
        }
    }
    client->Stop();
}

void AudioTap::Append(const float* frames, UINT32 count, int channels, bool silent) {
    AcquireSRWLockExclusive(&lock_);
    const size_t size = ring_.size();
    for (UINT32 frame = 0; frame < count && size; ++frame) {
        float sum = 0.0f;
        if (!silent) {
            for (int channel = 0; channel < channels; ++channel) {
                sum += frames[static_cast<size_t>(frame) * channels + channel];
            }
        }
        ring_[write_] = channels ? sum / static_cast<float>(channels) : 0.0f;
        write_ = (write_ + 1) % size;
    }
    ReleaseSRWLockExclusive(&lock_);
    lastData_.store(GetTickCount64());
}
