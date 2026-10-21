// OpenChatMix's build of DeepFilterNet (tools/deepfilter) as a microphone chain plugin: a DLL with a small C ABI
// around libDF and its embedded DeepFilterNet3 model, which users download and add like a VST plugin.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>

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

    bool Load(HMODULE module, double sampleRate, int blockSize) {
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
        if (rate(state_) != static_cast<uint32_t>(sampleRate) || frameLength_ != static_cast<size_t>(blockSize)) {
            Log("deepfilter: works at %u Hz in %zu samples, the chain at %.0f Hz in %d", rate(state_), frameLength_,
                sampleRate, blockSize);
            return false;
        }
        Log("deepfilter: loaded %s", description());
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

    void Process(float* mono, size_t frames) override {
        // Anything but whole frames, or a model that failed once, passes through unchanged.
        if (frames != frameLength_ || failed_) {
            return;
        }
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
        if (process_(state_, mono, mono) != 0) {
            failed_ = true;
            Log("deepfilter: processing failed, the microphone passes through unprocessed");
        }
    }

    int LatencySamples() const override { return failed_ ? 0 : kLookAheadSamples; }

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
    void* state_ = nullptr;
    size_t frameLength_ = 0;
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
                                                  bool& recognized) {
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
    if (!plugin->Load(module, sampleRate, blockSize)) {
        return nullptr;
    }
    return plugin;
}
