#define SPDLOG_WCHAR_TO_UTF8_SUPPORT

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <windows.h>

#include <cstdint>
#include <cstddef>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <string_view>
#include <mutex>
#include <chrono>
#include <thread>
#include <MinHook.h>
#include <inipp.h>

#include "l4n_plugin.h"
#include "hook/Entry.h"
#include "vars.h"


// ---- Diagnostic logging helpers -------------------------------------------
// We use a dedicated logger that writes to an absolute path so the log is
// always findable, regardless of the working directory L4N chooses.
namespace {

// Absolute log path: <game root>\L4N-Necola-ADS-diag.log
// Game root = directory of the host process exe (left4dead2.exe).
std::string ResolveLogPath() {
    char exePath[MAX_PATH] = {0};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string p(exePath);
    size_t slash = p.find_last_of("\\/");
    std::string dir = (slash != std::string::npos) ? p.substr(0, slash) : ".";
    return dir + "\\L4N-Necola-ADS-diag.log";
}

// Trivially safe log that works even before spdlog is up: appends to a file
// using Win32 CreateFile (no CRT state, no exceptions). Flushes each line.
void RawLog(const char* msg) {
    static std::string path = ResolveLogPath();
    HANDLE h = CreateFileA(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    char line[2048];
    int n = _snprintf_s(line, sizeof(line), _TRUNCATE,
        "[%04u-%02u-%02u %02u:%02u:%02u.%03u] %s\r\n",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
        st.wMilliseconds, msg);
    if (n > 0) {
        DWORD written = 0;
        WriteFile(h, line, (DWORD)n, &written, nullptr);
    }
    CloseHandle(h);
}

void InitConsole() {
    static bool done = false;
    if (done) return;
    done = true;
    AllocConsole();
    FILE *dummy = nullptr;
    freopen_s(&dummy, "CONOUT$", "w", stdout);
    freopen_s(&dummy, "CONOUT$", "w", stderr);
}

void InitSpdlog() {
    try {
        auto logger = spdlog::basic_logger_mt("diag", ResolveLogPath(), false);
        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::trace);
        spdlog::flush_on(spdlog::level::trace);
    } catch (const spdlog::spdlog_ex&) {
        // spdlog failed — RawLog still works, console stays up.
    }
}

} // namespace


// ---- Original init pipeline ------------------------------------------------
static void LoadIniSafe() {
    try { LoadIni(); }
    catch (...) { RawLog("LoadIni threw exception"); }
}

DWORD __stdcall Hook_necola(LPVOID lpParam)
{
    RawLog("Hook_necola thread started");
    InitConsole();
    InitSpdlog();

    HANDLE hProcess = OpenProcess(SYNCHRONIZE, FALSE, GetCurrentProcessId());
    if (!hProcess) { RawLog("OpenProcess(SYNCHRONIZE) failed — continuing anyway"); }
    RawLog("calling LoadIni");
    LoadIniSafe();
    RawLog("LoadIni done");

    std::wstring cmdline = cfg::System::cmdLine;
    if (cmdline.empty()) {
        cmdline = GetCommandLineW() ? GetCommandLineW() : L"";
    }
    {
        char buf[2048];
        WideCharToMultiByte(CP_UTF8, 0, cmdline.c_str(), -1, buf, sizeof(buf), nullptr, nullptr);
        std::string m = "cmdline: " + std::string(buf);
        RawLog(m.c_str());
    }
    RawLog("calling CGlobal_ModuleEntry::Load");
    try {
        G::ModuleEntry.Load();
        RawLog("CGlobal_ModuleEntry::Load returned OK");
    } catch (...) {
        RawLog("!!! CGlobal_ModuleEntry::Load threw exception");
    }

    if (hProcess) CloseHandle(hProcess);
    RawLog("Hook_necola thread exit");
    return 0;
}

void Undo_necola()
{
    RawLog("Undo_necola (DLL detach)");
    try { G::ModuleEntry.undo(); }
    catch (...) { RawLog("ModuleEntry.undo threw"); }
}


// ---- L4N plugin instance ---------------------------------------------------
// Bypass plugin: L4N only loads us; core ADS logic is autonomous (own
// CreateInterface + MinHook). We kick off the init thread from the first
// L4N callback and self-gate on Source module readiness inside the thread.
class NecolaL4NPlugin : public IL4NPlugin {
public:
    unsigned int GetInterfaceVersion() override {
        RawLog("GetInterfaceVersion() called -> 1");
        return 1;
    }
    const char* GetName() override { return "Necola-ADS"; }
    const char* GetVersion() override { return sFixVer.c_str(); }

    // SHARED once_flag across both callbacks — otherwise OnGameLaunch and
    // OnModuleLoaded each get their OWN local static once_flag and spawn two
    // separate init threads, both running ModuleEntry.Load() concurrently
    // → duplicate MH_Initialize + duplicate hook installs → crash.
    static std::once_flag s_initOnce;

    void OnModuleLoaded(const char* module_name, std::uintptr_t handle) override {
        char buf[256];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE,
            "OnModuleLoaded(\"%s\", 0x%p)", module_name ? module_name : "(null)", (void*)handle);
        RawLog(buf);

        std::call_once(s_initOnce, []() {
            RawLog("spawning InitThreadFunc (from OnModuleLoaded)");
            if (auto h = CreateThread(NULL, 0, &NecolaL4NPlugin::InitThreadFunc, nullptr, 0, NULL)) {
                CloseHandle(h);
            } else {
                RawLog("CreateThread(InitThreadFunc) FAILED");
            }
        });
    }

    void OnGameLaunch() override {
        RawLog("OnGameLaunch() called");
        std::call_once(s_initOnce, []() {
            RawLog("spawning InitThreadFunc (from OnGameLaunch)");
            if (auto h = CreateThread(NULL, 0, &NecolaL4NPlugin::InitThreadFunc, nullptr, 0, NULL)) {
                CloseHandle(h);
            } else {
                RawLog("CreateThread(InitThreadFunc) FAILED");
            }
        });
    }

    static DWORD __stdcall InitThreadFunc(LPVOID) {
        RawLog("InitThreadFunc entered");
        const char* required[] = {
            "client.dll", "engine.dll", "vgui2.dll", "datacache.dll",
            "vguimatsurface.dll", "inputsystem.dll", "filesystem_stdio.dll",
        };
        constexpr int kN = sizeof(required) / sizeof(required[0]);

        for (int waited = 0; waited < 120; ++waited) {
            bool all = true;
            char missing[256] = {0};
            int off = 0;
            for (int i = 0; i < kN; ++i) {
                if (!GetModuleHandleA(required[i])) {
                    all = false;
                    off += _snprintf_s(missing + off, sizeof(missing) - off, _TRUNCATE,
                        "%s%s", (off ? "," : ""), required[i]);
                    if (off >= (int)sizeof(missing) - 8) break;
                }
            }
            if (all) {
                RawLog("all required modules present");
                break;
            }
            if (waited % 10 == 0) {
                char m[300];
                _snprintf_s(m, sizeof(m), _TRUNCATE, "waiting (%ds) missing: %s", waited, missing);
                RawLog(m);
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        RawLog("module wait done, calling Hook_necola");
        DWORD r = Hook_necola(nullptr);
        RawLog("Hook_necola returned");
        return r;
    }

    void OnD3DCreated(void* d3d) override {}
    void OnD3DDeviceCreated(void* d3d_device, bool is_dxvk) override {}
};

// Definition of the shared once_flag — guarantees only ONE init thread is
// ever spawned, regardless of which L4N callback fires first or how many
// times. Without this, OnGameLaunch + OnModuleLoaded each create their own
// local once_flag and start separate threads → duplicate ModuleEntry.Load().
std::once_flag NecolaL4NPlugin::s_initOnce;


extern "C" __declspec(dllexport) IL4NPlugin* GetL4NPluginInstance() {
    RawLog("GetL4NPluginInstance() called");
    static NecolaL4NPlugin instance;
    RawLog("GetL4NPluginInstance() returning instance");
    return &instance;
}


BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH: {
            DisableThreadLibraryCalls(hModule);
            char msg[128];
            _snprintf_s(msg, sizeof(msg), _TRUNCATE,
                "DllMain(DLL_PROCESS_ATTACH), hModule=0x%p", (void*)hModule);
            RawLog(msg);
            break;
        }
        case DLL_PROCESS_DETACH:
            RawLog("DllMain(DLL_PROCESS_DETACH)");
            Undo_necola();
            break;
    }
    return TRUE;
}
