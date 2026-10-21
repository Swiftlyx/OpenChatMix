// OpenChatMix's build of DeepFilterNet (tools/deepfilter) as a plugin: a DLL with a small C ABI around libDF and
// its embedded DeepFilterNet3 model, which users download and add like a VST plugin. The microphone chain hands it
// whole frames; on a virtual device channel (Chat: the voices of others) it filters the mix of both sides in
// frames of its own, one frame later.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "log.h"
#include "plugin_editor.h"

namespace {

// ABI 1: 48 kHz mono in frames of ocm_df_frame_length() samples; no call lets a panic reach the caller.
using AbiVersionFn = uint32_t (*)();
using DescriptionFn = const char* (*)();
using CreateFn = void* (*)();
using SampleRateFn = uint32_t (*)(void*);
using FrameLengthFn = size_t (*)(void*);
using ProcessFn = int32_t (*)(void*, const float*, float*);
using SetFloatFn = void (*)(void*, float);
using FreeFn = void (*)(void*);

constexpr uint32_t kAbiVersion = 1;
constexpr float kPostFilterBeta = 0.02f;    // libDF's post-filter strength when it is on.
constexpr int kLookAheadSamples = 960;      // Two frames of look-ahead beyond the block the chain hands over.
constexpr int kMinReductionDb = 6;

template <typename Function>
Function Export(HMODULE module, const char* name) {
    return reinterpret_cast<Function>(GetProcAddress(module, name));
}

class DeepFilterPlugin final : public AudioPlugin {
public:
    ~DeepFilterPlugin() override {
        if (state_) {
            free_(state_);
        }
        // The module stays loaded; see PluginModulesLoaded().
    }

    bool Load(HMODULE module, double sampleRate, int blockSize, int channels) {
        const auto description = Export<DescriptionFn>(module, "ocm_df_description");
        const auto create = Export<CreateFn>(module, "ocm_df_create");
        const auto rate = Export<SampleRateFn>(module, "ocm_df_sample_rate");
        const auto frame = Export<FrameLengthFn>(module, "ocm_df_frame_length");
        process_ = Export<ProcessFn>(module, "ocm_df_process");
        setLimit_ = Export<SetFloatFn>(module, "ocm_df_set_atten_lim");
        setPostFilter_ = Export<SetFloatFn>(module, "ocm_df_set_post_filter_beta");
        free_ = Export<FreeFn>(module, "ocm_df_free");
        if (!description || !create || !rate || !frame || !process_ || !setLimit_ || !setPostFilter_ || !free_) {
            Log("deepfilter: the DLL lacks part of ABI %u", kAbiVersion);
            return false;
        }
        state_ = create();
        if (!state_) {
            Log("deepfilter: the model did not load");
            return false;
        }
        frameLength_ = frame(state_);
        if (rate(state_) != static_cast<uint32_t>(sampleRate) || frameLength_ == 0) {
            Log("deepfilter: works at %u Hz, the chain at %.0f Hz", rate(state_), sampleRate);
            return false;
        }
        // Anything but mono in whole frames goes through the frame buffers.
        buffered_ = channels != 1 || frameLength_ != static_cast<size_t>(blockSize);
        if (buffered_) {
            input_.assign(frameLength_, 0.0f);
            output_.assign(frameLength_, 0.0f);
        }
        Log("deepfilter: loaded %s%s", description(), buffered_ ? ", buffered" : "");
        return true;
    }

    std::wstring Name() const override { return L"DeepFilterNet"; }
    bool HasEditor() const override { return false; }
    void OpenEditor() override {}
    void CloseEditor() override {}

    std::vector<uint8_t> SaveState() override {
        char text[64];
        const int length = std::snprintf(text, sizeof(text), "max_reduction=%d\npost_filter=%d\n",
                                         maxReduction_.load(), postFilter_.load() ? 1 : 0);
        return std::vector<uint8_t>(text, text + std::max(length, 0));
    }

    void LoadState(const std::vector<uint8_t>& state) override {
        const std::string text(state.begin(), state.end());
        DeepFilterSettings settings;
        int reduction = 0;
        int postFilter = 0;
        if (sscanf_s(text.c_str(), "max_reduction=%d post_filter=%d", &reduction, &postFilter) == 2) {
            settings.maxReductionDb = reduction;
            settings.postFilter = postFilter != 0;
        }
        SetDeepFilter(settings);
    }

    void Process(float* samples, size_t frames, int channels) override {
        // A model that failed once lets the sound pass unchanged.
        if (failed_) {
            return;
        }
        if (!buffered_) {
            // Anything but a whole frame passes through unchanged.
            if (frames == frameLength_ && channels == 1) {
                ProcessFrame(samples, samples);
            }
            return;
        }
        // Each sample goes into the input frame and leaves from the frame filtered before it.
        for (size_t frame = 0; frame < frames; ++frame) {
            float* sample = samples + frame * channels;
            const float in = channels == 1 ? sample[0] : 0.5f * (sample[0] + sample[1]);
            const float out = output_[position_];
            input_[position_] = in;
            sample[0] = out;
            if (channels > 1) {
                sample[1] = out;
            }
            if (++position_ == frameLength_) {
                position_ = 0;
                ProcessFrame(input_.data(), output_.data());
                if (failed_) {
                    return;
                }
            }
        }
    }

    int LatencySamples() const override {
        return failed_ ? 0 : kLookAheadSamples + (buffered_ ? static_cast<int>(frameLength_) : 0);
    }

    std::optional<DeepFilterSettings> DeepFilter() const override {
        return DeepFilterSettings{maxReduction_.load(), postFilter_.load()};
    }

    void SetDeepFilter(const DeepFilterSettings& settings) override {
        const int reduction = settings.maxReductionDb >= DeepFilterSettings::kUnlimited
                                  ? DeepFilterSettings::kUnlimited
                                  : std::clamp(settings.maxReductionDb, kMinReductionDb,
                                               DeepFilterSettings::kUnlimited);
        maxReduction_ = reduction;
        postFilter_ = settings.postFilter;
    }

private:
    void ProcessFrame(const float* in, float* out) {
        const int reduction = maxReduction_.load();
        if (reduction != appliedReduction_) {
            setLimit_(state_, static_cast<float>(reduction));
            appliedReduction_ = reduction;
        }
        const bool postFilter = postFilter_.load();
        if (postFilter != appliedPostFilter_) {
            setPostFilter_(state_, postFilter ? kPostFilterBeta : 0.0f);
            appliedPostFilter_ = postFilter;
        }
        if (process_(state_, in, out) != 0) {
            failed_ = true;
            Log("deepfilter: processing failed, the sound passes through unprocessed");
        }
    }

    void* state_ = nullptr;
    size_t frameLength_ = 0;
    bool buffered_ = false;
    std::vector<float> input_;   // Buffered mode, audio thread only: the frame being filled,
    std::vector<float> output_;  // and the one filtered before it, being played.
    size_t position_ = 0;
    ProcessFn process_ = nullptr;
    SetFloatFn setLimit_ = nullptr;
    SetFloatFn setPostFilter_ = nullptr;
    FreeFn free_ = nullptr;
    // Set from the plugin thread, applied on the audio thread before the next frame.
    std::atomic<int> maxReduction_{DeepFilterSettings::kUnlimited};
    std::atomic<bool> postFilter_{false};
    // Audio thread only.
    int appliedReduction_ = -1;
    bool appliedPostFilter_ = false;
    std::atomic<bool> failed_{false};
};

}  // namespace

std::unique_ptr<AudioPlugin> LoadDeepFilterPlugin(const std::wstring& path, double sampleRate, int blockSize,
                                                  int channels, bool& recognized) {
    recognized = false;
    HMODULE module = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) {
        return nullptr;
    }
    const auto version = Export<AbiVersionFn>(module, "ocm_df_abi_version");
    if (!version) {
        return nullptr;
    }
    recognized = true;
    if (version() != kAbiVersion) {
        Log("deepfilter: ABI %u, this version of OpenChatMix needs %u", version(), kAbiVersion);
        return nullptr;
    }
    auto plugin = std::make_unique<DeepFilterPlugin>();
    if (!plugin->Load(module, sampleRate, blockSize, channels)) {
        return nullptr;
    }
    return plugin;
}
