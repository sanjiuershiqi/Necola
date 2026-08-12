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


void InitConsole() {
    AllocConsole();
    FILE *dummy;
    freopen_s(&dummy, "CONOUT$", "w", stdout);
}

void Logging() {
    if (cfg::System::debug) {
        InitConsole();
    }

    // spdlog initialisation
    try {
        auto logger = spdlog::basic_logger_mt(sFixName, sLogFile, false);
        spdlog::set_default_logger(logger);

        auto start_time = std::chrono::system_clock::now();

        if (cfg::System::debug) {
            spdlog::set_level(spdlog::level::debug);
        }
        spdlog::flush_on(spdlog::level::debug);
        spdlog::info("----------");
        spdlog::info("Date: {}", std::chrono::system_clock::to_time_t(start_time));
        spdlog::info("{} v{} loaded (L4N plugin mode).", sFixName.c_str(), sFixVer.c_str());
        spdlog::info("----------");


    } catch (const spdlog::spdlog_ex &ex) {
        InitConsole();
    }
}


DWORD __stdcall Hook_necola(LPVOID lpParam)
{
    HANDLE hProcess = OpenProcess(SYNCHRONIZE, FALSE, GetCurrentProcessId());
    if (!hProcess) return 1;
    LoadIni();
    Logging();

    // Get startup commandline (for logging only)
    std::wstring cmdline = cfg::System::cmdLine;
    if (cmdline.empty()) {
        cmdline = GetCommandLineW() ? GetCommandLineW() : L"";
    }
    spdlog::info(L"Startup commandline: {}", cmdline.c_str());
    spdlog::info("Necola Start! (loaded as L4N plugin)");
    G::ModuleEntry.Load();

    CloseHandle(hProcess);
    return 0;
}

void Undo_necola()
{
    spdlog::info("Stop Necola (L4N plugin unload)");
    G::ModuleEntry.undo();
}


// L4N plugin instance — bypass plugin.
// L4N only acts as the loader (scans neko/plugins/*.dll, LoadLibraryExA,
// GetProcAddress("GetL4NPluginInstance")). The core ADS logic is fully
// autonomous: own Source CreateInterface acquisition + own MinHook installs.
//
// Init strategy: spawn the init thread from the FIRST L4N callback we get
// (OnModuleLoaded for any module, or OnGameLaunch as a fallback). The init
// thread itself waits until all required Source engine modules are present
// (client.dll + engine.dll + vgui2.dll + datacache.dll + vguimatsurface.dll),
// so the exact L4N callback timing does not matter — we self-gate.
class NecolaL4NPlugin : public IL4NPlugin {
public:
    unsigned int GetInterfaceVersion() override { return 1; }
    const char* GetName() override { return "Necola-ADS"; }
    const char* GetVersion() override { return sFixVer.c_str(); }

    void OnModuleLoaded(const char* module_name, std::uintptr_t handle) override {
        // Kick off the init thread exactly once. Use a local static flag so
        // repeated callbacks from L4N do not spawn duplicate threads.
        static std::once_flag once;
        std::call_once(once, [this]() {
            if (auto h = CreateThread(NULL, 0, &NecolaL4NPlugin::InitThreadFunc, nullptr, 0, NULL)) {
                CloseHandle(h);
            }
        });
    }

    void OnGameLaunch() override {
        // Fallback in case OnModuleLoaded was never fired (defensive).
        static std::once_flag once;
        std::call_once(once, [this]() {
            if (auto h = CreateThread(NULL, 0, &NecolaL4NPlugin::InitThreadFunc, nullptr, 0, NULL)) {
                CloseHandle(h);
            }
        });
    }

    // Wait until every Source module we CreateInterface against is loaded,
    // then run the original Hook_necola init. We cannot rely on
    // serverbrowser.dll in the L4N-modified game flow, so we gate on the
    // actual modules we need.
    static DWORD __stdcall InitThreadFunc(LPVOID) {
        const char* required[] = {
            "client.dll",
            "engine.dll",
            "vgui2.dll",
            "datacache.dll",
            "vguimatsurface.dll",
            "inputsystem.dll",
            "filesystem_stdio.dll",
        };
        constexpr int kRequiredCount = sizeof(required) / sizeof(required[0]);

        // Wait up to ~120s for all modules. Each module gates an interface
        // we acquire in Entry.cpp; missing any of them crashes CreateInterface.
        for (int waited = 0; waited < 120; ++waited) {
            bool all = true;
            for (int i = 0; i < kRequiredCount; ++i) {
                if (!GetModuleHandleA(required[i])) { all = false; break; }
            }
            if (all) break;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        return Hook_necola(nullptr);
    }

    void OnD3DCreated(void* d3d) override {}
    void OnD3DDeviceCreated(void* d3d_device, bool is_dxvk) override {}
};


extern "C" __declspec(dllexport) IL4NPlugin* GetL4NPluginInstance() {
    static NecolaL4NPlugin instance;
    return &instance;
}


BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            break;
        case DLL_PROCESS_DETACH:
            Undo_necola();
            break;
    }
    return TRUE;
}
