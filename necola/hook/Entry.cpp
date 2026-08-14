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
#include "../sdk/utils/FeatureConfigManager.h"

// SEH safety net: an access violation inside the init body (e.g. a failed
// pattern-scan offset) is swallowed instead of killing the game process.
static void RunLoadBody();  // forward

void CGlobal_ModuleEntry::Load()
{
	__try {
		RunLoadBody();
	} __except(EXCEPTION_EXECUTE_HANDLER) {
	}
}

static void RunLoadBody()
{
	U::Offsets.Init();
	G::Vars.Load();

	// Engine interface acquisition
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
	}

	// Dereference sig-scanned pointers. Each one is guarded: a pattern scan
	// can fail on L4N-modified engine binaries, and a null deref would SEH-
	// abort the remaining init steps (hooks, ADS init, command registration).
	if (U::Offsets.m_dwClientMode) {
		I::ClientMode = **reinterpret_cast<void***>(U::Offsets.m_dwClientMode);
	}
	if (U::Offsets.m_dwGlobalVars) {
		I::GlobalVars = **reinterpret_cast<CGlobalVarsBase***>(U::Offsets.m_dwGlobalVars);
	}
	if (U::Offsets.m_dwCParticleSystemMgr) {
		I::ParticleSystemMgr = **reinterpret_cast<void***>(U::Offsets.m_dwCParticleSystemMgr);
	}

	{
		srand(time(NULL));
	}
	G::InputManagerI.Init();
	G::Hooks.Init();

	// Load persistent ADS/SequenceModify configuration
	{
		nlohmann::json doc = NecolaConfig::LoadConfig();
		F::AdsMgr.LoadConfig(doc);
		if (doc.contains("SequenceModify")) {
			G::Vars.sequenceLog = doc["SequenceModify"].value("SequenceLog", false);
			G::Vars.animSequenceModify = doc["SequenceModify"].value("AnimSequenceModify", false);
			G::Vars.ignoreShotgunSequence = doc["SequenceModify"].value("IgnoreShotgunSequence", false);
		}
	}

	// Install sequence property hooks (required by ADS layer sequence restoration)
	{
		F::SModify.RecvPropDataHook();
	}

	// Initialize ADS subsystem
	if (G::Vars.enableAdsSupport) {
		F::AdsMgr.Init();
	}

	{
		F::MenuMgr.InitMenuFonts();
		F::MenuMgr.InitConfigSwitches();
	}

	// Register console commands
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
}

void CGlobal_ModuleEntry::undo()
{
	G::Hooks.undo();
	G::InputManagerI.undo();
}
