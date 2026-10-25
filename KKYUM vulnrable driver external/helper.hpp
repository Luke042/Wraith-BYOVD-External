#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <TlHelp32.h>

#undef max
#undef min

#ifdef _WIN32
#include <windows.h>
#endif

#undef interface

#include "vendor/medius/include/medius.h"

namespace ioctl_codes
{
    const int mem_read = 0x22265C;
    const int mem_write = 0x222658;
}

struct MemoryOperation
{
    std::uint64_t source;
    std::uint64_t destination;
    std::uint64_t size;
};

struct MemoryRequest
{
    std::uint32_t pid;
    std::uint32_t operationCount;
    MemoryOperation operations[1];
};

static void enableAnsi() {
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode))
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
}

void ChangeTitle()
{
    
}

struct RGB { int r, g, b; };

static RGB lerp(RGB a, RGB b, double t) {
    return
    {
        int(a.r + (b.r - a.r) * t + 0.5),
        int(a.g + (b.g - a.g) * t + 0.5),
        int(a.b + (b.b - a.b) * t + 0.5)
    };
}

namespace mem
{
    void* g_device{};
    MediusDevice* medius_device;
    bool medius = false;
    uint32_t g_pid{};
    uint64_t g_base_address = 0x400000; //This is really bad practise as only 32-bit processes have constant base addresses, as this driver doesnt have a "get base address" function this will have to do.

    template <typename T>
    T read(std::uintptr_t address)
    {
        static_assert(std::is_trivially_copyable_v<T>);

        T value{};

        MemoryRequest request{};
        request.pid = g_pid;
        request.operationCount = 1;
        request.operations[0].source = address;
        request.operations[0].destination = reinterpret_cast<std::uint64_t>(&value);
        request.operations[0].size = sizeof(T);
        DWORD bytes_returned = 0;

        if (!DeviceIoControl(g_device, ioctl_codes::mem_read, &request, sizeof(request), nullptr, 0, &bytes_returned, nullptr))
        {
            std::println("Failed read at {:#x}, error {}", address, GetLastError());
            return T{};
        }

        return value;
    }

    template <typename T>
    bool write(std::uintptr_t addr, T value)
    {
        MemoryRequest request{};
        request.pid = mem::g_pid;
        request.operationCount = 1;
        request.operations[0].source = addr;
        request.operations[0].destination = reinterpret_cast<std::uint64_t>(&value);
        request.operations[0].size = sizeof(T);
        DWORD bytes_returned = 0;

        return DeviceIoControl(mem::g_device, ioctl_codes::mem_write, &request, sizeof(request), nullptr, 0, &bytes_returned, nullptr);
    }

    std::uint32_t GetPid(const std::wstring& process_name) {
        std::uint32_t pid = 0;

        void* hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnapshot == INVALID_HANDLE_VALUE) {
            return 0;
        }

        PROCESSENTRY32W pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32W);

        if (Process32FirstW(hSnapshot, &pe32)) {
            do {
                if (process_name == pe32.szExeFile) {
                    pid = pe32.th32ProcessID;
                    break;
                }
            } while (Process32NextW(hSnapshot, &pe32));
        }

        CloseHandle(hSnapshot);
        return pid;
    }
}

namespace logo
{
    const char* wraith_logo = R"ART(
                                ++
                               +-+
                             +++
                           ++++
+++++           +        ++++  +++++++++++++++       +++++++++      +++++++++++++++++++++++++       ++++++
  +++++        +++      ++++  +++++++++++++++++      ++++++++        +++++++++++++++++++           +++++
   ++++       ++++     ++++               ++++     ++++++++++                 ++++      +++       ++++
    ++++    +++++++   ++++               ++++     +++++  +++++      + +++    ++++      ++++       +++
     +++++ ++++++++  ++++    ++++++++++++++++   +++++     ++++     +++++    +++++     ++++++++++++++
      ++++++++++++++++++    ++++++++++++++++   ++++        ++++    ++++    ++++       ++++++++++++++
       +++++++  +++++++    +++++  +++++       ++++    ++++++++++  +-+++   +++++      ++++      ++++
       ++++++    +++++    ++++     +++++     ++++   +++++++++++   ++++    ++++      ++++       ++++
        ++++     ++++     +++++     +++++   ++++            +++++ ++++    +++       ++++     +++++
         ++        +                 +++++  ++              +++++ +++    +++       ++        +++
                                      +++++
                                       ++++
                                         +++
                                           ++
                                             +
)ART";

    void Render()
    {
        std::vector<std::string> lines;
        std::istringstream in(wraith_logo + 1);
        for (std::string line; std::getline(in, line); )
            lines.push_back(line);

        size_t width = 0;
        for (auto& l : lines) width = std::max(width, l.size());

        const RGB left = { 150, 20, 210 };
        const RGB right = { 60, 30, 225 };

        for (const auto& line : lines) {
            for (size_t x = 0; x < line.size(); ++x) {
                if (line[x] == ' ') { std::cout << ' '; continue; }
                RGB c = lerp(left, right, double(x) / (width - 1));
                std::cout << "\x1b[38;2;" << c.r << ';' << c.g << ';' << c.b << 'm'
                    << line[x];
            }
            std::cout << "\x1b[0m\n";
        }
        std::cout << "\n\n";
    }
        
}

namespace offsets
{
    std::uint64_t local_player = 0x0017E0A8;
    std::uint64_t entity_list = 0x18AC04;
    std::uint64_t fov = 0x18A7CC;
    std::uint64_t player_count = 0x18AC0C;

    std::uint64_t head_x = 0x4;
    std::uint64_t head_y = 0x8;
    std::uint64_t head_z = 0xC;

    std::uint64_t yaw = 0x34;
    std::uint64_t pitch = 0x38;

    std::uint64_t health = 0xEC;
}

namespace settings
{
    bool memory_aim = false;
    bool makcu_aim = false;
    bool two_pc_makcu_aim = false;
}

constexpr float dpp = 0.09091f;
constexpr float gain = 0.9f;
constexpr float max_deg_per_s = 500.f;
constexpr float deadzone = 0.15f;
constexpr float interval_s = 0.008f;