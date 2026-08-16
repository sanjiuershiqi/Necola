#include "Entry.h"
#include <iostream>
#include <stdlib.h>
#include <time.h>
#include <spdlog/spdlog.h>
#include <windows.h>

#include "./Feature/SequenceModify/SequenceModify.h"
#include "./Feature/CommandManager/CommandManager.h"
#include "./Feature/MenuManager/MenuManager.h"
#include "./Feature/AdsSupport/AdsSupport.h"
#include "../sdk/L4NEnv.h"
#include "../sdk/l4d2/interfaces/IConVar.h"
#include "../sdk/utils/FeatureConfigManager.h"

// ---- Local diagnostic logger (Win32, no CRT deps) ------------------------
namespace {
std::string EntryLogPath() {
    char exePath[MAX_PATH] = {0};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string p(exePath);
    size_t slash = p.find_last_of("\\/");
    std::string dir = (slash != std::string::npos) ? p.substr(0, slash) : ".";
    return dir + "\\L4N-Necola-ADS-diag.log";
}

void ELog(const char* msg) {
    static std::string path = EntryLogPath();
    HANDLE h = CreateFileA(path.c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME st; GetLocalTime(&st);
    char line[2048];
    int n = _snprintf_s(line, sizeof(line), _TRUNCATE,
        "[%04u-%02u-%02u %02u:%02u:%02u.%03u] %s\r\n",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
        st.wMilliseconds, msg);
    if (n > 0) { DWORD w = 0; WriteFile(h, line, (DWORD)n, &w, nullptr); }
    CloseHandle(h);
}
} // namespace

// SEH translator: convert access-violation etc. into a logged message so
// we can see WHERE it crashes instead of a silent process termination.
// (C++ catch(...) with /EHsc does NOT catch SEH exceptions.)
static void RunLoadBody();  // forward

void CGlobal_ModuleEntry::Load()
{
	ELog("=== CGlobal_ModuleEntry::Load entered ===");

	// Wrap the entire body in SEH so an access violation logs its address
	// instead of killing the process silently.
	__try {
		RunLoadBody();
		ELog("=== CGlobal_ModuleEntry::Load body completed OK ===");
	} __except(EXCEPTION_EXECUTE_HANDLER) {
		char buf[256];
		_snprintf_s(buf, sizeof(buf), _TRUNCATE,
			"!!! SEH exception 0x%08X in ModuleEntry::Load",
			GetExceptionCode());
		ELog(buf);
	}
}

static void RunLoadBody()
{
	ELog("Step 1: U::Offsets.Init()");
	U::Offsets.Init();
	ELog("Step 1 done");

	ELog("Step 2: G::Vars.Load()");
	G::Vars.Load();
	ELog("Step 2 done");

	// Engine interface acquisition
	ELog("Step 3: CreateInterface calls");
	{
		I::BaseClient       = U::Interface.Get<IBaseClientDLL*>("client.dll", "VClient016");
		I::ClientEntityList = U::Interface.Get<IClientEntityList*>("client.dll", "VClientEntityList003");
		I::Prediction       = U::Interface.Get<IPrediction*>("client.dll", "VClientPrediction001");
		I::ModelInfo        = U::Interface.Get<IVModelInfo*>("engine.dll", "VModelInfoClient004");
		I::GameEventManager = U::Interface.Get<IGameEventManager2*>("engine.dll", "GAMEEVENTSMANAGER002");
		I::EngineVGui       = U::Interface.Get<IEngineVGui*>("engine.dll", "VEngineVGui001");
		I::EngineClient     = U::Interface.Get<IVEngineClient*>("engine.dll", "VEngineClient013");
		I::EngineSound 		= U::Interface.Get<IEngineSound*>("engine.dll", "IEngineSoundClient003");
		I::NetworkStringTable = U::Interface.Get<INetworkStringTableContainer*>("engine.dll", "VEngineClientStringTable001");
		I::EngineTrace		= U::Interface.Get<IEngineTrace*>("engine.dll", "EngineTraceClient003");

		I::MDLCache 		= U::Interface.Get<IMDLCache*>("datacache.dll", "MDLCache004");

		I::FileSystem 		= U::Interface.Get<IFileSystem*>("filesystem_stdio.dll", "VFileSystem018");

		I::VGuiPanel        = U::Interface.Get<IVGuiPanel*>("vgui2.dll", "VGUI_Panel009");
		I::VGuiSurface      = U::Interface.Get<IVGuiSurface*>("vgui2.dll", "VGUI_Surface031");
		I::MatSystemSurface = U::Interface.Get<IMatSystemSurface*>("vguimatsurface.dll", "VGUI_Surface031");
		I::InputSystem		= U::Interface.Get<IInputSystem*>("inputsystem.dll", "InputSystemVersion001");

		// ICvar — required for L4N coordination (l4n_* cvars) and for the
		// direct-ConVar crosshair control in EngineVGui::Paint.
		I::Cvars			= U::Interface.Get<ICvar*>("vstdlib.dll", "VEngineCvar007");
	}
	{
		char buf[512];
		_snprintf_s(buf, sizeof(buf), _TRUNCATE,
			"  Interface check: BaseClient=%p ClientEntityList=%p EngineClient=%p "
			"EngineVGui=%p VGuiPanel=%p VGuiSurface=%p MatSysSurface=%p InputSystem=%p "
			"MDLCache=%p FileSystem=%p ModelInfo=%p GameEventMgr=%p Prediction=%p "
			"EngineSound=%p NetStrTable=%p EngineTrace=%p Cvars=%p",
			(void*)I::BaseClient, (void*)I::ClientEntityList, (void*)I::EngineClient,
			(void*)I::EngineVGui, (void*)I::VGuiPanel, (void*)I::VGuiSurface,
			(void*)I::MatSystemSurface, (void*)I::InputSystem,
			(void*)I::MDLCache, (void*)I::FileSystem, (void*)I::ModelInfo,
			(void*)I::GameEventManager, (void*)I::Prediction,
			(void*)I::EngineSound, (void*)I::NetworkStringTable, (void*)I::EngineTrace,
			(void*)I::Cvars);
		ELog(buf);
	}

	// L4N environment awareness: detect the platform, cache the coordinated
	// l4n_* cvars and log a one-shot conflict report (l4n_patch_hud_scope /
	// l4n_vm_sway_* interactions with ADS).
	ELog("Step 3.7: L4N::Env.Init()");
	L4N::Env.Init();
	ELog("Step 3.7 done");

	{
		char buf[256];
		_snprintf_s(buf, sizeof(buf), _TRUNCATE,
			"Step 3.5: Offsets check: m_dwClientMode=%p m_dwGlobalVars=%p m_dwCParticleSystemMgr=%p",
			(void*)U::Offsets.m_dwClientMode, (void*)U::Offsets.m_dwGlobalVars,
			(void*)U::Offsets.m_dwCParticleSystemMgr);
		ELog(buf);
	}
	ELog("Step 3.6: dereferencing offsets for ClientMode/GlobalVars/ParticleSystemMgr");
	{
		// Guard each dereference against null offsets — pattern scans can fail
		// (esp. m_dwCParticleSystemMgr on L4N-modified client.dll), and a null
		// deref would SEH-skip Steps 4-10 (incl. command registration) →
		// "commands don't work". ParticleSystemMgr is unused by ADS-only build,
		// so a null there is safe; ClientMode/GlobalVars are required by ADS.
		if (U::Offsets.m_dwClientMode) {
			I::ClientMode = **reinterpret_cast<void***>(U::Offsets.m_dwClientMode);
		} else {
			ELog("  WARN: m_dwClientMode is NULL — ADS will be limited");
		}
		if (U::Offsets.m_dwGlobalVars) {
			I::GlobalVars = **reinterpret_cast<CGlobalVarsBase***>(U::Offsets.m_dwGlobalVars);
		} else {
			ELog("  WARN: m_dwGlobalVars is NULL — ADS will be limited");
		}
		if (U::Offsets.m_dwCParticleSystemMgr) {
			I::ParticleSystemMgr = **reinterpret_cast<void***>(U::Offsets.m_dwCParticleSystemMgr);
		} else {
			ELog("  WARN: m_dwCParticleSystemMgr is NULL (pattern scan failed) — ParticleSystemMgr unavailable, unused by ADS");
		}
	}
	ELog("Step 3 done");

	{
		srand(time(NULL));
	}
	ELog("Step 4: G::InputManagerI.Init()");
	G::InputManagerI.Init();
	ELog("Step 4 done");

	ELog("Step 5: G::Hooks.Init()");
	G::Hooks.Init();
	ELog("Step 5 done");

	// Load persistent ADS/SequenceModify configuration
	ELog("Step 6: LoadConfig / AdsMgr.LoadConfig");
	{
		nlohmann::json doc = NecolaConfig::LoadConfig();
		F::AdsMgr.LoadConfig(doc);
		if (doc.contains("SequenceModify")) {
			G::Vars.sequenceLog = doc["SequenceModify"].value("SequenceLog", false);
			G::Vars.animSequenceModify = doc["SequenceModify"].value("AnimSequenceModify", false);
			G::Vars.ignoreShotgunSequence = doc["SequenceModify"].value("IgnoreShotgunSequence", false);
		}
	}
	ELog("Step 6 done");

	// Install sequence property hooks (required by ADS layer sequence restoration)
	ELog("Step 7: F::SModify.RecvPropDataHook()");
	{
		F::SModify.RecvPropDataHook();
	}
	ELog("Step 7 done");

	// Initialize ADS subsystem
	ELog("Step 8: AdsMgr.Init (if enabled)");
	if (G::Vars.enableAdsSupport) {
		F::AdsMgr.Init();
	}
	ELog("Step 8 done");

	ELog("Step 9: MenuMgr init");
	{
		F::MenuMgr.InitMenuFonts();
		F::MenuMgr.InitConfigSwitches();
	}
	ELog("Step 9 done");

	// Register console commands
	ELog("Step 10: register commands");
	{
		F::CmdMgr.RegistCommand("necola_menu", F::MenuMgr.ToggleNecolaMenu, "toggle necola menu");
		F::CmdMgr.RegistCommand("necola_ads", [](int*) {
			if (G::Vars.enableAdsSupport) {
				F::AdsMgr.OnNecolaAdsPressed();
			}
		}, "toggle Necola ADS");
		F::CmdMgr.RegistCommand("necola_ads_mixed", [](int*) {
			if (G::Vars.enableAdsSupport) {
				F::AdsMgr.OnMixedPressed();
			}
		}, "toggle Necola ADS MIXED state");
		F::CmdMgr.RegistCommand("necola_ads_foreceback", [](int*) {
			if (G::Vars.enableAdsSupport) {
				F::AdsMgr.OnForcebackPressed();
			}
		}, "forceback ADS to normal state");
		F::CmdMgr.RegistCommand("necola_ads_back", [](int*) {
			if (G::Vars.enableAdsSupport) {
				F::AdsMgr.OnAdsBackPressed();
			}
		}, "go back to previous ADS state");
	}
	ELog("Step 10 done");
}

void CGlobal_ModuleEntry::undo()
{
	ELog("undo: restore crosshair if forced off");
	Hooks::EngineVGui::RestoreCrosshairForUnload();
	ELog("undo: G::Hooks.undo()");
	G::Hooks.undo();
	ELog("undo: G::InputManagerI.undo()");
	G::InputManagerI.undo();
	ELog("undo done");
}
