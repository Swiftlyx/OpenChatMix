// NVIDIA Audio Effects (the Maxine AFX SDK) as a plugin: users install NVIDIA's redistributable for their RTX card
// and add its NVAudioEffects.dll like a VST plugin. The calls follow the SDK's public C API (nvAudioEffects.h, MIT);
// the DLL, its dependencies and the models (models\<effect>_48k.trtpkg) come from that install folder.
// A model takes seconds to load, so it loads on a thread of its own; until it is ready, and after a failure, the
// sound passes unchanged. The microphone chain hands it whole 10 ms frames; on a virtual device channel it filters
// the mix of both sides in frames of its own, one frame later.

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "app.h"
#include "log.h"
#include "plugin_editor.h"

namespace {

using Handle = void*;
using Status = int;  // NvAFX_Status; 0 is success.
using CreateEffectFn = Status (*)(const char*, Handle*);
using DestroyEffectFn = Status (*)(Handle);
using SetStringFn = Status (*)(Handle, const char*, const char*);
using SetFloatFn = Status (*)(Handle, const char*, float);
using GetU32Fn = Status (*)(Handle, const char*, unsigned int*);
using LoadFn = Status (*)(Handle);
using RunFn = Status (*)(Handle, const float**, float**, unsigned, unsigned);

constexpr Status kSuccess = 0;
constexpr unsigned kSampleRate = 48000;
constexpr size_t kFrameLength = 480;  // 10 ms, the frame of the SDK's 48 kHz models.
// Parameter names of the SDK; the older ones are what releases before 1.4 call them.
constexpr char kModelPath[] = "model_path";
constexpr char kIntensity[] = "intensity_ratio";
constexpr char kInputSampleRate[] = "input_sample_rate";
constexpr char kOlderSampleRate[] = "sample_rate";
constexpr char kInputFrameLength[] = "num_input_samples_per_frame";
constexpr char kOlderFrameLength[] = "num_samples_per_frame";

using Effect = NvidiaAfxSettings::Effect;
using ModelStatus = NvidiaAfxSettings::Status;

// The SDK's effect selector and the model file of each effect.
const char* EffectName(Effect effect) {
    switch (effect) {
    case Effect::Dereverb: return "dereverb";
    case Effect::DereverbDenoiser: return "dereverb_denoiser";
    case Effect::Denoiser: break;
    }
    return "denoiser";
}

template <typename Function>
Function Export(HMODULE module, const char* name) {
    return reinterpret_cast<Function>(GetProcAddress(module, name));
}

class NvidiaAfxPlugin final : public AudioPlugin {
public:
    ~NvidiaAfxPlugin() override {
        if (loader_.joinable()) {
            stopping_ = true;
            SetEvent(wake_);
            loader_.join();
        }
        if (wake_) {
            CloseHandle(wake_);
        }
        for (Handle handle : {next_.exchange(nullptr), retired_.exchange(nullptr), active_}) {
            if (handle) {
                destroy_(handle);
            }
        }
        // The module stays loaded; see PluginModulesLoaded().
    }

    bool Load(HMODULE module, const std::wstring& path, double sampleRate, int blockSize, int channels) {
        create_ = Export<CreateEffectFn>(module, "NvAFX_CreateEffect");
        destroy_ = Export<DestroyEffectFn>(module, "NvAFX_DestroyEffect");
        setString_ = Export<SetStringFn>(module, "NvAFX_SetString");
        setFloat_ = Export<SetFloatFn>(module, "NvAFX_SetFloat");
        getU32_ = Export<GetU32Fn>(module, "NvAFX_GetU32");
        load_ = Export<LoadFn>(module, "NvAFX_Load");
        run_ = Export<RunFn>(module, "NvAFX_Run");
        if (!create_ || !destroy_ || !setString_ || !setFloat_ || !getU32_ || !load_ || !run_) {
            Log("nvafx: the DLL lacks part of the Audio Effects API");
            return false;
        }
        if (static_cast<unsigned>(sampleRate) != kSampleRate) {
            Log("nvafx: works at %u Hz, the chain at %.0f Hz", kSampleRate, sampleRate);
            return false;
        }
        models_ = path.substr(0, path.find_last_of(L"\\/")) + L"\\models\\";
        // Anything but mono in whole frames goes through the frame buffers.
        buffered_ = channels != 1 || static_cast<size_t>(blockSize) != kFrameLength;
        input_.assign(kFrameLength, 0.0f);
        output_.assign(kFrameLength, 0.0f);
        wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!wake_) {
            return false;
        }
        // The first model loads once LoadState has set the effect.
        loader_ = std::thread([this] { LoaderLoop(); });
        Log("nvafx: models in %s%s", Utf8(models_).c_str(), buffered_ ? ", buffered" : "");
        return true;
    }

    std::wstring Name() const override { return L"NVIDIA Audio Effects"; }
    bool HasEditor() const override { return false; }
    void OpenEditor() override {}
    void CloseEditor() override {}

    std::vector<uint8_t> SaveState() override {
        char text[64];
        const int length = std::snprintf(text, sizeof(text), "effect=%d\nintensity=%d\n", effect_.load(),
                                         intensity_.load());
        return std::vector<uint8_t>(text, text + std::max(length, 0));
    }

    void LoadState(const std::vector<uint8_t>& state) override {
        const std::string text(state.begin(), state.end());
        NvidiaAfxSettings settings;
        int effect = 0;
        int intensity = 0;
        if (sscanf_s(text.c_str(), "effect=%d intensity=%d", &effect, &intensity) == 2) {
            settings.effect = static_cast<Effect>(std::clamp(effect, 0, 2));
            settings.intensityPercent = intensity;
        }
        SetNvidiaAfx(settings);
    }

    void Process(float* samples, size_t frames, int channels) override {
        AdoptLoaded();
        if (!buffered_) {
            // Anything but a whole mono frame, or the time without a model, passes through unchanged.
            if (active_ && !failed_ && frames == kFrameLength && channels == 1) {
                ProcessFrame(samples, output_.data());
                std::copy(output_.begin(), output_.end(), samples);
            }
            return;
        }
        // Each sample goes into the input frame and leaves from the frame filtered before it, so the delay stays
        // the same with and without a model.
        for (size_t frame = 0; frame < frames; ++frame) {
            float* sample = samples + frame * channels;
            const float in = channels == 1 ? sample[0] : 0.5f * (sample[0] + sample[1]);
            const float out = output_[position_];
            input_[position_] = in;
            sample[0] = out;
            if (channels > 1) {
                sample[1] = out;
            }
            if (++position_ == kFrameLength) {
                position_ = 0;
                if (active_ && !failed_) {
                    ProcessFrame(input_.data(), output_.data());
                } else {
                    output_ = input_;
                }
            }
        }
    }

    int LatencySamples() const override { return buffered_ ? static_cast<int>(kFrameLength) : 0; }

    std::optional<NvidiaAfxSettings> NvidiaAfx() const override {
        return NvidiaAfxSettings{static_cast<Effect>(effect_.load()), intensity_.load(), status_.load()};
    }

    void SetNvidiaAfx(const NvidiaAfxSettings& settings) override {
        intensity_ = std::clamp(settings.intensityPercent, 0, 100);
        effect_ = static_cast<int>(settings.effect);
        // The loader picks up a changed effect; an unchanged one costs it nothing.
        SetEvent(wake_);
    }

private:
    // Loader thread: loads the wanted effect whenever it differs from the last one loaded, and destroys the effect
    // the audio thread let go of.
    void LoaderLoop() {
        int loaded = -1;
        while (WaitForSingleObject(wake_, INFINITE) == WAIT_OBJECT_0 && !stopping_) {
            if (Handle retired = retired_.exchange(nullptr)) {
                destroy_(retired);
            }
            bool changed = false;
            for (int wanted = effect_.load(); wanted != loaded && !stopping_; wanted = effect_.load()) {
                status_ = ModelStatus::Loading;
                NotifyPluginStatus();
                changed = true;
                loaded = wanted;
                Handle handle = CreateLoaded(static_cast<Effect>(wanted));
                if (!handle) {
                    status_ = ModelStatus::Failed;
                    continue;
                }
                if (stopping_) {
                    destroy_(handle);
                    return;
                }
                if (Handle stale = next_.exchange(handle)) {
                    destroy_(stale);
                }
                status_ = ModelStatus::Ready;
            }
            if (changed) {
                NotifyPluginStatus();
            }
        }
    }

    // Loader thread: a loaded effect, or null with the reason in the log.
    Handle CreateLoaded(Effect effect) {
        const char* name = EffectName(effect);
        const std::wstring model = models_ + FromUtf8(name) + L"_48k.trtpkg";
        if (GetFileAttributesW(model.c_str()) == INVALID_FILE_ATTRIBUTES) {
            Log("nvafx: no model %s; install the Audio Effects redistributable for this graphics card",
                Utf8(model).c_str());
            return nullptr;
        }
        Handle handle = nullptr;
        Status status = create_(name, &handle);
        if (status != kSuccess || !handle) {
            Log("nvafx: %s not available, status %d", name, status);
            return nullptr;
        }
        const ULONGLONG start = GetTickCount64();
        if ((status = setString_(handle, kModelPath, Utf8(model).c_str())) != kSuccess ||
            (status = load_(handle)) != kSuccess) {
            // 11: the card has no Tensor cores; 8: the model is not for this card's architecture.
            Log("nvafx: %s did not load, status %d", name, status);
            destroy_(handle);
            return nullptr;
        }
        unsigned rate = 0;
        unsigned frame = 0;
        if (getU32_(handle, kInputSampleRate, &rate) != kSuccess) {
            getU32_(handle, kOlderSampleRate, &rate);
        }
        if (getU32_(handle, kInputFrameLength, &frame) != kSuccess) {
            getU32_(handle, kOlderFrameLength, &frame);
        }
        if (rate != kSampleRate || frame != kFrameLength) {
            Log("nvafx: %s works at %u Hz in frames of %u, expected %u and %zu", name, rate, frame, kSampleRate,
                kFrameLength);
            destroy_(handle);
            return nullptr;
        }
        Log("nvafx: %s loaded in %llu ms", name, GetTickCount64() - start);
        return handle;
    }

    // Audio thread: takes over an effect the loader finished once the one before it has been destroyed.
    void AdoptLoaded() {
        if (!next_.load(std::memory_order_relaxed) || retired_.load(std::memory_order_relaxed)) {
            return;
        }
        Handle fresh = next_.exchange(nullptr);
        if (!fresh) {
            return;
        }
        if (active_) {
            retired_ = active_;
            SetEvent(wake_);
        }
        active_ = fresh;
        appliedIntensity_ = -1;
        failed_ = false;
    }

    // Audio thread.
    void ProcessFrame(const float* in, float* out) {
        const int intensity = intensity_.load();
        if (intensity != appliedIntensity_) {
            if (setFloat_(active_, kIntensity, static_cast<float>(intensity) / 100.0f) != kSuccess &&
                appliedIntensity_ == -1) {
                Log("nvafx: the intensity could not be set");
            }
            appliedIntensity_ = intensity;
        }
        const float* inputs[] = {in};
        float* outputs[] = {out};
        if (const Status status = run_(active_, inputs, outputs, static_cast<unsigned>(kFrameLength), 1);
            status != kSuccess) {
            failed_ = true;
            std::copy(in, in + kFrameLength, out);
            Log("nvafx: processing failed, status %d; the sound passes through unprocessed", status);
        }
    }

    CreateEffectFn create_ = nullptr;
    DestroyEffectFn destroy_ = nullptr;
    SetStringFn setString_ = nullptr;
    SetFloatFn setFloat_ = nullptr;
    GetU32Fn getU32_ = nullptr;
    LoadFn load_ = nullptr;
    RunFn run_ = nullptr;
    std::wstring models_;  // The install's models folder, with a trailing backslash.

    bool buffered_ = false;
    std::vector<float> input_;   // Buffered mode: the frame being filled,
    std::vector<float> output_;  // and the one filtered before it, being played; unbuffered: the filtered frame.
    size_t position_ = 0;

    // Settings, set from the plugin thread.
    std::atomic<int> effect_{static_cast<int>(Effect::Denoiser)};
    std::atomic<int> intensity_{100};
    std::atomic<ModelStatus> status_{ModelStatus::Loading};

    // Loader thread to audio thread: the effect to switch to, and back: the one switched away from.
    std::atomic<Handle> next_{nullptr};
    std::atomic<Handle> retired_{nullptr};
    HANDLE wake_ = nullptr;
    std::thread loader_;
    std::atomic<bool> stopping_{false};

    // Audio thread only.
    Handle active_ = nullptr;
    int appliedIntensity_ = -1;
    bool failed_ = false;
};

}  // namespace

std::unique_ptr<AudioPlugin> LoadNvidiaAfxPlugin(const std::wstring& path, double sampleRate, int blockSize,
                                                 int channels, bool& recognized) {
    recognized = false;
    HMODULE module = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module || !GetProcAddress(module, "NvAFX_CreateEffect")) {
        return nullptr;
    }
    recognized = true;
    auto plugin = std::make_unique<NvidiaAfxPlugin>();
    if (!plugin->Load(module, path, sampleRate, blockSize, channels)) {
        return nullptr;
    }
    return plugin;
}
