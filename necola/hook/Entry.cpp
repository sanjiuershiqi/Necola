#include "Entry.h"
#include <iostream>
#include <stdlib.h>
#include <time.h>
#include <spdlog/spdlog.h>
#include <windows.h>
#include <exception>

#include "./Feature/SequenceModify/SequenceModify.h"
#include "./Feature/CommandManager/CommandManager.h"
#include "./Feature/MenuManager/MenuManager.h"
#include "./Feature/AdsSupport/AdsSupport.h"
#include "./Feature/CampaignTimer/CampaignTimer.h"
#include "./Feature/KillFeedback/KillFeedback.h"
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
static bool RunLoadBody();  // forward

static bool RunLoadBodyWithCppExceptionGuard() {
	try {
		return RunLoadBody();
	} catch (const std::exception& exception) {
		char buf[512];
		_snprintf_s(buf, sizeof(buf), _TRUNCATE,
			"!!! C++ exception in ModuleEntry::Load: %s", exception.what());
		ELog(buf);
	} catch (...) {
		ELog("!!! unknown C++ exception in ModuleEntry::Load");
	}
	return false;
}

struct NamedRequirement {
	const char* name;
	const void* value;
};

template <size_t N>
bool ValidateRequirements(const char* category, const NamedRequirement (&requirements)[N]) {
	bool valid = true;
	for (const auto& requirement : requirements) {
		if (requirement.value) continue;
		char buf[256];
		_snprintf_s(buf, sizeof(buf), _TRUNCATE,
			"ERROR: missing required %s: %s", category, requirement.name);
		ELog(buf);
		valid = false;
	}
	return valid;
}

bool CGlobal_ModuleEntry::Load()
{
	ELog("=== CGlobal_ModuleEntry::Load entered ===");
	bool success = false;

	// Wrap the entire body in SEH so an access violation logs its address
	// instead of killing the process silently.
	__try {
		success = RunLoadBodyWithCppExceptionGuard();
		ELog(success
			? "=== CGlobal_ModuleEntry::Load body completed OK ==="
			: "=== CGlobal_ModuleEntry::Load aborted ===");
	} __except(EXCEPTION_EXECUTE_HANDLER) {
		char buf[256];
		_snprintf_s(buf, sizeof(buf), _TRUNCATE,
			"!!! SEH exception 0x%08X in ModuleEntry::Load",
			GetExceptionCode());
		ELog(buf);
	}
	if (!success) {
		ELog("rolling back partial initialization");
		undo();
	}
	return success;
}

static bool RunLoadBody()
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
		I::MaterialSystem	= U::Interface.Get<IMaterialSystem*>("materialsystem.dll", "VMaterialSystem080");

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
			"EngineSound=%p NetStrTable=%p EngineTrace=%p Cvars=%p MaterialSystem=%p",
			(void*)I::BaseClient, (void*)I::ClientEntityList, (void*)I::EngineClient,
			(void*)I::EngineVGui, (void*)I::VGuiPanel, (void*)I::VGuiSurface,
			(void*)I::MatSystemSurface, (void*)I::InputSystem,
			(void*)I::MDLCache, (void*)I::FileSystem, (void*)I::ModelInfo,
			(void*)I::GameEventManager, (void*)I::Prediction,
			(void*)I::EngineSound, (void*)I::NetworkStringTable, (void*)I::EngineTrace,
			(void*)I::Cvars, (void*)I::MaterialSystem);
		ELog(buf);
	}
	const NamedRequirement requiredInterfaces[] = {
		{"IBaseClientDLL", I::BaseClient},
		{"IClientEntityList", I::ClientEntityList},
		{"IVModelInfo", I::ModelInfo},
		{"IGameEventManager2", I::GameEventManager},
		{"IEngineVGui", I::EngineVGui},
		{"IVEngineClient", I::EngineClient},
		{"IMatSystemSurface", I::MatSystemSurface},
	};
	if (!ValidateRequirements("interface", requiredInterfaces)) return false;
	if (!I::Cvars) ELog("WARN: ICvar unavailable; L4N coordination and direct crosshair control disabled");
	if (!I::MaterialSystem) ELog("WARN: IMaterialSystem unavailable; campaign timer HUD integration disabled");

	const NamedRequirement requiredOffsets[] = {
		{"m_dwGlobalVars", reinterpret_cast<void*>(U::Offsets.m_dwGlobalVars)},
		{"m_dwStaticConCommand", reinterpret_cast<void*>(U::Offsets.m_dwStaticConCommand)},
		{"m_dwSendWeaponAnim", reinterpret_cast<void*>(U::Offsets.m_dwSendWeaponAnim)},
		{"m_dwSetIdealActivity", reinterpret_cast<void*>(U::Offsets.m_dwSetIdealActivity)},
		{"m_dwRecvProxySequenceViewModel", reinterpret_cast<void*>(U::Offsets.m_dwRecvProxySequenceViewModel)},
		{"m_dwSelectWeightedSequence", reinterpret_cast<void*>(U::Offsets.m_dwSelectWeightedSequence)},
		{"m_dwFireEvent", reinterpret_cast<void*>(U::Offsets.m_dwFireEvent)},
		{"m_dwGetSequenceActivity", reinterpret_cast<void*>(U::Offsets.m_dwGetSequenceActivity)},
		{"m_dwFindBodygroupByName", reinterpret_cast<void*>(U::Offsets.m_dwFindBodygroupByName)},
		{"m_dwSetBodygroup", reinterpret_cast<void*>(U::Offsets.m_dwSetBodygroup)},
	};
	if (!ValidateRequirements("offset", requiredOffsets)) return false;

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
	ELog("Step 3.6: dereferencing required GlobalVars offset");
	I::GlobalVars = **reinterpret_cast<CGlobalVarsBase***>(U::Offsets.m_dwGlobalVars);
	if (!I::GlobalVars) {
		ELog("ERROR: m_dwGlobalVars resolved to a null pointer");
		return false;
	}
	ELog("Step 3 done");

	{
		srand(time(NULL));
	}
	ELog("Step 4: G::InputManagerI.Init()");
	G::InputManagerI.Init();
	ELog("Step 4 done");

	ELog("Step 5: G::Hooks.Init()");
	if (!G::Hooks.Init()) {
		ELog("ERROR: one or more MinHook detours could not be installed");
		return false;
	}
	ELog("Step 5 done");

	// Load persistent ADS/SequenceModify configuration
	ELog("Step 6: LoadConfig / AdsMgr.LoadConfig");
	{
		nlohmann::json doc = NecolaConfig::LoadConfig();
		F::AdsMgr.LoadConfig(doc);
		if (const auto it = doc.find("SequenceModify"); it != doc.end() && it->is_object()) {
			auto readBool = [it](const char* key, bool fallback) {
				const auto value = it->find(key);
				return value != it->end() && value->is_boolean() ? value->get<bool>() : fallback;
			};
			G::Vars.sequenceLog = readBool("SequenceLog", false);
			G::Vars.animSequenceModify = readBool("AnimSequenceModify", false);
			G::Vars.ignoreShotgunSequence = readBool("IgnoreShotgunSequence", false);
		}
		F::MenuMgr.LoadConfig(doc);
		F::KillFeedbackMgr.LoadConfig(doc);
	}
	ELog("Step 6 done");

	// Install sequence property hooks (required by ADS layer sequence restoration)
	ELog("Step 7: F::SModify.RecvPropDataHook()");
	if (!F::SModify.RecvPropDataHook()) {
		ELog("ERROR: required CBaseViewModel RecvProp proxies were not installed");
		return false;
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
		F::CmdMgr.RegistCommand("necola_timer_reset", [](int*) {
			F::CampaignTimerMgr.Reset();
			if (I::Cvars) I::Cvars->ConsolePrintf("[Necola] campaign timer reset\n");
		}, "reset the Necola campaign timer");
		F::CmdMgr.RegistCommand("necola_timer_status", [](int*) {
			const std::uint64_t total = F::CampaignTimerMgr.GetCampaignSeconds();
			if (I::Cvars) {
				I::Cvars->ConsolePrintf("[Necola] campaign timer %02llu:%02llu:%02llu, chapter %llu seconds, map %s\n",
					total / 3600, (total / 60) % 60, total % 60,
					F::CampaignTimerMgr.GetChapterSeconds(), F::CampaignTimerMgr.GetCurrentMap().c_str());
			}
		}, "print the Necola campaign timer status");
		F::CmdMgr.RegistCommand("necola_killfeedback_status", [](int*) {
			F::KillFeedbackMgr.PrintStatus();
		}, "print Necola kill feedback status");
		F::CmdMgr.RegistCommand("necola_killfeedback_test", [](int*) {
			F::KillFeedbackMgr.Preview(KillFeedbackEffect::Kill1);
		}, "test Necola kill feedback assets");
	}
	ELog("Step 10 done");

	// Register kill feedback event listener via the official AddListener API
	// (works on both listen and dedicated servers, unlike vtable hooks).
	ELog("Step 11: KillFeedback listeners");
	if (!F::KillFeedbackMgr.InitListeners()) {
		ELog("WARN: one or more kill feedback event listeners failed to register");
	}
	ELog("Step 11 done");
	return true;
}

void CGlobal_ModuleEntry::undo()
{
	ELog("undo: restore crosshair if forced off");
	Hooks::EngineVGui::RestoreCrosshairForUnload();
	ELog("undo: release campaign timer materials");
	F::CampaignTimerMgr.Shutdown();
	ELog("undo: release kill feedback materials");
	F::KillFeedbackMgr.Shutdown();
	ELog("undo: restore RecvProp proxies");
	F::SModify.RecvPropDataUnhook();
	ELog("undo: G::Hooks.undo()");
	G::Hooks.undo();
	ELog("undo: G::InputManagerI.undo()");
	G::InputManagerI.undo();
	ELog("undo done");
}
