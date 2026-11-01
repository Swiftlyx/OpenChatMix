// The PC's sensors without a driver of our own: Windows for the CPU load, the memory and the GPU (D3DKMT
// performance data and the GPU Engine counters, as Task Manager shows them), NVIDIA's NVML (part of the driver) for
// a GPU Windows tells nothing about, and for the CPU temperature and the case fans the sensor tool the user runs:
// HWiNFO (its shared memory or its gadget values in the registry) or LibreHardwareMonitor / Open Hardware Monitor
// (their WMI classes), else PawnIO when it is installed and this program runs as administrator.

#define _WIN32_DCOM
#include "infoscreen.h"

#include <d3dkmthk.h>
#include <pdh.h>
#include <wbemidl.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <sstream>
#include <type_traits>

namespace {

constexpr ULONGLONG kRetryMs = 15000;  // A source that is not there is looked for again after this long.

// HWiNFO's shared memory, as its SDK documents it.
constexpr wchar_t kHwinfoMapping[] = L"Global\\HWiNFO_SENS_SM2";
constexpr wchar_t kHwinfoGadgetKey[] = L"Software\\HWiNFO64\\VSB";
constexpr DWORD kHwinfoTemp = 1;
constexpr DWORD kHwinfoFan = 3;
constexpr DWORD kHwinfoUsage = 7;
constexpr DWORD kHwinfoOther = 8;
constexpr size_t kHwinfoText = 128;

#pragma pack(push, 1)
struct HwinfoHeader {
    char signature[4];  // "HWiS" while it runs; "DEAD" once Shared Memory Support is off.
    DWORD version;
    DWORD revision;
    __int64 pollTime;
    DWORD sensorOffset;
    DWORD sensorSize;
    DWORD sensorCount;
    DWORD readingOffset;
    DWORD readingSize;
    DWORD readingCount;
};
struct HwinfoSensor {
    DWORD id;
    DWORD instance;
    char nameOriginal[kHwinfoText];
    char nameUser[kHwinfoText];
};
struct HwinfoReading {
    DWORD type;
    DWORD sensorIndex;
    DWORD readingId;
    char labelOriginal[kHwinfoText];
    char labelUser[kHwinfoText];
    char unit[16];
    double value;
    double minimum;
    double maximum;
    double average;
    // Newer versions add the user's label and the unit in UTF-8 here.
};
#pragma pack(pop)

// NVML, loaded from the driver; the subset used here, as NVIDIA's nvml.h declares it.
using NvmlDevice = void*;
struct NvmlUtilization {
    unsigned gpu;
    unsigned memory;
};
struct NvmlFanSpeedInfo {
    unsigned version;
    unsigned fan;
    unsigned speed;
};
constexpr unsigned kNvmlFanSpeedInfoVersion = sizeof(NvmlFanSpeedInfo) | (1u << 24);
constexpr int kNvmlSuccess = 0;
constexpr int kNvmlTemperatureGpu = 0;

// What Windows reports about the dedicated GPU.
struct AdapterFigures {
    LUID luid{};  // Of the adapter the figures are from.
    ULONG deciCelsius = 0;
    ULONG fanRpm = 0;
    ULONG maxFanRpm = 0;  // 0 when the driver does not report its fan.
};

// One reading of a sensor tool, whichever it is.
struct ToolReading {
    enum class Kind { Temp, Fan, Percent } kind;
    std::wstring device;  // The hardware it belongs to.
    std::wstring name;
    double value = 0.0;
    bool cpu = false;
    bool gpu = false;
    std::wstring pairKey;  // Fans and their controls share it, where the tool tells.
};

bool Contains(const std::wstring& text, const wchar_t* part) {
    std::wstring lower = text;
    std::wstring wanted = part;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
    std::transform(wanted.begin(), wanted.end(), wanted.begin(), ::towlower);
    return lower.find(wanted) != std::wstring::npos;
}

std::wstring FromCodePage(const char* text, size_t size, UINT codePage) {
    const size_t length = strnlen(text, size);
    if (!length) {
        return {};
    }
    const int count = MultiByteToWideChar(codePage, 0, text, static_cast<int>(length), nullptr, 0);
    std::wstring wide(count, L'\0');
    MultiByteToWideChar(codePage, 0, text, static_cast<int>(length), wide.data(), count);
    return wide;
}

// How well a temperature names the CPU as a whole; 0 for not at all.
int CpuTempScore(const std::wstring& name, bool cpuDevice) {
    if (Contains(name, L"Tctl/Tdie") || Contains(name, L"CPU Package")) {
        return 5;
    }
    if (Contains(name, L"Tctl") || Contains(name, L"Tdie") || Contains(name, L"CPU Die")) {
        return 4;
    }
    if (cpuDevice && (Contains(name, L"Core Max") || Contains(name, L"Core Average"))) {
        return 3;
    }
    if (cpuDevice && Contains(name, L"CPU")) {
        return 2;
    }
    return cpuDevice ? 1 : 0;
}

}  // namespace

struct Sensors::Impl {
    // CPU load.
    ULONGLONG lastIdle = 0;
    ULONGLONG lastTotal = 0;

    // NVML.
    HMODULE nvml = nullptr;
    bool nvmlTried = false;
    NvmlDevice gpu = nullptr;
    int(__cdecl* nvmlInit)() = nullptr;
    int(__cdecl* nvmlShutdown)() = nullptr;
    int(__cdecl* deviceCount)(unsigned*) = nullptr;
    int(__cdecl* deviceByIndex)(unsigned, NvmlDevice*) = nullptr;
    int(__cdecl* temperature)(NvmlDevice, int, unsigned*) = nullptr;
    int(__cdecl* utilization)(NvmlDevice, NvmlUtilization*) = nullptr;
    int(__cdecl* fanCount)(NvmlDevice, unsigned*) = nullptr;
    int(__cdecl* fanSpeed)(NvmlDevice, unsigned, unsigned*) = nullptr;
    int(__cdecl* fanSpeedRpm)(NvmlDevice, NvmlFanSpeedInfo*) = nullptr;

    // HWiNFO.
    HANDLE hwinfoMapping = nullptr;
    const uint8_t* hwinfoView = nullptr;
    ULONGLONG hwinfoRetry = 0;

    // LibreHardwareMonitor / Open Hardware Monitor.
    bool comReady = false;
    IWbemServices* wmi = nullptr;
    std::wstring wmiName;
    ULONGLONG wmiRetry = 0;

    // Windows' own GPU figures.
    PDH_HQUERY gpuQuery = nullptr;
    PDH_HCOUNTER gpuCounter = nullptr;
    bool gpuQueryTried = false;

    std::wstring toolName;  // The sensor tool read last, for --sources.
    PawnIo pawnIo;

    ~Impl() {
        if (nvml) {
            if (nvmlShutdown) {
                nvmlShutdown();
            }
            FreeLibrary(nvml);
        }
        CloseHwinfo();
        if (wmi) {
            wmi->Release();
        }
        if (gpuQuery) {
            PdhCloseQuery(gpuQuery);
        }
        if (comReady) {
            CoUninitialize();
        }
    }

    std::optional<double> CpuLoad() {
        FILETIME idle{};
        FILETIME kernel{};
        FILETIME user{};
        if (!GetSystemTimes(&idle, &kernel, &user)) {
            return std::nullopt;
        }
        auto ticks = [](const FILETIME& time) {
            return (static_cast<ULONGLONG>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
        };
        // Kernel time includes the idle time.
        const ULONGLONG idleNow = ticks(idle);
        const ULONGLONG totalNow = ticks(kernel) + ticks(user);
        const ULONGLONG idleDelta = idleNow - lastIdle;
        const ULONGLONG totalDelta = totalNow - lastTotal;
        const bool first = lastTotal == 0;
        lastIdle = idleNow;
        lastTotal = totalNow;
        if (first || totalDelta == 0) {
            return std::nullopt;
        }
        return std::clamp(100.0 * static_cast<double>(totalDelta - idleDelta) / static_cast<double>(totalDelta), 0.0,
                          100.0);
    }

    bool LoadNvml() {
        if (nvmlTried) {
            return gpu != nullptr;
        }
        nvmlTried = true;
        nvml = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!nvml) {
            wchar_t folder[MAX_PATH];
            if (ExpandEnvironmentStringsW(L"%ProgramFiles%\\NVIDIA Corporation\\NVSMI\\nvml.dll", folder, MAX_PATH)) {
                nvml = LoadLibraryExW(folder, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
            }
        }
        if (!nvml) {
            return false;
        }
        auto get = [&](auto& function, const char* name) {
            function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(GetProcAddress(nvml, name));
        };
        get(nvmlInit, "nvmlInit_v2");
        get(nvmlShutdown, "nvmlShutdown");
        get(deviceCount, "nvmlDeviceGetCount_v2");
        get(deviceByIndex, "nvmlDeviceGetHandleByIndex_v2");
        get(temperature, "nvmlDeviceGetTemperature");
        get(utilization, "nvmlDeviceGetUtilizationRates");
        get(fanCount, "nvmlDeviceGetNumFans");
        get(fanSpeed, "nvmlDeviceGetFanSpeed_v2");
        get(fanSpeedRpm, "nvmlDeviceGetFanSpeedRPM");
        unsigned count = 0;
        if (!nvmlInit || !deviceCount || !deviceByIndex || nvmlInit() != kNvmlSuccess ||
            deviceCount(&count) != kNvmlSuccess || count == 0 || deviceByIndex(0, &gpu) != kNvmlSuccess) {
            gpu = nullptr;
            return false;
        }
        return true;
    }

    void ReadNvml(Readings& readings, bool wantFans) {
        unsigned value = 0;
        if (temperature && temperature(gpu, kNvmlTemperatureGpu, &value) == kNvmlSuccess) {
            readings.gpuTemp = value;
        }
        NvmlUtilization use{};
        if (utilization && utilization(gpu, &use) == kNvmlSuccess) {
            readings.gpuLoad = use.gpu;
        }
        unsigned fans = 0;
        if (!wantFans || !fanCount || fanCount(gpu, &fans) != kNvmlSuccess) {
            return;
        }
        for (unsigned i = 0; i < fans; ++i) {
            Fan fan;
            fan.gpu = true;
            fan.name = fans > 1 ? L"GPU Fan " + std::to_wstring(i + 1) : L"GPU Fan";
            if (fanSpeed && fanSpeed(gpu, i, &value) == kNvmlSuccess) {
                fan.percent = value;
            }
            NvmlFanSpeedInfo info{kNvmlFanSpeedInfoVersion, i, 0};
            if (fanSpeedRpm && fanSpeedRpm(gpu, &info) == kNvmlSuccess) {
                fan.rpm = info.speed;
            }
            if (fan.rpm || fan.percent) {
                readings.fans.push_back(std::move(fan));
            }
        }
    }

    void CloseHwinfo() {
        if (hwinfoView) {
            UnmapViewOfFile(hwinfoView);
            hwinfoView = nullptr;
        }
        if (hwinfoMapping) {
            CloseHandle(hwinfoMapping);
            hwinfoMapping = nullptr;
        }
    }

    bool ReadHwinfoMemory(std::vector<ToolReading>& out) {
        const ULONGLONG now = GetTickCount64();
        if (!hwinfoView) {
            if (now < hwinfoRetry) {
                return false;
            }
            hwinfoRetry = now + kRetryMs;
            hwinfoMapping = OpenFileMappingW(FILE_MAP_READ, FALSE, kHwinfoMapping);
            hwinfoView = hwinfoMapping
                             ? static_cast<const uint8_t*>(MapViewOfFile(hwinfoMapping, FILE_MAP_READ, 0, 0, 0))
                             : nullptr;
            if (!hwinfoView) {
                CloseHwinfo();
                return false;
            }
        }
        MEMORY_BASIC_INFORMATION region{};
        VirtualQuery(hwinfoView, &region, sizeof(region));
        const size_t mapped = region.RegionSize;
        const auto* header = reinterpret_cast<const HwinfoHeader*>(hwinfoView);
        const bool alive = mapped >= sizeof(HwinfoHeader) && (memcmp(header->signature, "HWiS", 4) == 0 ||
                                                              memcmp(header->signature, "SiWH", 4) == 0);
        if (!alive || header->readingSize < sizeof(HwinfoReading) || header->sensorSize < sizeof(HwinfoSensor) ||
            static_cast<ULONGLONG>(header->readingOffset) + static_cast<ULONGLONG>(header->readingSize) * header->readingCount > mapped ||
            static_cast<ULONGLONG>(header->sensorOffset) + static_cast<ULONGLONG>(header->sensorSize) * header->sensorCount > mapped) {
            // Shared Memory Support was switched off (the free version ends it after 12 hours): look again later.
            CloseHwinfo();
            hwinfoRetry = now + kRetryMs;
            return false;
        }
        const bool utf8 = header->readingSize >= sizeof(HwinfoReading) + kHwinfoText;
        for (DWORD i = 0; i < header->readingCount; ++i) {
            const auto* reading = reinterpret_cast<const HwinfoReading*>(hwinfoView + header->readingOffset +
                                                                          static_cast<size_t>(i) * header->readingSize);
            ToolReading item;
            if (reading->type == kHwinfoTemp) {
                item.kind = ToolReading::Kind::Temp;
            } else if (reading->type == kHwinfoFan) {
                item.kind = ToolReading::Kind::Fan;
            } else if ((reading->type == kHwinfoUsage || reading->type == kHwinfoOther) &&
                       strncmp(reading->unit, "%", 2) == 0) {
                item.kind = ToolReading::Kind::Percent;
            } else {
                continue;
            }
            item.name = utf8 ? FromCodePage(reinterpret_cast<const char*>(reading + 1), kHwinfoText, CP_UTF8)
                             : std::wstring{};
            if (item.name.empty()) {
                item.name = FromCodePage(reading->labelUser, kHwinfoText, CP_ACP);
            }
            if (reading->sensorIndex < header->sensorCount) {
                const auto* sensor = reinterpret_cast<const HwinfoSensor*>(
                    hwinfoView + header->sensorOffset + static_cast<size_t>(reading->sensorIndex) * header->sensorSize);
                item.device = FromCodePage(sensor->nameOriginal, kHwinfoText, CP_ACP);
            }
            item.value = reading->value;
            item.cpu = item.device.starts_with(L"CPU");
            item.gpu = item.device.starts_with(L"GPU");
            out.push_back(std::move(item));
        }
        toolName = L"HWiNFO (shared memory)";
        return true;
    }

    // HWiNFO's "Report value in Gadget": each chosen reading as SensorN, LabelN, ValueN and ValueRawN.
    bool ReadHwinfoGadget(std::vector<ToolReading>& out) {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kHwinfoGadgetKey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
            return false;
        }
        auto text = [&](const std::wstring& name) {
            wchar_t buffer[256] = {};
            DWORD size = sizeof(buffer) - sizeof(wchar_t);
            DWORD type = 0;
            if (RegQueryValueExW(key, name.c_str(), nullptr, &type, reinterpret_cast<BYTE*>(buffer), &size) !=
                    ERROR_SUCCESS ||
                type != REG_SZ) {
                return std::wstring{};
            }
            return std::wstring(buffer);
        };
        bool any = false;
        for (int i = 0;; ++i) {
            const std::wstring index = std::to_wstring(i);
            const std::wstring label = text(L"Label" + index);
            const std::wstring shown = text(L"Value" + index);
            if (label.empty() && shown.empty()) {
                break;
            }
            ToolReading item;
            if (shown.find(L"°") != std::wstring::npos) {
                item.kind = ToolReading::Kind::Temp;
            } else if (Contains(shown, L"RPM")) {
                item.kind = ToolReading::Kind::Fan;
            } else if (shown.find(L'%') != std::wstring::npos) {
                item.kind = ToolReading::Kind::Percent;
            } else {
                continue;
            }
            // ValueRaw holds the number with a dot; Value is formatted for the user's locale.
            std::wstring raw = text(L"ValueRaw" + index);
            std::replace(raw.begin(), raw.end(), L',', L'.');
            item.value = _wtof(raw.c_str());
            if (item.kind == ToolReading::Kind::Temp && Contains(shown, L"°F")) {
                item.value = (item.value - 32.0) * 5.0 / 9.0;
            }
            item.name = label;
            item.device = text(L"Sensor" + index);
            item.cpu = item.device.starts_with(L"CPU");
            item.gpu = item.device.starts_with(L"GPU");
            out.push_back(std::move(item));
            any = true;
        }
        RegCloseKey(key);
        if (any) {
            toolName = L"HWiNFO (gadget values)";
        }
        return any;
    }

    bool ConnectWmi() {
        const ULONGLONG now = GetTickCount64();
        if (wmi) {
            return true;
        }
        if (now < wmiRetry) {
            return false;
        }
        wmiRetry = now + kRetryMs;
        if (!comReady) {
            comReady = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
            CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE,
                                 nullptr, EOAC_NONE, nullptr);
        }
        IWbemLocator* locator = nullptr;
        if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator,
                                    reinterpret_cast<void**>(&locator)))) {
            return false;
        }
        for (const wchar_t* space : {L"ROOT\\LibreHardwareMonitor", L"ROOT\\OpenHardwareMonitor"}) {
            BSTR name = SysAllocString(space);
            const HRESULT result = locator->ConnectServer(name, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &wmi);
            SysFreeString(name);
            if (SUCCEEDED(result)) {
                CoSetProxyBlanket(wmi, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                                  RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
                wmiName = space + 5;
                break;
            }
            wmi = nullptr;
        }
        locator->Release();
        return wmi != nullptr;
    }

    bool ReadWmi(std::vector<ToolReading>& out) {
        if (!ConnectWmi()) {
            return false;
        }
        BSTR language = SysAllocString(L"WQL");
        BSTR query = SysAllocString(
            L"SELECT Name, Identifier, SensorType, Value, Parent FROM Sensor WHERE SensorType='Temperature' OR "
            L"SensorType='Fan' OR SensorType='Control' OR SensorType='Load'");
        IEnumWbemClassObject* rows = nullptr;
        const HRESULT result = wmi->ExecQuery(language, query, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                              nullptr, &rows);
        SysFreeString(language);
        SysFreeString(query);
        if (FAILED(result)) {
            // The tool closed; its namespace stays registered but answers no more.
            wmi->Release();
            wmi = nullptr;
            return false;
        }
        auto text = [](IWbemClassObject* row, const wchar_t* name) {
            VARIANT value;
            VariantInit(&value);
            std::wstring string;
            if (SUCCEEDED(row->Get(name, 0, &value, nullptr, nullptr)) && value.vt == VT_BSTR && value.bstrVal) {
                string = value.bstrVal;
            }
            VariantClear(&value);
            return string;
        };
        bool any = false;
        for (;;) {
            IWbemClassObject* row = nullptr;
            ULONG count = 0;
            if (rows->Next(1000, 1, &row, &count) != WBEM_S_NO_ERROR || count == 0) {
                break;
            }
            const std::wstring type = text(row, L"SensorType");
            const std::wstring id = text(row, L"Identifier");
            ToolReading item;
            item.kind = type == L"Temperature" ? ToolReading::Kind::Temp
                        : type == L"Fan"       ? ToolReading::Kind::Fan
                                               : ToolReading::Kind::Percent;
            item.name = text(row, L"Name");
            item.device = text(row, L"Parent");
            VARIANT value;
            VariantInit(&value);
            if (SUCCEEDED(row->Get(L"Value", 0, &value, nullptr, nullptr))) {
                item.value = value.vt == VT_R4 ? value.fltVal : value.vt == VT_R8 ? value.dblVal : 0.0;
            }
            VariantClear(&value);
            row->Release();
            item.cpu = item.device.starts_with(L"/amdcpu") || item.device.starts_with(L"/intelcpu");
            item.gpu = item.device.starts_with(L"/gpu");
            // A fan and its control: ".../fan/0" and ".../control/0".
            if (type == L"Fan" || type == L"Control") {
                std::wstring pair = id;
                const size_t at = pair.rfind(type == L"Fan" ? L"/fan/" : L"/control/");
                if (at != std::wstring::npos) {
                    pair = pair.substr(0, at) + L"/" + pair.substr(pair.find_last_of(L'/') + 1);
                }
                item.pairKey = pair;
            }
            if (type == L"Load" && !(item.gpu && item.name == L"GPU Core")) {
                continue;  // Only the GPU's load is wanted from here; Windows tells the CPU's.
            }
            if (type == L"Load") {
                item.name = L"GPU Load";
            }
            out.push_back(std::move(item));
            any = true;
        }
        rows->Release();
        if (any) {
            toolName = wmiName + L" (WMI)";
        }
        return any;
    }

    // Windows' own temperature and fan of the dedicated GPU, as Task Manager shows them: the adapter with the
    // highest temperature, as integrated GPUs report none.
    AdapterFigures ReadAdapterPerformance() {
        AdapterFigures best;
        D3DKMT_ENUMADAPTERS2 list{};
        if (D3DKMTEnumAdapters2(&list) != 0 || list.NumAdapters == 0) {
            return best;
        }
        std::vector<D3DKMT_ADAPTERINFO> adapters(list.NumAdapters);
        list.pAdapters = adapters.data();
        if (D3DKMTEnumAdapters2(&list) != 0) {
            return best;
        }
        for (ULONG i = 0; i < list.NumAdapters; ++i) {
            D3DKMT_ADAPTER_PERFDATA data{};
            D3DKMT_QUERYADAPTERINFO query{};
            query.hAdapter = adapters[i].hAdapter;
            query.Type = KMTQAITYPE_ADAPTERPERFDATA;
            query.pPrivateDriverData = &data;
            query.PrivateDriverDataSize = sizeof(data);
            if (D3DKMTQueryAdapterInfo(&query) == 0 && data.Temperature > best.deciCelsius) {
                best.deciCelsius = data.Temperature;
                best.luid = adapters[i].AdapterLuid;
                best.fanRpm = data.FanRPM;
                D3DKMT_ADAPTER_PERFDATACAPS caps{};
                query.Type = KMTQAITYPE_ADAPTERPERFDATA_CAPS;
                query.pPrivateDriverData = &caps;
                query.PrivateDriverDataSize = sizeof(caps);
                best.maxFanRpm = D3DKMTQueryAdapterInfo(&query) == 0 ? caps.MaxFanRPM : 0;
            }
            D3DKMT_CLOSEADAPTER close{adapters[i].hAdapter};
            D3DKMTCloseAdapter(&close);
        }
        return best;
    }

    // The GPU's load as Task Manager shows it: its busiest engine, each engine summed over the processes using it. Of
    // the adapter the temperature is from when there is one, so a processor's graphics do not stand in for the card.
    std::optional<double> WindowsGpuLoad(const LUID* luid) {
        if (!gpuQueryTried) {
            gpuQueryTried = true;
            if (PdhOpenQueryW(nullptr, 0, &gpuQuery) != ERROR_SUCCESS ||
                PdhAddEnglishCounterW(gpuQuery, L"\\GPU Engine(*)\\Utilization Percentage", 0, &gpuCounter) !=
                    ERROR_SUCCESS) {
                if (gpuQuery) {
                    PdhCloseQuery(gpuQuery);
                }
                gpuQuery = nullptr;
            }
            if (gpuQuery) {
                PdhCollectQueryData(gpuQuery);
            }
            return std::nullopt;
        }
        if (!gpuQuery || PdhCollectQueryData(gpuQuery) != ERROR_SUCCESS) {
            return std::nullopt;
        }
        DWORD size = 0;
        DWORD count = 0;
        PdhGetFormattedCounterArrayW(gpuCounter, PDH_FMT_DOUBLE, &size, &count, nullptr);
        std::vector<uint8_t> buffer(size);
        auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
        if (!size || PdhGetFormattedCounterArrayW(gpuCounter, PDH_FMT_DOUBLE, &size, &count, items) != ERROR_SUCCESS) {
            return std::nullopt;
        }
        // Instances are named pid_P_luid_A_B_phys_N_eng_E_engtype_T.
        std::map<std::wstring, double> perEngine;
        for (DWORD i = 0; i < count; ++i) {
            const std::wstring name = items[i].szName;
            const size_t from = name.find(L"luid_");
            const size_t type = name.find(L"_engtype_");
            if (from != std::wstring::npos && type != std::wstring::npos && items[i].FmtValue.CStatus == ERROR_SUCCESS) {
                perEngine[name.substr(from, type - from)] += items[i].FmtValue.doubleValue;
            }
        }
        wchar_t adapter[48] = {};
        if (luid) {
            swprintf_s(adapter, L"luid_0x%08lX_0x%08lX", static_cast<unsigned long>(luid->HighPart), luid->LowPart);
        }
        double best = 0.0;
        for (const auto& [engine, load] : perEngine) {
            if (!luid || _wcsnicmp(engine.c_str(), adapter, wcslen(adapter)) == 0) {
                best = std::max(best, load);
            }
        }
        return std::min(best, 100.0);
    }

    // Fans from the tool, with their duty where the tool pairs a control with them.
    static void AddToolFans(const std::vector<ToolReading>& tool, Readings& readings) {
        for (const ToolReading& reading : tool) {
            if (reading.kind != ToolReading::Kind::Fan) {
                continue;
            }
            Fan fan;
            fan.name = reading.name;
            fan.gpu = reading.gpu;
            fan.rpm = reading.value;
            if (!reading.pairKey.empty()) {
                for (const ToolReading& other : tool) {
                    if (other.kind == ToolReading::Kind::Percent && other.pairKey == reading.pairKey) {
                        fan.percent = other.value;
                    }
                }
            }
            readings.fans.push_back(std::move(fan));
        }
    }
};

Sensors::Sensors() : impl_(new Impl) {}

Sensors::~Sensors() {
    delete impl_;
}

Readings Sensors::Read(const Options& options) {
    Impl& impl = *impl_;
    Readings readings;
    const std::optional<double> load = impl.CpuLoad();
    if (options.cpuLoad) {
        readings.cpuLoad = load;
    }
    if (options.ram) {
        MEMORYSTATUSEX memory{sizeof(memory)};
        if (GlobalMemoryStatusEx(&memory) && memory.ullTotalPhys) {
            const double used = static_cast<double>(memory.ullTotalPhys - memory.ullAvailPhys);
            readings.ramUsedGb = used / (1024.0 * 1024.0 * 1024.0);
            readings.ramPercent = 100.0 * used / static_cast<double>(memory.ullTotalPhys);
        }
    }
    // The GPU from Windows first, as Task Manager shows it; NVML only where Windows tells nothing, as loading it
    // takes about 19 MB.
    const bool wantGpu = options.gpuTemp || options.gpuLoad;
    std::vector<Fan> gpuFans;
    if (wantGpu || options.fans) {
        const AdapterFigures adapter = impl.ReadAdapterPerformance();
        if (adapter.deciCelsius) {
            readings.gpuTemp = adapter.deciCelsius / 10.0;
        }
        if (adapter.maxFanRpm) {
            Fan fan;
            fan.gpu = true;
            fan.name = L"GPU Fan";
            fan.rpm = adapter.fanRpm;
            fan.percent = std::min(100.0, 100.0 * adapter.fanRpm / adapter.maxFanRpm);
            gpuFans.push_back(std::move(fan));
        }
        if (options.gpuLoad) {
            readings.gpuLoad = impl.WindowsGpuLoad(adapter.deciCelsius ? &adapter.luid : nullptr);
        }
        if ((!adapter.deciCelsius || (options.fans && !adapter.maxFanRpm)) && impl.LoadNvml()) {
            Readings nvidia;
            impl.ReadNvml(nvidia, options.fans);
            if (!readings.gpuTemp) {
                readings.gpuTemp = nvidia.gpuTemp;
            }
            if (!readings.gpuLoad && !impl.gpuQuery) {
                readings.gpuLoad = nvidia.gpuLoad;
            }
            if (gpuFans.empty()) {
                gpuFans = std::move(nvidia.fans);
            }
        }
    }
    std::vector<ToolReading> tool;
    if (options.cpuTemp || options.fans || (wantGpu && !readings.gpuTemp)) {
        impl.toolName.clear();
        if (!impl.ReadHwinfoMemory(tool) && !impl.ReadWmi(tool)) {
            impl.ReadHwinfoGadget(tool);
        }
    }
    if (options.cpuTemp) {
        int best = 0;
        for (const ToolReading& reading : tool) {
            if (reading.kind != ToolReading::Kind::Temp || reading.gpu || reading.value <= 0.0) {
                continue;
            }
            const int score = CpuTempScore(reading.name, reading.cpu);
            if (score > best) {
                best = score;
                readings.cpuTemp = reading.value;
            }
        }
        if (!readings.cpuTemp) {
            readings.cpuTemp = impl.pawnIo.CpuTemperature();
        }
    }
    for (const ToolReading& reading : tool) {
        if (!reading.gpu) {
            continue;
        }
        if (reading.kind == ToolReading::Kind::Temp && !readings.gpuTemp && Contains(reading.name, L"GPU")) {
            readings.gpuTemp = reading.value;
        } else if (reading.kind == ToolReading::Kind::Percent && !readings.gpuLoad &&
                   (Contains(reading.name, L"GPU Load") || Contains(reading.name, L"GPU Core Load") ||
                    Contains(reading.name, L"GPU Utilization"))) {
            readings.gpuLoad = reading.value;
        }
    }
    if (options.fans) {
        Impl::AddToolFans(tool, readings);
        if (std::none_of(readings.fans.begin(), readings.fans.end(), [](const Fan& fan) { return !fan.gpu; })) {
            for (Fan& fan : impl.pawnIo.BoardFans()) {
                readings.fans.push_back(std::move(fan));
            }
        }
        // Windows or NVML know the GPU's fan; the tool's own GPU fans give way to it.
        if (!gpuFans.empty()) {
            std::erase_if(readings.fans, [](const Fan& fan) { return fan.gpu; });
            readings.fans.insert(readings.fans.end(), gpuFans.begin(), gpuFans.end());
        }
    }
    if (!options.gpuTemp) {
        readings.gpuTemp.reset();
    }
    if (!options.gpuLoad) {
        readings.gpuLoad.reset();
    }
    return readings;
}

std::wstring Sensors::Describe() {
    Impl& impl = *impl_;
    Options all;
    Read(all);
    Sleep(1000);
    const Readings readings = Read(all);
    std::wostringstream text;
    auto line = [&](const wchar_t* name, const std::optional<double>& value) {
        text << name << L": ";
        if (value) {
            text << *value;
        } else {
            text << L"-";
        }
        text << L"\r\n";
    };
    text << L"NVML: " << (impl.gpu ? L"loaded" : L"not loaded") << L"\r\n";
    text << L"Sensor tool: " << (impl.toolName.empty() ? L"none" : impl.toolName) << L"\r\n";
    text << L"PawnIO: " << impl.pawnIo.Describe() << L"\r\n";
    line(L"CPU temperature", readings.cpuTemp);
    line(L"CPU load", readings.cpuLoad);
    line(L"GPU temperature", readings.gpuTemp);
    line(L"GPU load", readings.gpuLoad);
    line(L"RAM used GB", readings.ramUsedGb);
    line(L"RAM percent", readings.ramPercent);
    for (const Fan& fan : readings.fans) {
        text << L"Fan '" << fan.name << L"'" << (fan.gpu ? L" (GPU)" : L"") << L": ";
        text << (fan.rpm ? std::to_wstring(static_cast<int>(*fan.rpm)) + L" RPM" : L"- RPM") << L", ";
        text << (fan.percent ? std::to_wstring(static_cast<int>(*fan.percent)) + L" %" : L"- %") << L"\r\n";
    }
    std::vector<ToolReading> tool;
    if (impl.ReadHwinfoMemory(tool) || impl.ReadWmi(tool) || impl.ReadHwinfoGadget(tool)) {
        text << L"\r\nAll readings of " << impl.toolName << L":\r\n";
        for (const ToolReading& reading : tool) {
            const wchar_t* kind = reading.kind == ToolReading::Kind::Temp  ? L"temp"
                                  : reading.kind == ToolReading::Kind::Fan ? L"fan"
                                                                           : L"percent";
            text << L"  [" << kind << L"] " << reading.device << L" / " << reading.name << L" = " << reading.value
                 << L"\r\n";
        }
    }
    return text.str();
}
