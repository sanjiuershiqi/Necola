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
    std::wstring cmdline = cfg::System::cmdLine;
    if(cmdline.empty())
    {
        std::wstring cmdline = GetCommandLineW();
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
// This removes the previous Detours-based injector (left4dead2_necola.exe).
class NecolaL4NPlugin : public IL4NPlugin {
public:
    unsigned int GetInterfaceVersion() override { return 1; }
    const char* GetName() override { return "Necola-ADS"; }
    const char* GetVersion() override { return sFixVer.c_str(); }

    // client.dll is the last major game module to load before the client is
    // ready. Spawn the init thread here; it still waits for serverbrowser.dll
    // (loaded late, during main menu) as the final readiness gate before
    // acquiring Source interfaces and installing hooks.
    void OnModuleLoaded(const char* module_name, std::uintptr_t handle) override {
        if (std::string_view(module_name) == "client") {
            if (auto h = CreateThread(NULL, 0, Hook_necola, nullptr, 0, NULL)) {
                CloseHandle(h);
            }
        }
    }

    void OnGameLaunch() override {}
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
