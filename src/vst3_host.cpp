// VST 3 hosting on the interfaces of the VST 3 SDK (pluginterfaces, MIT license). This unit defines
// the interface IDs; funknown.cpp of the SDK is compiled through vst3_sdk.cpp.
#define INIT_CLASS_IID

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <variant>
#include <vector>

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "pluginterfaces/vst/ivstattributes.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include "app.h"
#include "log.h"
#include "plugin_editor.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr int32 kMaxBusChannels = 32;
constexpr size_t kMaxPendingEdits = 512;

bool SameIid(const TUID a, const FUID& b) {
    return FUnknownPrivate::iidEqual(a, b.toTUID());
}

// Host objects handed to plugins. Those created for a plugin are reference counted; the ones owned
// by a Vst3Plugin ignore the count, since they outlive every plugin reference.
template <class Interface>
class RefCounted : public Interface {
public:
    virtual ~RefCounted() = default;
    uint32 PLUGIN_API addRef() override { return ++refs_; }
    uint32 PLUGIN_API release() override {
        const uint32 refs = --refs_;
        if (refs == 0) {
            delete this;
        }
        return refs;
    }
    tresult PLUGIN_API queryInterface(const TUID iid, void** object) override {
        if (SameIid(iid, FUnknown::iid) || SameIid(iid, Interface::iid)) {
            addRef();
            *object = static_cast<Interface*>(this);
            return kResultOk;
        }
        *object = nullptr;
        return kNoInterface;
    }

private:
    std::atomic<uint32> refs_{1};
};

template <class Interface>
class Owned : public Interface {
public:
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    tresult PLUGIN_API queryInterface(const TUID iid, void** object) override {
        if (SameIid(iid, FUnknown::iid) || SameIid(iid, Interface::iid)) {
            *object = static_cast<Interface*>(this);
            return kResultOk;
        }
        *object = nullptr;
        return kNoInterface;
    }
};

class AttributeList final : public RefCounted<IAttributeList> {
public:
    tresult PLUGIN_API setInt(AttrID id, int64 value) override { return Set(id, value); }
    tresult PLUGIN_API getInt(AttrID id, int64& value) override { return Get(id, value); }
    tresult PLUGIN_API setFloat(AttrID id, double value) override { return Set(id, value); }
    tresult PLUGIN_API getFloat(AttrID id, double& value) override { return Get(id, value); }
    tresult PLUGIN_API setString(AttrID id, const TChar* string) override {
        return string ? Set(id, std::u16string(reinterpret_cast<const char16_t*>(string))) : kInvalidArgument;
    }
    tresult PLUGIN_API getString(AttrID id, TChar* string, uint32 sizeInBytes) override {
        std::u16string value;
        if (Get(id, value) != kResultOk || !string || sizeInBytes < sizeof(TChar)) {
            return kResultFalse;
        }
        const size_t count = std::min<size_t>(value.size(), sizeInBytes / sizeof(TChar) - 1);
        std::memcpy(string, value.data(), count * sizeof(TChar));
        string[count] = 0;
        return kResultOk;
    }
    tresult PLUGIN_API setBinary(AttrID id, const void* data, uint32 size) override {
        const auto* bytes = static_cast<const uint8_t*>(data);
        return Set(id, std::vector<uint8_t>(bytes, bytes + size));
    }
    tresult PLUGIN_API getBinary(AttrID id, const void*& data, uint32& size) override {
        const auto found = id ? values_.find(id) : values_.end();
        if (found == values_.end() || !std::holds_alternative<std::vector<uint8_t>>(found->second)) {
            return kResultFalse;
        }
        const auto& bytes = std::get<std::vector<uint8_t>>(found->second);
        data = bytes.data();
        size = static_cast<uint32>(bytes.size());
        return kResultOk;
    }

private:
    using Value = std::variant<int64, double, std::u16string, std::vector<uint8_t>>;

    template <class T>
    tresult Set(AttrID id, T value) {
        if (!id) {
            return kInvalidArgument;
        }
        values_[id] = std::move(value);
        return kResultOk;
    }
    template <class T>
    tresult Get(AttrID id, T& value) {
        const auto found = id ? values_.find(id) : values_.end();
        if (found == values_.end() || !std::holds_alternative<T>(found->second)) {
            return kResultFalse;
        }
        value = std::get<T>(found->second);
        return kResultOk;
    }

    std::map<std::string, Value> values_;
};

class Message final : public RefCounted<IMessage> {
public:
    FIDString PLUGIN_API getMessageID() override { return id_.c_str(); }
    void PLUGIN_API setMessageID(FIDString id) override { id_ = id ? id : ""; }
    IAttributeList* PLUGIN_API getAttributes() override { return attributes_; }
    ~Message() override { attributes_->release(); }

private:
    std::string id_;
    AttributeList* attributes_ = new AttributeList;
};

class HostApplication final : public Owned<IHostApplication> {
public:
    tresult PLUGIN_API getName(String128 name) override {
        const std::u16string text = u"OpenChatMix";
        std::memcpy(name, text.c_str(), (text.size() + 1) * sizeof(char16_t));
        return kResultOk;
    }
    tresult PLUGIN_API createInstance(TUID cid, TUID interfaceId, void** object) override {
        if (SameIid(cid, IMessage::iid) && SameIid(interfaceId, IMessage::iid)) {
            *object = static_cast<IMessage*>(new Message);
            return kResultOk;
        }
        if (SameIid(cid, IAttributeList::iid) && SameIid(interfaceId, IAttributeList::iid)) {
            *object = static_cast<IAttributeList*>(new AttributeList);
            return kResultOk;
        }
        *object = nullptr;
        return kResultFalse;
    }
};

HostApplication g_hostApplication;

class MemoryStream final : public RefCounted<IBStream> {
public:
    MemoryStream() = default;
    explicit MemoryStream(std::vector<uint8_t> data) : data_(std::move(data)) {}

    tresult PLUGIN_API read(void* buffer, int32 count, int32* done) override {
        const size_t available = data_.size() - std::min(position_, data_.size());
        const size_t length = std::min<size_t>(available, std::max(count, 0));
        if (length) {
            std::memcpy(buffer, data_.data() + position_, length);
        }
        position_ += length;
        if (done) {
            *done = static_cast<int32>(length);
        }
        return kResultOk;
    }
    tresult PLUGIN_API write(void* buffer, int32 count, int32* done) override {
        if (count < 0) {
            return kInvalidArgument;
        }
        if (position_ + count > data_.size()) {
            data_.resize(position_ + count);
        }
        std::memcpy(data_.data() + position_, buffer, count);
        position_ += count;
        if (done) {
            *done = count;
        }
        return kResultOk;
    }
    tresult PLUGIN_API seek(int64 offset, int32 mode, int64* result) override {
        int64 base = 0;
        if (mode == kIBSeekCur) {
            base = static_cast<int64>(position_);
        } else if (mode == kIBSeekEnd) {
            base = static_cast<int64>(data_.size());
        }
        const int64 target = base + offset;
        if (target < 0) {
            return kInvalidArgument;
        }
        position_ = static_cast<size_t>(target);
        if (result) {
            *result = target;
        }
        return kResultOk;
    }
    tresult PLUGIN_API tell(int64* position) override {
        if (position) {
            *position = static_cast<int64>(position_);
        }
        return kResultOk;
    }

    const std::vector<uint8_t>& Data() const { return data_; }

private:
    std::vector<uint8_t> data_;
    size_t position_ = 0;
};

// The latest value of each parameter edited since the last block, at its start.
class ParamQueue final : public Owned<IParamValueQueue> {
public:
    ParamID PLUGIN_API getParameterId() override { return id; }
    int32 PLUGIN_API getPointCount() override { return 1; }
    tresult PLUGIN_API getPoint(int32 index, int32& offset, ParamValue& point) override {
        if (index != 0) {
            return kResultFalse;
        }
        offset = 0;
        point = value;
        return kResultOk;
    }
    tresult PLUGIN_API addPoint(int32, ParamValue, int32&) override { return kResultFalse; }

    ParamID id = 0;
    ParamValue value = 0.0;
};

class ParamChanges final : public Owned<IParameterChanges> {
public:
    ParamChanges() { queues_.resize(kMaxPendingEdits); }

    int32 PLUGIN_API getParameterCount() override { return count_; }
    IParamValueQueue* PLUGIN_API getParameterData(int32 index) override {
        return index >= 0 && index < count_ ? &queues_[index] : nullptr;
    }
    IParamValueQueue* PLUGIN_API addParameterData(const ParamID& id, int32& index) override {
        (void)id;
        index = -1;
        return nullptr;
    }

    void Clear() { count_ = 0; }
    void Set(ParamID id, ParamValue value) {
        for (int32 i = 0; i < count_; ++i) {
            if (queues_[i].id == id) {
                queues_[i].value = value;
                return;
            }
        }
        if (count_ < static_cast<int32>(queues_.size())) {
            queues_[count_].id = id;
            queues_[count_].value = value;
            ++count_;
        }
    }

private:
    std::vector<ParamQueue> queues_;
    int32 count_ = 0;
};

class Vst3Plugin;

class ComponentHandler final : public Owned<IComponentHandler> {
public:
    explicit ComponentHandler(Vst3Plugin& plugin) : plugin_(plugin) {}
    tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue value) override;
    tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32) override { return kResultOk; }

private:
    Vst3Plugin& plugin_;
};

class PlugFrame final : public Owned<IPlugFrame> {
public:
    explicit PlugFrame(Vst3Plugin& plugin) : plugin_(plugin) {}
    tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* size) override;

private:
    Vst3Plugin& plugin_;
};

int32 ChannelCount(SpeakerArrangement arrangement) {
    return static_cast<int32>(std::popcount(arrangement));
}

std::wstring BinaryPath(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        return path;
    }
    // Bundle folder: Name.vst3\Contents\x86_64-win\Name.vst3
    const size_t slash = path.find_last_of(L"\\/");
    const std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
    return path + L"\\Contents\\x86_64-win\\" + name;
}

class Vst3Plugin final : public AudioPlugin, private EditorWindow::Client {
public:
    Vst3Plugin() : handler_(*this), frame_(*this) {}

    ~Vst3Plugin() override {
        CloseEditor();
        if (processor_) {
            processor_->setProcessing(false);
        }
        if (component_ && active_) {
            component_->setActive(false);
        }
        if (componentPoint_ && controllerPoint_) {
            componentPoint_->disconnect(controllerPoint_);
            controllerPoint_->disconnect(componentPoint_);
        }
        for (FUnknown* point : {static_cast<FUnknown*>(componentPoint_), static_cast<FUnknown*>(controllerPoint_)}) {
            if (point) {
                point->release();
            }
        }
        if (controller_) {
            controller_->setComponentHandler(nullptr);
            if (separateController_) {
                controller_->terminate();
            }
            controller_->release();
        }
        if (processor_) {
            processor_->release();
        }
        if (component_) {
            component_->terminate();
            component_->release();
        }
        if (factory_) {
            factory_->release();
        }
        if (module_) {
            using ExitDll = bool (*)();
            if (const auto exit = reinterpret_cast<ExitDll>(GetProcAddress(module_, "ExitDll"))) {
                exit();
            }
            // The module stays loaded; see PluginModulesLoaded().
        }
    }

    bool Load(const std::wstring& path, double rate, int block) {
        block_ = block;
        const std::wstring binary = BinaryPath(path);
        module_ = LoadLibraryExW(binary.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!module_) {
            Log("vst3: cannot load %s, error %lu", Utf8(binary).c_str(), GetLastError());
            return false;
        }
        using InitDll = bool (*)();
        if (const auto init = reinterpret_cast<InitDll>(GetProcAddress(module_, "InitDll")); init && !init()) {
            Log("vst3: InitDll failed for %s", Utf8(binary).c_str());
            return false;
        }
        using GetFactory = IPluginFactory* (PLUGIN_API*)();
        const auto getFactory = reinterpret_cast<GetFactory>(GetProcAddress(module_, "GetPluginFactory"));
        factory_ = getFactory ? getFactory() : nullptr;
        if (!factory_) {
            Log("vst3: %s has no plugin factory", Utf8(binary).c_str());
            return false;
        }

        // The first audio effect of the module.
        PClassInfo info{};
        bool found = false;
        for (int32 index = 0; index < factory_->countClasses() && !found; ++index) {
            found = factory_->getClassInfo(index, &info) == kResultOk && strcmp(info.category, kVstAudioEffectClass) == 0;
        }
        if (!found ||
            factory_->createInstance(info.cid, IComponent::iid, reinterpret_cast<void**>(&component_)) != kResultOk ||
            !component_ || component_->initialize(&g_hostApplication) != kResultOk) {
            if (component_) {
                component_->release();
                component_ = nullptr;
            }
            Log("vst3: %s has no audio effect", Utf8(binary).c_str());
            return false;
        }
        name_ = FromUtf8(info.name);
        if (name_.empty()) {
            const std::wstring file = path.substr(path.find_last_of(L"\\/") + 1);
            name_ = file.substr(0, file.find_last_of(L'.'));
        }
        if (component_->queryInterface(IAudioProcessor::iid, reinterpret_cast<void**>(&processor_)) != kResultOk ||
            !processor_) {
            Log("vst3: %s has no audio processor", Utf8(name_).c_str());
            return false;
        }
        CreateController();
        if (!SetUpBuses() || !StartProcessing(rate, block)) {
            return false;
        }
        Log("vst3: loaded %s (%d in, %d out)", Utf8(name_).c_str(), ChannelCount(inputArrangement_),
            ChannelCount(outputArrangement_));
        return true;
    }

    std::wstring Name() const override { return name_; }
    bool HasEditor() const override { return controller_ != nullptr; }

    void OpenEditor() override {
        if (editor_.IsOpen()) {
            editor_.BringToFront();
            return;
        }
        if (!controller_) {
            return;
        }
        view_ = controller_->createView(ViewType::kEditor);
        if (!view_ || view_->isPlatformTypeSupported(kPlatformTypeHWND) != kResultTrue) {
            ReleaseView();
            return;
        }
        // Editors that take the screen scale draw sharply; Windows scales the others.
        IPlugViewContentScaleSupport* scaling = nullptr;
        view_->queryInterface(IPlugViewContentScaleSupport::iid, reinterpret_cast<void**>(&scaling));
        ViewRect size{};
        view_->getSize(&size);
        const bool resizable = view_->canResize() == kResultTrue;
        if (!editor_.Create(name_, std::max<int32>(size.getWidth(), 200), std::max<int32>(size.getHeight(), 100),
                            resizable, scaling == nullptr, *this)) {
            if (scaling) {
                scaling->release();
            }
            ReleaseView();
            return;
        }
        view_->setFrame(&frame_);
        if (scaling) {
            scaling->setContentScaleFactor(editor_.Scale());
            scaling->release();
        }
        const DPI_AWARENESS_CONTEXT previous =
            scaling ? nullptr : SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE);
        const tresult attached = view_->attached(editor_.Handle(), kPlatformTypeHWND);
        if (previous) {
            SetThreadDpiAwarenessContext(previous);
        }
        if (attached != kResultOk) {
            Log("vst3: editor of %s did not open", Utf8(name_).c_str());
            CloseEditor();
            return;
        }
        // The size can change with the scale factor.
        if (view_->getSize(&size) == kResultOk) {
            editor_.SetClientSize(size.getWidth(), size.getHeight());
        }
    }

    void CloseEditor() override {
        if (view_) {
            if (editor_.IsOpen()) {
                view_->removed();
            }
            view_->setFrame(nullptr);
            ReleaseView();
        }
        editor_.Destroy();
    }

    std::vector<uint8_t> SaveState() override {
        // Component and controller state, each with its size in front.
        std::vector<uint8_t> state;
        auto append = [&](const std::vector<uint8_t>& part) {
            const uint32_t size = static_cast<uint32_t>(part.size());
            const auto* bytes = reinterpret_cast<const uint8_t*>(&size);
            state.insert(state.end(), bytes, bytes + sizeof(size));
            state.insert(state.end(), part.begin(), part.end());
        };
        auto* component = new MemoryStream;
        component_->getState(component);
        append(component->Data());
        component->release();
        auto* controller = new MemoryStream;
        if (controller_ && separateController_) {
            controller_->getState(controller);
        }
        append(controller->Data());
        controller->release();
        return state;
    }

    void LoadState(const std::vector<uint8_t>& state) override {
        size_t offset = 0;
        auto take = [&](std::vector<uint8_t>& part) {
            uint32_t size = 0;
            if (offset + sizeof(size) > state.size()) {
                return false;
            }
            std::memcpy(&size, &state[offset], sizeof(size));
            offset += sizeof(size);
            if (offset + size > state.size()) {
                return false;
            }
            part.assign(state.begin() + offset, state.begin() + offset + size);
            offset += size;
            return true;
        };
        std::vector<uint8_t> componentState;
        std::vector<uint8_t> controllerState;
        if (!take(componentState) || !take(controllerState)) {
            return;
        }
        if (!componentState.empty()) {
            auto* stream = new MemoryStream(componentState);
            component_->setState(stream);
            stream->release();
            if (controller_) {
                stream = new MemoryStream(componentState);
                controller_->setComponentState(stream);
                stream->release();
            }
        }
        if (!controllerState.empty() && controller_ && separateController_) {
            auto* stream = new MemoryStream(controllerState);
            controller_->setState(stream);
            stream->release();
        }
    }

    void Process(float* mono, size_t frames) override {
        frames = std::min<size_t>(frames, block_);
        changes_.Clear();
        {
            std::lock_guard lock(editsLock_);
            for (const auto& [id, value] : edits_) {
                changes_.Set(id, value);
            }
            edits_.clear();
        }
        for (int32 channel = 0; channel < inputBuffers_[0].numChannels; ++channel) {
            std::memcpy(inputBuffers_[0].channelBuffers32[channel], mono, frames * sizeof(float));
        }
        ProcessData data;
        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = static_cast<int32>(frames);
        data.numInputs = static_cast<int32>(inputBuffers_.size());
        data.numOutputs = static_cast<int32>(outputBuffers_.size());
        data.inputs = inputBuffers_.data();
        data.outputs = outputBuffers_.data();
        data.inputParameterChanges = &changes_;
        if (processor_->process(data) != kResultOk) {
            return;
        }
        // A stereo effect gets the microphone on both sides; its sides are averaged back to mono.
        const int32 channels = std::min(outputBuffers_[0].numChannels, 2);
        if (channels <= 0) {
            return;
        }
        for (size_t frame = 0; frame < frames; ++frame) {
            float sum = 0.0f;
            for (int32 channel = 0; channel < channels; ++channel) {
                sum += outputBuffers_[0].channelBuffers32[channel][frame];
            }
            mono[frame] = sum / channels;
        }
    }

    // From the controller (editor) on the plugin thread; reaches the processor with the next block.
    void QueueEdit(ParamID id, ParamValue value) {
        std::lock_guard lock(editsLock_);
        if (edits_.size() < kMaxPendingEdits) {
            edits_.push_back({id, value});
        }
    }

    tresult ResizeView(IPlugView* view, ViewRect* size) {
        if (!size || view != view_ || !editor_.IsOpen()) {
            return kInvalidArgument;
        }
        editor_.SetClientSize(size->getWidth(), size->getHeight());
        view_->onSize(size);
        return kResultOk;
    }

private:
    void CreateController() {
        if (component_->queryInterface(IEditController::iid, reinterpret_cast<void**>(&controller_)) != kResultOk) {
            controller_ = nullptr;
            TUID cid{};
            if (component_->getControllerClassId(cid) == kResultOk &&
                factory_->createInstance(cid, IEditController::iid, reinterpret_cast<void**>(&controller_)) == kResultOk &&
                controller_) {
                separateController_ = true;
                if (controller_->initialize(&g_hostApplication) != kResultOk) {
                    controller_->release();
                    controller_ = nullptr;
                    separateController_ = false;
                }
            }
        }
        if (!controller_) {
            return;
        }
        controller_->setComponentHandler(&handler_);
        if (separateController_) {
            // Component and controller talk to each other through their connection points.
            component_->queryInterface(Vst::IConnectionPoint::iid, reinterpret_cast<void**>(&componentPoint_));
            controller_->queryInterface(Vst::IConnectionPoint::iid, reinterpret_cast<void**>(&controllerPoint_));
            if (componentPoint_ && controllerPoint_) {
                componentPoint_->connect(controllerPoint_);
                controllerPoint_->connect(componentPoint_);
            }
            // The controller starts from the component's current state.
            auto* stream = new MemoryStream;
            if (component_->getState(stream) == kResultOk) {
                stream->seek(0, IBStream::kIBSeekSet, nullptr);
                controller_->setComponentState(stream);
            }
            stream->release();
        }
    }

    // Mono in and out where the plugin takes it, otherwise stereo, otherwise its own layout. Only the
    // main buses are active; others (side chains) get silence.
    bool SetUpBuses() {
        const int32 inputs = component_->getBusCount(kAudio, kInput);
        const int32 outputs = component_->getBusCount(kAudio, kOutput);
        if (inputs < 1 || outputs < 1) {
            Log("vst3: %s is no effect (%d input and %d output buses)", Utf8(name_).c_str(), inputs, outputs);
            return false;
        }
        std::vector<SpeakerArrangement> in(inputs);
        std::vector<SpeakerArrangement> out(outputs);
        auto current = [&] {
            for (int32 i = 0; i < inputs; ++i) {
                processor_->getBusArrangement(kInput, i, in[i]);
            }
            for (int32 i = 0; i < outputs; ++i) {
                processor_->getBusArrangement(kOutput, i, out[i]);
            }
        };
        current();
        for (const SpeakerArrangement wanted : {SpeakerArr::kMono, SpeakerArr::kStereo}) {
            in[0] = wanted;
            out[0] = wanted;
            if (processor_->setBusArrangements(in.data(), inputs, out.data(), outputs) == kResultTrue) {
                break;
            }
        }
        current();
        inputArrangement_ = in[0];
        outputArrangement_ = out[0];
        component_->activateBus(kAudio, kInput, 0, true);
        component_->activateBus(kAudio, kOutput, 0, true);
        for (int32 i = 1; i < inputs; ++i) {
            component_->activateBus(kAudio, kInput, i, false);
        }
        for (int32 i = 1; i < outputs; ++i) {
            component_->activateBus(kAudio, kOutput, i, false);
        }

        auto prepare = [&](const std::vector<SpeakerArrangement>& arrangements, std::vector<AudioBusBuffers>& buses) {
            buses.resize(arrangements.size());
            pointers_.reserve(pointers_.size() + arrangements.size());
            for (size_t bus = 0; bus < arrangements.size(); ++bus) {
                const int32 channels = std::min(ChannelCount(arrangements[bus]), kMaxBusChannels);
                std::vector<float*> pointers;
                for (int32 channel = 0; channel < channels; ++channel) {
                    samples_.emplace_back(block_, 0.0f);
                    pointers.push_back(samples_.back().data());
                }
                pointers_.push_back(std::move(pointers));
                buses[bus].numChannels = channels;
                buses[bus].silenceFlags = 0;
                buses[bus].channelBuffers32 = pointers_.back().data();
            }
        };
        samples_.reserve(static_cast<size_t>(inputs + outputs) * 2);
        prepare(in, inputBuffers_);
        prepare(out, outputBuffers_);
        return inputBuffers_[0].numChannels > 0 && outputBuffers_[0].numChannels > 0;
    }

    bool StartProcessing(double rate, int block) {
        if (processor_->canProcessSampleSize(kSample32) != kResultTrue) {
            Log("vst3: %s cannot process 32-bit samples", Utf8(name_).c_str());
            return false;
        }
        ProcessSetup setup{kRealtime, kSample32, block, rate};
        if (processor_->setupProcessing(setup) != kResultOk) {
            Log("vst3: %s rejected the processing setup", Utf8(name_).c_str());
            return false;
        }
        if (component_->setActive(true) != kResultOk) {
            Log("vst3: %s could not be activated", Utf8(name_).c_str());
            return false;
        }
        active_ = true;
        processor_->setProcessing(true);  // Optional for plugins; kNotImplemented is fine.
        return true;
    }

    void ReleaseView() {
        if (view_) {
            view_->release();
            view_ = nullptr;
        }
    }

    void OnEditorClose() override { CloseEditor(); }

    void OnEditorResized(int width, int height) override {
        if (!view_) {
            return;
        }
        ViewRect size{0, 0, width, height};
        if (view_->checkSizeConstraint(&size) == kResultTrue &&
            (size.getWidth() != width || size.getHeight() != height)) {
            editor_.SetClientSize(size.getWidth(), size.getHeight());
        }
        view_->onSize(&size);
    }

    HMODULE module_ = nullptr;
    IPluginFactory* factory_ = nullptr;
    IComponent* component_ = nullptr;
    IAudioProcessor* processor_ = nullptr;
    IEditController* controller_ = nullptr;
    bool separateController_ = false;
    bool active_ = false;
    Vst::IConnectionPoint* componentPoint_ = nullptr;
    Vst::IConnectionPoint* controllerPoint_ = nullptr;
    IPlugView* view_ = nullptr;
    ComponentHandler handler_;
    PlugFrame frame_;
    EditorWindow editor_;
    std::wstring name_;
    int block_ = 480;
    SpeakerArrangement inputArrangement_ = 0;
    SpeakerArrangement outputArrangement_ = 0;
    std::vector<std::vector<float>> samples_;
    std::vector<std::vector<float*>> pointers_;
    std::vector<AudioBusBuffers> inputBuffers_;
    std::vector<AudioBusBuffers> outputBuffers_;
    ParamChanges changes_;
    std::mutex editsLock_;
    std::vector<std::pair<ParamID, ParamValue>> edits_;
};

tresult PLUGIN_API ComponentHandler::performEdit(ParamID id, ParamValue value) {
    plugin_.QueueEdit(id, value);
    return kResultOk;
}

tresult PLUGIN_API PlugFrame::resizeView(IPlugView* view, ViewRect* size) {
    return plugin_.ResizeView(view, size);
}

}  // namespace

std::unique_ptr<AudioPlugin> LoadVst3Plugin(const std::wstring& path, double sampleRate, int blockSize) {
    auto plugin = std::make_unique<Vst3Plugin>();
    if (!plugin->Load(path, sampleRate, blockSize)) {
        return nullptr;
    }
    return plugin;
}
