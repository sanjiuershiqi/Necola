#define SPDLOG_WCHAR_TO_UTF8_SUPPORT

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <windows.h>

#include <cstdint>
#include <cstddef>
#include <string>
#include <mutex>
#include <chrono>
#include <thread>
#include <MinHook.h>
#include <inipp.h>

#include "l4n_plugin.h"
#include "hook/Entry.h"
#include "vars.h"


// ---- Logging ---------------------------------------------------------------
namespace {

std::string ResolveLogPath() {
    char exePath[MAX_PATH] = {0};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string p(exePath);
    size_t slash = p.find_last_of("\\/");
    std::string dir = (slash != std::string::npos) ? p.substr(0, slash) : ".";
    return dir + "\\L4N-Necola-ADS.log";
}

void InitSpdlog() {
    try {
        auto logger = spdlog::basic_logger_mt("necola", ResolveLogPath(), false);
        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::info);
        spdlog::flush_on(spdlog::level::warn);
    } catch (const spdlog::spdlog_ex&) {
        // Logging is best-effort; the plugin keeps working without it.
    }
}

} // namespace


// ---- Init pipeline ----------------------------------------------------------
static void LoadIniSafe() {
    try { LoadIni(); }
    catch (...) {}
}

DWORD __stdcall Hook_necola(LPVOID lpParam)
{
    InitSpdlog();
    LoadIniSafe();
    G::ModuleEntry.Load();
    return 0;
}

void Undo_necola()
{
    try { G::ModuleEntry.undo(); }
    catch (...) {}
}


// ---- L4N plugin instance ---------------------------------------------------
// L4N only loads us; core ADS logic is autonomous (own CreateInterface +
// MinHook). We kick off the init thread from the first L4N callback and
// self-gate on Source module readiness inside the thread.
class NecolaL4NPlugin : public IL4NPlugin {
public:
    unsigned int GetInterfaceVersion() override { return 1; }
    const char* GetName() override { return "Necola-ADS"; }
    const char* GetVersion() override { return sFixVer.c_str(); }

    // SHARED once_flag across both callbacks — otherwise OnGameLaunch and
    // OnModuleLoaded each spawn a separate init thread, both running
    // ModuleEntry.Load() concurrently → duplicate MH_Initialize + duplicate
    // hook installs → crash.
    static std::once_flag s_initOnce;

    void OnModuleLoaded(const char* module_name, std::uintptr_t handle) override {
        std::call_once(s_initOnce, []() {
            if (auto h = CreateThread(NULL, 0, &NecolaL4NPlugin::InitThreadFunc, nullptr, 0, NULL)) {
                CloseHandle(h);
            }
        });
    }

    void OnGameLaunch() override {
        std::call_once(s_initOnce, []() {
            if (auto h = CreateThread(NULL, 0, &NecolaL4NPlugin::InitThreadFunc, nullptr, 0, NULL)) {
                CloseHandle(h);
            }
        });
    }

    static DWORD __stdcall InitThreadFunc(LPVOID) {
        // Wait until the engine modules we interface with are alive.
        const char* required[] = {
            "client.dll", "engine.dll", "vgui2.dll", "datacache.dll",
            "vguimatsurface.dll", "inputsystem.dll", "filesystem_stdio.dll",
        };
        constexpr int kN = sizeof(required) / sizeof(required[0]);

        for (int waited = 0; waited < 120; ++waited) {
            bool all = true;
            for (int i = 0; i < kN; ++i) {
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

std::once_flag NecolaL4NPlugin::s_initOnce;


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
