// The CPU temperature and the motherboard's fans through PawnIO (pawnio.eu), when it is installed and the add-on runs
// as administrator, which PawnIO requires. Its modules come signed from PawnIO.Modules (LGPL-2.1) and are embedded as
// resources: AMDFamily17 reads a Ryzen's Tctl over SMN, IntelMSR the package's thermal status, LpcIO the Super I/O
// chip whose hardware monitor counts the fans. The registers are the ones AMD, Intel and Nuvoton document, as the
// Linux k10temp, coretemp and nct6775 drivers use them. Which header of the board each fan channel serves comes from
// the board's own wiring; the boards known here follow LibreHardwareMonitor's motherboard tables.

#include "infoscreen.h"

#include <intrin.h>

#include <algorithm>
#include <cstring>
#include <iterator>

namespace {

constexpr wchar_t kLibrary[] = L"\\PawnIO\\PawnIOLib.dll";  // Under Program Files, where its installer puts it.
constexpr ULONGLONG kRetryMs = 30000;
constexpr DWORD kLockWaitMs = 100;
// Mutexes other monitoring tools take around the same hardware.
constexpr wchar_t kPciMutex[] = L"Global\\Access_PCI";
constexpr wchar_t kIsaMutex[] = L"Global\\Access_ISABUS.HTP.Method";

constexpr ULONG64 kSmnTemperature = 0x00059800;  // THM_TCON_CUR_TMP: degrees in bits 31-21 by 1/8, bit 19 -49.
constexpr ULONG64 kMsrTemperatureTarget = 0x1A2;
constexpr ULONG64 kMsrPackageThermStatus = 0x1B1;

constexpr ULONG64 kSuperIoPorts[] = {0x2E, 0x4E};
constexpr ULONG64 kNuvotonEnter = 0x87;
constexpr ULONG64 kSuperIoExit = 0xAA;
constexpr ULONG64 kChipIdRegister = 0x20;
constexpr ULONG64 kDeviceSelectRegister = 0x07;
constexpr ULONG64 kHardwareMonitorDevice = 0x0B;
constexpr ULONG64 kBaseAddressRegister = 0x60;
constexpr ULONG64 kAddressPort = 5;  // The hardware monitor's index and data ports, from its base.
constexpr ULONG64 kDataPort = 6;
constexpr unsigned kBankRegister = 0x4E;
constexpr unsigned kVendorRegister = 0x4F;  // 0x5CA3 for Nuvoton: the high byte in bank 0x80, the low in bank 0.
constexpr unsigned kNuvotonVendor = 0x5CA3;
// NCT6779D to NCT6799D and NCT6701D by their chip ID's upper 12 bits: these count each fan's RPM in a word of bank 4.
constexpr unsigned kNuvotonChips[] = {0xC560, 0xC800, 0xC910, 0xD120, 0xD350, 0xD420, 0xD450, 0xD800};
constexpr unsigned kFanRegisters[] = {0x4C0, 0x4C2, 0x4C4, 0x4C6, 0x4C8, 0x4CA, 0x4CE};
constexpr unsigned kDutyRegisters[] = {0x001, 0x003, 0x011, 0x013, 0x015, 0xA09, 0xB09};  // 0-255.

// The headers a board wires to the chip's seven fan channels, and their labels on the screen.
struct BoardHeaders {
    const wchar_t* product;  // SMBIOS baseboard product.
    const wchar_t* names[std::size(kFanRegisters)];
    const char* tags[std::size(kFanRegisters)];
};

constexpr BoardHeaders kBoards[] = {
    {L"ROG STRIX B850-E GAMING WIFI",
     {L"Chassis Fan 1", L"CPU Fan", L"Chassis Fan 2", L"Chassis Fan 3", L"Chassis Fan 4", L"Chassis Fan 5", L"AIO Pump"},
     {"1", "CPU", "2", "3", "4", "5", "PUMP"}},
};

// The board's entry, by the baseboard product Windows read from its SMBIOS; null for boards not known here.
const BoardHeaders* FindBoard() {
    wchar_t product[256] = {};
    DWORD size = sizeof(product) - sizeof(wchar_t);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"BaseBoardProduct", RRF_RT_REG_SZ,
                     nullptr, product, &size) != ERROR_SUCCESS) {
        return nullptr;
    }
    for (const BoardHeaders& board : kBoards) {
        if (_wcsicmp(product, board.product) == 0) {
            return &board;
        }
    }
    return nullptr;
}

using OpenFunction = HRESULT(WINAPI*)(PHANDLE);
using LoadFunction = HRESULT(WINAPI*)(HANDLE, const UCHAR*, SIZE_T);
using ExecuteFunction = HRESULT(WINAPI*)(HANDLE, PCSTR, const ULONG64*, SIZE_T, PULONG64, SIZE_T, PSIZE_T);
using CloseFunction = HRESULT(WINAPI*)(HANDLE);

enum class Processor { Unknown, Amd, Intel };

Processor ProcessorKind() {
    int registers[4] = {};
    __cpuid(registers, 0);
    char vendor[13] = {};
    memcpy(vendor, &registers[1], 4);
    memcpy(vendor + 4, &registers[3], 4);
    memcpy(vendor + 8, &registers[2], 4);
    __cpuid(registers, 1);
    const int base = (registers[0] >> 8) & 0xF;
    const int family = base == 0xF ? base + ((registers[0] >> 20) & 0xFF) : base;
    if (strcmp(vendor, "AuthenticAMD") == 0 && family >= 0x17 && family <= 0x1A) {
        return Processor::Amd;
    }
    return strcmp(vendor, "GenuineIntel") == 0 ? Processor::Intel : Processor::Unknown;
}

// Holds a mutex other tools share, for a moment at most.
class Lock {
public:
    explicit Lock(HANDLE mutex) : mutex_(mutex) {
        const DWORD result = mutex ? WaitForSingleObject(mutex, kLockWaitMs) : WAIT_FAILED;
        held_ = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
    }
    ~Lock() {
        if (held_) {
            ReleaseMutex(mutex_);
        }
    }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
    bool Held() const { return held_; }

private:
    HANDLE mutex_;
    bool held_ = false;
};

}  // namespace

struct PawnIo::Impl {
    HMODULE library = nullptr;
    OpenFunction open = nullptr;
    LoadFunction load = nullptr;
    ExecuteFunction execute = nullptr;
    CloseFunction close = nullptr;
    HANDLE processor = nullptr;  // AMDFamily17 or IntelMSR, loaded.
    HANDLE superIo = nullptr;    // LpcIO, with the hardware monitor found.
    Processor kind = ProcessorKind();
    ULONG64 monitor = 0;         // The hardware monitor's base port.
    unsigned chip = 0;
    const BoardHeaders* board = FindBoard();
    HANDLE pciMutex = CreateMutexW(nullptr, FALSE, kPciMutex);
    HANDLE isaMutex = CreateMutexW(nullptr, FALSE, kIsaMutex);
    ULONGLONG processorRetry = 0;
    ULONGLONG superIoRetry = 0;
    std::wstring status = L"not tried";

    ~Impl() {
        for (HANDLE handle : {processor, superIo}) {
            if (handle && close) {
                close(handle);
            }
        }
        for (HANDLE mutex : {pciMutex, isaMutex}) {
            if (mutex) {
                CloseHandle(mutex);
            }
        }
    }

    bool OpenLibrary() {
        if (library) {
            return true;
        }
        wchar_t folder[MAX_PATH] = {};
        if (!ExpandEnvironmentStringsW(L"%ProgramFiles%", folder, MAX_PATH)) {
            return false;
        }
        library = LoadLibraryExW((std::wstring(folder) + kLibrary).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!library) {
            status = L"not installed";
            return false;
        }
        open = reinterpret_cast<OpenFunction>(GetProcAddress(library, "pawnio_open"));
        load = reinterpret_cast<LoadFunction>(GetProcAddress(library, "pawnio_load"));
        execute = reinterpret_cast<ExecuteFunction>(GetProcAddress(library, "pawnio_execute"));
        close = reinterpret_cast<CloseFunction>(GetProcAddress(library, "pawnio_close"));
        if (!open || !load || !execute || !close) {
            status = L"unknown PawnIOLib";
            FreeLibrary(library);
            library = nullptr;
            return false;
        }
        return true;
    }

    // A new executor with the embedded module of that resource name.
    HANDLE LoadModule(const wchar_t* resource) {
        if (!OpenLibrary()) {
            return nullptr;
        }
        HRSRC found = FindResourceW(nullptr, resource, RT_RCDATA);
        HGLOBAL loaded = found ? LoadResource(nullptr, found) : nullptr;
        const auto* bytes = loaded ? static_cast<const UCHAR*>(LockResource(loaded)) : nullptr;
        if (!bytes) {
            status = L"module missing";
            return nullptr;
        }
        HANDLE handle = nullptr;
        HRESULT result = open(&handle);
        if (FAILED(result)) {
            status = result == E_ACCESSDENIED ? L"needs administrator rights" : L"cannot be opened";
            return nullptr;
        }
        result = load(handle, bytes, SizeofResource(nullptr, found));
        if (FAILED(result)) {
            status = L"module not loaded";
            close(handle);
            return nullptr;
        }
        status = L"open";
        return handle;
    }

    bool Call(HANDLE handle, const char* function, std::initializer_list<ULONG64> in, ULONG64* out = nullptr) {
        ULONG64 result = 0;
        SIZE_T returned = 0;
        if (FAILED(execute(handle, function, in.begin(), in.size(), &result, out ? 1 : 0, &returned))) {
            return false;
        }
        if (out) {
            *out = result;
        }
        return true;
    }

    // A register of the hardware monitor; the bank is selected only when it changes.
    bool ReadMonitor(unsigned reg, unsigned& bank, ULONG64& value) {
        if (reg >> 8 != bank) {
            if (!Call(superIo, "ioctl_pio_outb", {monitor + kAddressPort, kBankRegister}) ||
                !Call(superIo, "ioctl_pio_outb", {monitor + kDataPort, reg >> 8})) {
                return false;
            }
            bank = reg >> 8;
        }
        return Call(superIo, "ioctl_pio_outb", {monitor + kAddressPort, reg & 0xFF}) &&
               Call(superIo, "ioctl_pio_inb", {monitor + kDataPort}, &value);
    }

    // Finds a Nuvoton chip's hardware monitor through the Super I/O configuration ports.
    bool FindMonitor() {
        superIo = LoadModule(L"PAWNIO_LPCIO");
        if (!superIo) {
            return false;
        }
        const Lock lock(isaMutex);
        if (!lock.Held()) {
            return false;
        }
        for (size_t slot = 0; slot < std::size(kSuperIoPorts) && !monitor; ++slot) {
            const ULONG64 port = kSuperIoPorts[slot];
            ULONG64 id = 0;
            ULONG64 base = 0;
            if (!Call(superIo, "ioctl_select_slot", {slot}) || !Call(superIo, "ioctl_pio_outb", {port, kNuvotonEnter}) ||
                !Call(superIo, "ioctl_pio_outb", {port, kNuvotonEnter}) ||
                !Call(superIo, "ioctl_superio_inw", {kChipIdRegister}, &id)) {
                continue;
            }
            const bool nuvoton = std::find(std::begin(kNuvotonChips), std::end(kNuvotonChips),
                                           static_cast<unsigned>(id & 0xFFF0)) != std::end(kNuvotonChips);
            if (nuvoton && Call(superIo, "ioctl_superio_outb", {kDeviceSelectRegister, kHardwareMonitorDevice}) &&
                Call(superIo, "ioctl_superio_inw", {kBaseAddressRegister}, &base) && base &&
                Call(superIo, "ioctl_find_bars", {})) {
                monitor = base & 0xFFF8;
                chip = static_cast<unsigned>(id);
            }
            Call(superIo, "ioctl_pio_outb", {port, kSuperIoExit});
        }
        // The vendor ID confirms the chip before any fan is read.
        unsigned bank = ~0u;
        ULONG64 high = 0;
        ULONG64 low = 0;
        if (monitor && !(ReadMonitor(0x8000 | kVendorRegister, bank, high) && ReadMonitor(kVendorRegister, bank, low) &&
                         ((high << 8) | low) == kNuvotonVendor)) {
            monitor = 0;
        }
        if (monitor && bank != 0) {
            ULONG64 ignored = 0;
            ReadMonitor(0, bank, ignored);
        }
        if (!monitor) {
            status = L"no Nuvoton chip found";
            close(superIo);
            superIo = nullptr;
        }
        return monitor != 0;
    }
};

PawnIo::PawnIo() : impl_(new Impl) {}

PawnIo::~PawnIo() {
    delete impl_;
}

std::optional<double> PawnIo::CpuTemperature() {
    Impl& impl = *impl_;
    if (impl.kind == Processor::Unknown) {
        return std::nullopt;
    }
    const ULONGLONG now = GetTickCount64();
    if (!impl.processor) {
        if (now < impl.processorRetry) {
            return std::nullopt;
        }
        impl.processorRetry = now + kRetryMs;
        impl.processor = impl.LoadModule(impl.kind == Processor::Amd ? L"PAWNIO_AMDFAMILY17" : L"PAWNIO_INTELMSR");
        if (!impl.processor) {
            return std::nullopt;
        }
    }
    ULONG64 value = 0;
    if (impl.kind == Processor::Amd) {
        const Lock lock(impl.pciMutex);
        if (!lock.Held() || !impl.Call(impl.processor, "ioctl_read_smn", {kSmnTemperature}, &value)) {
            return std::nullopt;
        }
        double celsius = ((value >> 21) & 0x7FF) / 8.0;
        if (value & (1u << 19)) {
            celsius -= 49.0;
        }
        return celsius;
    }
    ULONG64 target = 0;
    if (!impl.Call(impl.processor, "ioctl_read_msr", {kMsrTemperatureTarget}, &target) ||
        !impl.Call(impl.processor, "ioctl_read_msr", {kMsrPackageThermStatus}, &value) || !target) {
        return std::nullopt;
    }
    return static_cast<double>(((target >> 16) & 0xFF) - ((value >> 16) & 0x7F));
}

std::vector<Fan> PawnIo::BoardFans() {
    Impl& impl = *impl_;
    std::vector<Fan> fans;
    const ULONGLONG now = GetTickCount64();
    if (!impl.monitor) {
        if (now < impl.superIoRetry) {
            return fans;
        }
        impl.superIoRetry = now + kRetryMs;
        if (!impl.FindMonitor()) {
            return fans;
        }
    }
    const Lock lock(impl.isaMutex);
    if (!lock.Held()) {
        return fans;
    }
    unsigned bank = ~0u;
    for (size_t i = 0; i < std::size(kFanRegisters); ++i) {
        ULONG64 high = 0;
        ULONG64 low = 0;
        ULONG64 duty = 0;
        if (!impl.ReadMonitor(kFanRegisters[i], bank, high) || !impl.ReadMonitor(kFanRegisters[i] + 1, bank, low) ||
            !impl.ReadMonitor(kDutyRegisters[i], bank, duty)) {
            break;
        }
        Fan fan;
        if (impl.board) {
            fan.name = impl.board->names[i];
            fan.tag = impl.board->tags[i];
        } else {
            fan.name = L"Fan " + std::to_wstring(i + 1);
        }
        fan.rpm = static_cast<double>((high << 8) | low);
        fan.percent = duty * 100.0 / 255.0;
        fans.push_back(std::move(fan));
    }
    // Bank 0 again, where other tools expect it.
    ULONG64 ignored = 0;
    impl.ReadMonitor(0, bank, ignored);
    // The processor's fan first.
    std::stable_partition(fans.begin(), fans.end(), [](const Fan& fan) { return fan.tag == "CPU"; });
    return fans;
}

std::wstring PawnIo::Describe() const {
    const Impl& impl = *impl_;
    std::wstring text = impl.status;
    if (impl.monitor) {
        wchar_t chip[64];
        swprintf_s(chip, L", fans from Nuvoton chip %04X at port %llX", impl.chip, impl.monitor);
        text += chip;
        text += impl.board ? std::wstring(L", headers of the ") + impl.board->product : L", headers unknown";
    }
    return text;
}
