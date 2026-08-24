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
#include <atomic>
#include <chrono>
#include <thread>
#include <MinHook.h>
#include <inipp.h>

#include "l4n_plugin.h"
#include "sdk/L4NEnv.h"
#include "hook/Entry.h"
#include "vars.h"
#include "diag.h"


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

// Verbose debugging (console window + per-module/per-callback logs) is opt-in
// via the NECOLA_ADS_DEBUG environment variable, so a release install stays
// silent. Critical milestones and failures are always written to the log.
bool DebugEnabled() {
    static const bool on = [] {
        char buf[8];
        return GetEnvironmentVariableA("NECOLA_ADS_DEBUG", buf, sizeof(buf)) > 0;
    }();
    return on;
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

PVOID g_exceptionHandler = nullptr;
std::atomic_flag g_loggingException = ATOMIC_FLAG_INIT;

LONG CALLBACK LogVectoredException(EXCEPTION_POINTERS* pointers) {
    if (!pointers || !pointers->ExceptionRecord || !pointers->ContextRecord) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const DWORD code = pointers->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_INT_DIVIDE_BY_ZERO &&
        code != EXCEPTION_FLT_DIVIDE_BY_ZERO) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (g_loggingException.test_and_set()) return EXCEPTION_CONTINUE_SEARCH;

    const void* address = pointers->ExceptionRecord->ExceptionAddress;
    HMODULE module = nullptr;
    char modulePath[MAX_PATH] = "unknown";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(address), &module) && module) {
        GetModuleFileNameA(module, modulePath, MAX_PATH);
    }
    char message[1024] = {};
#if defined(_M_IX86)
    _snprintf_s(message, sizeof(message), _TRUNCATE,
        "!!! VEH code=0x%08lX address=%p module=%s base=%p offset=0x%08llX EIP=%08lX ESP=%08lX EBP=%08lX",
        code, address, modulePath, module,
        module ? static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(address) -
            reinterpret_cast<std::uintptr_t>(module)) : 0ull,
        pointers->ContextRecord->Eip, pointers->ContextRecord->Esp, pointers->ContextRecord->Ebp);
#else
    _snprintf_s(message, sizeof(message), _TRUNCATE,
        "!!! VEH code=0x%08lX address=%p module=%s base=%p", code, address, modulePath, module);
#endif
    RawLog(message);
    g_loggingException.clear();
    return EXCEPTION_CONTINUE_SEARCH;
}

// Debug-only log: call sites that would spam every launch (per-module
// callbacks, cmdline dumps, interface version probes).
void DbgLog(const char* msg) {
    if (!DebugEnabled()) return;
    RawLog(msg);
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

void NecolaDiagLog(const char* message) {
    if (message) RawLog(message);
}


// ---- Original init pipeline ------------------------------------------------
static void LoadIniSafe() {
    try { LoadIni(); }
    catch (...) { RawLog("LoadIni threw exception"); }
}

DWORD __stdcall Hook_necola(LPVOID lpParam)
{
    RawLog("Hook_necola thread started");
    if (!g_exceptionHandler) {
        g_exceptionHandler = AddVectoredExceptionHandler(1, LogVectoredException);
        RawLog(g_exceptionHandler ? "vectored exception diagnostics installed" :
            "WARN: AddVectoredExceptionHandler failed");
    }
    // Console window + spdlog are debug-only: a release L4N plugin must not
    // pop a console at every game launch. RawLog/ELog still cover the file.
    if (DebugEnabled()) {
        InitConsole();
        InitSpdlog();
    }

    HANDLE hProcess = OpenProcess(SYNCHRONIZE, FALSE, GetCurrentProcessId());
    if (!hProcess) { RawLog("OpenProcess(SYNCHRONIZE) failed — continuing anyway"); }
    RawLog("calling LoadIni");
    LoadIniSafe();
    RawLog("LoadIni done");

    if (DebugEnabled()) {
        std::wstring cmdline = cfg::System::cmdLine;
        if (cmdline.empty()) {
            cmdline = GetCommandLineW() ? GetCommandLineW() : L"";
        }
        char buf[2048];
        WideCharToMultiByte(CP_UTF8, 0, cmdline.c_str(), -1, buf, sizeof(buf), nullptr, nullptr);
        std::string m = "cmdline: " + std::string(buf);
        DbgLog(m.c_str());
    }
    RawLog("calling CGlobal_ModuleEntry::Load");
    try {
        if (G::ModuleEntry.Load()) {
            RawLog("CGlobal_ModuleEntry::Load returned OK");
        } else {
            RawLog("CGlobal_ModuleEntry::Load FAILED; plugin remains inactive");
        }
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
    if (g_exceptionHandler) {
        RemoveVectoredExceptionHandler(g_exceptionHandler);
        g_exceptionHandler = nullptr;
    }
}


// ---- L4N plugin instance ---------------------------------------------------
// Bypass plugin: L4N only loads us; core ADS logic is autonomous (own
// CreateInterface + MinHook). We kick off the init thread from the first
// L4N callback and self-gate on Source module readiness inside the thread.
class NecolaL4NPlugin : public IL4NPlugin {
public:
    unsigned int GetInterfaceVersion() override {
        DbgLog("GetInterfaceVersion() called -> 1");
        return 1;
    }
    const char* GetName() override { return "Necola-ADS"; }
    const char* GetVersion() override { return sFixVer.c_str(); }

    static std::atomic_bool s_initStarted;

    static void TryStartInit(const char* source) {
        bool expected = false;
        if (!s_initStarted.compare_exchange_strong(expected, true)) return;

        char msg[128];
        _snprintf_s(msg, sizeof(msg), _TRUNCATE,
            "spawning InitThreadFunc (from %s)", source);
        RawLog(msg);

        if (auto h = CreateThread(nullptr, 0, &NecolaL4NPlugin::InitThreadFunc, nullptr, 0, nullptr)) {
            CloseHandle(h);
        } else {
            RawLog("CreateThread(InitThreadFunc) FAILED; a later callback may retry");
            s_initStarted.store(false);
        }
    }

    void OnModuleLoaded(const char* module_name, std::uintptr_t handle) override {
        // Official pattern (l4n_plugin.h sample): `handle` is the
        // LoadLibrary HMODULE, converted via std::bit_cast<HMODULE>; the
        // name arrives WITHOUT the ".dll" suffix. L4N::Env normalizes and
        // stores it so the rest of the plugin can resolve modules through
        // the platform-reported handles instead of re-guessing.
        L4N::Env.OnModuleLoaded(module_name, handle);

        if (DebugEnabled()) {
            char buf[256];
            _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                "OnModuleLoaded(\"%s\", 0x%p)", module_name ? module_name : "(null)", (void*)handle);
            DbgLog(buf);
        }

        TryStartInit("OnModuleLoaded");
    }

    void OnGameLaunch() override {
        DbgLog("OnGameLaunch() called");
        TryStartInit("OnGameLaunch");
    }

    static DWORD __stdcall InitThreadFunc(LPVOID) {
        DbgLog("InitThreadFunc entered");
        const char* required[] = {
            "client.dll", "engine.dll", "vgui2.dll", "datacache.dll",
            "vguimatsurface.dll", "inputsystem.dll", "filesystem_stdio.dll",
            "vstdlib.dll", "materialsystem.dll",
        };
        constexpr int kN = sizeof(required) / sizeof(required[0]);
        bool allReady = false;

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
                allReady = true;
                break;
            }
            if (waited % 10 == 0) {
                char m[300];
                _snprintf_s(m, sizeof(m), _TRUNCATE, "waiting (%ds) missing: %s", waited, missing);
                RawLog(m);
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        if (!allReady) {
            RawLog("module wait timed out; initialization aborted");
            return ERROR_TIMEOUT;
        }
        RawLog("module wait done, calling Hook_necola");
        DWORD r = Hook_necola(nullptr);
        RawLog("Hook_necola returned");
        return r;
    }

    void OnD3DCreated(void* d3d) override {}
    void OnD3DDeviceCreated(void* d3d_device, bool is_dxvk) override {}
};

std::atomic_bool NecolaL4NPlugin::s_initStarted{false};


extern "C" __declspec(dllexport) IL4NPlugin* GetL4NPluginInstance() {
    DbgLog("GetL4NPluginInstance() called");
    static NecolaL4NPlugin instance;
    DbgLog("GetL4NPluginInstance() returning instance");
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
