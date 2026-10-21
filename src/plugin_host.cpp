#include "plugin_host.h"

#include <ole2.h>

#include <algorithm>
#include <atomic>
#include <cstring>

#include "app.h"
#include "log.h"
#include "plugin_editor.h"
#include "resource.h"

namespace {

constexpr wchar_t kThreadWindowClass[] = L"OpenChatMix.PluginThread";
constexpr wchar_t kEditorWindowClass[] = L"OpenChatMix.PluginEditor";
constexpr UINT WM_RUN = WM_APP + 1;
constexpr UINT_PTR kIdleTimer = 1;
constexpr UINT kIdleMs = 33;
constexpr DWORD kStartTimeoutMs = 5000;
constexpr DWORD kStopTimeoutMs = 5000;

std::wstring FileStem(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
    return name.substr(0, name.find_last_of(L'.'));
}

bool EndsWithNoCase(const std::wstring& text, const wchar_t* suffix) {
    const size_t length = wcslen(suffix);
    return text.size() >= length && _wcsicmp(text.c_str() + text.size() - length, suffix) == 0;
}

// ---- VST2 ----------------------------------------------------------------------------------------
// The VST 2 binary interface, declared from its public documentation: the SDK itself is no longer
// distributed, but plugins built with it keep working with any host that speaks the same layout.

struct AEffect;
using AudioMasterCallback = intptr_t (*)(AEffect*, int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt);
using DispatcherProc = intptr_t (*)(AEffect*, int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt);
using ProcessProc = void (*)(AEffect*, float** inputs, float** outputs, int32_t frames);
using SetParameterProc = void (*)(AEffect*, int32_t index, float value);
using GetParameterProc = float (*)(AEffect*, int32_t index);
using PluginMain = AEffect* (*)(AudioMasterCallback);

struct AEffect {
    int32_t magic;
    DispatcherProc dispatcher;
    ProcessProc process;  // Deprecated accumulating process.
    SetParameterProc setParameter;
    GetParameterProc getParameter;
    int32_t numPrograms;
    int32_t numParams;
    int32_t numInputs;
    int32_t numOutputs;
    int32_t flags;
    intptr_t resvd1;  // Reserved for the host; holds the Vst2Plugin.
    intptr_t resvd2;
    int32_t initialDelay;
    int32_t realQualities;
    int32_t offQualities;
    float ioRatio;
    void* object;
    void* user;
    int32_t uniqueID;
    int32_t version;
    ProcessProc processReplacing;
    void* processDoubleReplacing;
    char future[56];
};

struct ERect {
    int16_t top, left, bottom, right;
};

constexpr int32_t kEffectMagic = 0x56737450;  // 'VstP'
enum : int32_t {
    effOpen = 0,
    effClose = 1,
    effSetSampleRate = 10,
    effSetBlockSize = 11,
    effMainsChanged = 12,
    effEditGetRect = 13,
    effEditOpen = 14,
    effEditClose = 15,
    effEditIdle = 19,
    effGetChunk = 23,
    effSetChunk = 24,
    effGetEffectName = 45,
    effGetProductString = 48,
    effStartProcess = 71,
    effStopProcess = 72,
    effSetProcessPrecision = 77,
};
enum : int32_t {
    audioMasterAutomate = 0,
    audioMasterVersion = 1,
    audioMasterCurrentId = 2,
    audioMasterIdle = 3,
    audioMasterGetTime = 7,
    audioMasterIOChanged = 13,
    audioMasterSizeWindow = 15,
    audioMasterGetSampleRate = 16,
    audioMasterGetBlockSize = 17,
    audioMasterGetCurrentProcessLevel = 23,
    audioMasterGetAutomationState = 24,
    audioMasterGetVendorString = 32,
    audioMasterGetProductString = 33,
    audioMasterGetVendorVersion = 34,
    audioMasterCanDo = 37,
    audioMasterGetLanguage = 38,
    audioMasterUpdateDisplay = 42,
    audioMasterBeginEdit = 43,
    audioMasterEndEdit = 44,
};
constexpr int32_t effFlagsHasEditor = 1 << 0;
constexpr int32_t effFlagsCanReplacing = 1 << 4;
constexpr int32_t effFlagsProgramChunks = 1 << 5;
constexpr int32_t effFlagsIsSynth = 1 << 8;
constexpr int32_t kMaxChannels = 32;

class Vst2Plugin;
thread_local Vst2Plugin* g_loading = nullptr;  // The plugin whose entry point runs, before resvd1 is set.

class Vst2Plugin final : public AudioPlugin, private EditorWindow::Client {
public:
    ~Vst2Plugin() override {
        CloseEditor();
        if (effect_) {
            Dispatch(effStopProcess);
            Dispatch(effMainsChanged, 0, 0);
            Dispatch(effClose);
        }
        // The module stays loaded; see PluginModulesLoaded().
    }

    bool Load(const std::wstring& path, double rate, int block) {
        rate_ = rate;
        block_ = block;
        module_ = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!module_) {
            Log("vst2: cannot load %s, error %lu", Utf8(path).c_str(), GetLastError());
            return false;
        }
        auto main = reinterpret_cast<PluginMain>(GetProcAddress(module_, "VSTPluginMain"));
        if (!main) {
            main = reinterpret_cast<PluginMain>(GetProcAddress(module_, "main"));
        }
        if (!main) {
            Log("vst2: %s has no VST entry point", Utf8(path).c_str());
            return false;
        }
        g_loading = this;
        effect_ = main(&AudioMaster);
        g_loading = nullptr;
        if (!effect_ || effect_->magic != kEffectMagic) {
            effect_ = nullptr;
            Log("vst2: %s is not a VST 2 plugin", Utf8(path).c_str());
            return false;
        }
        effect_->resvd1 = reinterpret_cast<intptr_t>(this);
        Dispatch(effOpen);
        if (!(effect_->flags & effFlagsCanReplacing) || (effect_->flags & effFlagsIsSynth) || effect_->numInputs < 1 ||
            effect_->numInputs > kMaxChannels || effect_->numOutputs > kMaxChannels) {
            Log("vst2: %s is no effect this host can run (%d in, %d out, flags 0x%x)", Utf8(path).c_str(),
                effect_->numInputs, effect_->numOutputs, effect_->flags);
            return false;
        }
        char text[256] = {};
        Dispatch(effGetEffectName, 0, 0, text);
        if (!text[0]) {
            Dispatch(effGetProductString, 0, 0, text);
        }
        name_ = text[0] ? FromUtf8(text) : FileStem(path);

        Dispatch(effSetSampleRate, 0, 0, nullptr, static_cast<float>(rate));
        Dispatch(effSetBlockSize, 0, block);
        Dispatch(effSetProcessPrecision, 0, 0);  // 32-bit float.
        inputs_.assign(static_cast<size_t>(effect_->numInputs) * block, 0.0f);
        outputs_.assign(static_cast<size_t>(std::max(effect_->numOutputs, 1)) * block, 0.0f);
        for (int32_t channel = 0; channel < effect_->numInputs; ++channel) {
            inputPointers_.push_back(&inputs_[static_cast<size_t>(channel) * block]);
        }
        for (int32_t channel = 0; channel < std::max(effect_->numOutputs, 1); ++channel) {
            outputPointers_.push_back(&outputs_[static_cast<size_t>(channel) * block]);
        }
        Dispatch(effMainsChanged, 0, 1);
        Dispatch(effStartProcess);
        Log("vst2: loaded %s (%d in, %d out)", Utf8(name_).c_str(), effect_->numInputs, effect_->numOutputs);
        return true;
    }

    std::wstring Name() const override { return name_; }
    bool HasEditor() const override { return (effect_->flags & effFlagsHasEditor) != 0; }

    void OpenEditor() override {
        if (editor_.IsOpen()) {
            editor_.BringToFront();
            return;
        }
        if (!HasEditor()) {
            return;
        }
        int width = 0;
        int height = 0;
        EditorSize(width, height);
        // VST 2 has no way to tell an editor the screen scale, so Windows scales it.
        if (!editor_.Create(name_, std::max(width, 200), std::max(height, 100), false, true, *this)) {
            return;
        }
        const DPI_AWARENESS_CONTEXT previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE);
        Dispatch(effEditOpen, 0, 0, editor_.Handle());
        SetThreadDpiAwarenessContext(previous);
        // Many plugins report their size only once the editor is open.
        if (EditorSize(width, height)) {
            editor_.SetClientSize(width, height);
        }
    }

    void CloseEditor() override {
        if (editor_.IsOpen()) {
            Dispatch(effEditClose);
            editor_.Destroy();
        }
    }

    std::vector<uint8_t> SaveState() override {
        std::vector<uint8_t> state;
        if (effect_->flags & effFlagsProgramChunks) {
            void* data = nullptr;
            const intptr_t size = Dispatch(effGetChunk, 0, 0, &data);
            if (data && size > 0) {
                state.push_back('C');
                state.insert(state.end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
                return state;
            }
        }
        state.push_back('P');
        for (int32_t index = 0; index < effect_->numParams; ++index) {
            const float value = effect_->getParameter(effect_, index);
            const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
            state.insert(state.end(), bytes, bytes + sizeof(value));
        }
        return state;
    }

    void LoadState(const std::vector<uint8_t>& state) override {
        if (state.empty()) {
            return;
        }
        if (state[0] == 'C' && state.size() > 1) {
            std::vector<uint8_t> chunk(state.begin() + 1, state.end());
            Dispatch(effSetChunk, 0, static_cast<intptr_t>(chunk.size()), chunk.data());
        } else if (state[0] == 'P') {
            const size_t count = std::min<size_t>((state.size() - 1) / sizeof(float), effect_->numParams);
            for (size_t index = 0; index < count; ++index) {
                float value = 0.0f;
                std::memcpy(&value, &state[1 + index * sizeof(float)], sizeof(value));
                effect_->setParameter(effect_, static_cast<int32_t>(index), value);
            }
        }
    }

    void Process(float* mono, size_t frames) override {
        frames = std::min<size_t>(frames, block_);
        for (float* input : inputPointers_) {
            std::memcpy(input, mono, frames * sizeof(float));
        }
        effect_->processReplacing(effect_, inputPointers_.data(), outputPointers_.data(), static_cast<int32_t>(frames));
        // A stereo effect gets the microphone on both sides; its sides are averaged back to mono.
        const int32_t outputs = std::min(effect_->numOutputs, 2);
        if (outputs <= 0) {
            return;
        }
        for (size_t frame = 0; frame < frames; ++frame) {
            float sum = 0.0f;
            for (int32_t channel = 0; channel < outputs; ++channel) {
                sum += outputPointers_[channel][frame];
            }
            mono[frame] = sum / outputs;
        }
    }

private:
    intptr_t Dispatch(int32_t opcode, int32_t index = 0, intptr_t value = 0, void* ptr = nullptr, float opt = 0.0f) {
        return effect_->dispatcher(effect_, opcode, index, value, ptr, opt);
    }

    bool EditorSize(int& width, int& height) {
        ERect* rect = nullptr;
        Dispatch(effEditGetRect, 0, 0, &rect);
        if (!rect || rect->right <= rect->left || rect->bottom <= rect->top) {
            return false;
        }
        width = rect->right - rect->left;
        height = rect->bottom - rect->top;
        return true;
    }

    void OnEditorClose() override { CloseEditor(); }
    void OnEditorIdle() override { Dispatch(effEditIdle); }

    static intptr_t AudioMaster(AEffect* effect, int32_t opcode, int32_t index, intptr_t value, void* ptr, float) {
        Vst2Plugin* self = effect && effect->resvd1 ? reinterpret_cast<Vst2Plugin*>(effect->resvd1) : g_loading;
        switch (opcode) {
        case audioMasterVersion:
            return 2400;
        case audioMasterCurrentId:
            return effect ? effect->uniqueID : 0;
        case audioMasterGetSampleRate:
            return self ? static_cast<intptr_t>(self->rate_) : 48000;
        case audioMasterGetBlockSize:
            return self ? self->block_ : 480;
        case audioMasterGetVendorString:
        case audioMasterGetProductString:
            if (ptr) {
                strcpy_s(static_cast<char*>(ptr), 64, "OpenChatMix");
            }
            return 1;
        case audioMasterGetVendorVersion:
            return 1;
        case audioMasterGetLanguage:
            return 1;  // English.
        case audioMasterGetAutomationState:
            return 1;  // Off.
        case audioMasterCanDo:
            return ptr && strcmp(static_cast<const char*>(ptr), "sizeWindow") == 0 ? 1 : 0;
        case audioMasterSizeWindow:
            if (self && self->editor_.IsOpen()) {
                self->editor_.SetClientSize(index, static_cast<int>(value));
                return 1;
            }
            return 0;
        case audioMasterAutomate:
        case audioMasterBeginEdit:
        case audioMasterEndEdit:
        case audioMasterIdle:
        case audioMasterUpdateDisplay:
        case audioMasterIOChanged:
        case audioMasterGetTime:
        case audioMasterGetCurrentProcessLevel:
            return 0;
        }
        return 0;
    }

    HMODULE module_ = nullptr;
    AEffect* effect_ = nullptr;
    std::wstring name_;
    double rate_ = 48000.0;
    int block_ = 480;
    std::vector<float> inputs_;
    std::vector<float> outputs_;
    std::vector<float*> inputPointers_;
    std::vector<float*> outputPointers_;
    EditorWindow editor_;
};

}  // namespace

// ---- Editor window --------------------------------------------------------------------------------

EditorWindow::~EditorWindow() {
    Destroy();
}

bool EditorWindow::Create(const std::wstring& title, int width, int height, bool resizable, bool dpiUnaware,
                          Client& client) {
    Destroy();
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP));
        windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        windowClass.lpszClassName = kEditorWindowClass;
        RegisterClassExW(&windowClass);
        registered = true;
    }
    client_ = &client;
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
                        (resizable ? WS_THICKFRAME | WS_MAXIMIZEBOX : 0);
    const DPI_AWARENESS_CONTEXT previous =
        dpiUnaware ? SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE) : nullptr;
    RECT rect{0, 0, width, height};
    AdjustWindowRectEx(&rect, style, FALSE, 0);
    window_ = CreateWindowExW(0, kEditorWindowClass, (title + L" · " + kAppName).c_str(), style, CW_USEDEFAULT,
                              CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr,
                              GetModuleHandleW(nullptr), this);
    if (previous) {
        SetThreadDpiAwarenessContext(previous);
    }
    if (!window_) {
        Log("plugin editor: window failed, error %lu", GetLastError());
        return false;
    }
    SetTimer(window_, kIdleTimer, kIdleMs, nullptr);
    ShowWindow(window_, SW_SHOW);
    SetForegroundWindow(window_);
    return true;
}

void EditorWindow::Destroy() {
    if (window_) {
        HWND window = window_;
        window_ = nullptr;
        DestroyWindow(window);
    }
}

void EditorWindow::BringToFront() {
    if (window_) {
        if (IsIconic(window_)) {
            ShowWindow(window_, SW_RESTORE);
        }
        SetForegroundWindow(window_);
    }
}

void EditorWindow::SetClientSize(int width, int height) {
    if (!window_ || width <= 0 || height <= 0) {
        return;
    }
    RECT rect{0, 0, width, height};
    AdjustWindowRectExForDpi(&rect, static_cast<DWORD>(GetWindowLongPtrW(window_, GWL_STYLE)), FALSE, 0,
                             GetDpiForWindow(window_));
    resizing_ = true;
    SetWindowPos(window_, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    resizing_ = false;
}

float EditorWindow::Scale() const {
    return window_ ? GetDpiForWindow(window_) / 96.0f : 1.0f;
}

LRESULT CALLBACK EditorWindow::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
    }
    auto* self = reinterpret_cast<EditorWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    // Messages during DestroyWindow arrive after window_ was cleared and go to the default handler.
    if (self && self->window_ == window && self->client_) {
        switch (message) {
        case WM_CLOSE:
            self->client_->OnEditorClose();
            return 0;
        case WM_TIMER:
            if (wParam == kIdleTimer) {
                self->client_->OnEditorIdle();
                return 0;
            }
            break;
        case WM_SIZE:
            if (!self->resizing_ && wParam != SIZE_MINIMIZED) {
                self->client_->OnEditorResized(LOWORD(lParam), HIWORD(lParam));
            }
            return 0;
        }
    }
    if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

// ---- Loading ---------------------------------------------------------------------------------------

namespace {

std::atomic<bool> g_modulesLoaded{false};

}  // namespace

bool PluginModulesLoaded() {
    return g_modulesLoaded;
}

std::unique_ptr<AudioPlugin> LoadAudioPlugin(const std::wstring& path, double sampleRate, int blockSize) {
    g_modulesLoaded = true;
    if (EndsWithNoCase(path, L".vst3")) {
        return LoadVst3Plugin(path, sampleRate, blockSize);
    }
    bool deepFilter = false;
    if (auto plugin = LoadDeepFilterPlugin(path, sampleRate, blockSize, deepFilter); plugin || deepFilter) {
        return plugin;
    }
    auto plugin = std::make_unique<Vst2Plugin>();
    if (!plugin->Load(path, sampleRate, blockSize)) {
        return nullptr;
    }
    return plugin;
}

// ---- Plugin thread ---------------------------------------------------------------------------------

PluginThread::~PluginThread() {
    Stop();
}

bool PluginThread::Start() {
    if (thread_) {
        return true;
    }
    ready_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = ready_ ? CreateThread(nullptr, 0, ThreadProc, this, 0, nullptr) : nullptr;
    if (!thread_) {
        Stop();
        return false;
    }
    const HANDLE waits[] = {ready_, thread_};
    WaitForMultipleObjects(2, waits, FALSE, kStartTimeoutMs);
    if (!window_) {
        Stop();
        return false;
    }
    return true;
}

void PluginThread::Stop() {
    if (thread_) {
        if (window_) {
            PostMessageW(window_, WM_CLOSE, 0, 0);
        }
        if (WaitForSingleObject(thread_, kStopTimeoutMs) != WAIT_OBJECT_0) {
            Log("plugin thread did not stop");
        }
        CloseHandle(thread_);
        thread_ = nullptr;
    }
    if (ready_) {
        CloseHandle(ready_);
        ready_ = nullptr;
    }
    window_ = nullptr;
}

void PluginThread::Run(const std::function<void()>& work) {
    if (window_) {
        SendMessageW(window_, WM_RUN, 0, reinterpret_cast<LPARAM>(&work));
    }
}

DWORD WINAPI PluginThread::ThreadProc(void* param) {
    auto* self = static_cast<PluginThread*>(param);
    const HRESULT ole = OleInitialize(nullptr);
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = kThreadWindowClass;
        RegisterClassExW(&windowClass);
        registered = true;
    }
    self->window_ = CreateWindowExW(0, kThreadWindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                    GetModuleHandleW(nullptr), nullptr);
    SetEvent(self->ready_);
    if (self->window_) {
        MSG message;
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (SUCCEEDED(ole)) {
        OleUninitialize();
    }
    return 0;
}

LRESULT CALLBACK PluginThread::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_RUN:
        (*reinterpret_cast<const std::function<void()>*>(lParam))();
        return 0;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
