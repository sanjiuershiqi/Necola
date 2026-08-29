#include "BaseClient.h"
#include "../../Feature/MenuManager/MenuManager.h"
#include "../../Feature/AdsSupport/AdsSupport.h"
#include "../../Feature/BodygroupFix/BodygroupFix.h"
#include "../../Feature/CampaignTimer/CampaignTimer.h"
#include "../../Feature/KillFeedback/KillFeedback.h"
#include "../../Feature/HitFeedback/HitFeedback.h"
#include "../../../diag.h"

#include <spdlog/spdlog.h>


using namespace Hooks;

namespace {
int MenuDigitFromButton(int keynum) {
	if (keynum >= KEY_0 && keynum <= KEY_9) return keynum - KEY_0;
	if (keynum >= KEY_PAD_0 && keynum <= KEY_PAD_9) return keynum - KEY_PAD_0;
	return -1;
}
bool g_loggedNetUpdateEnd = false;
bool g_loggedRenderStart = false;
}

void __fastcall BaseClient::LevelInitPreEntity::Detour(void* ecx, void* edx, char const* pMapName)
{
	char message[256] = {};
	_snprintf_s(message, sizeof(message), _TRUNCATE, "MapStage: LevelInitPreEntity enter map=%s",
		pMapName ? pMapName : "(null)");
	NecolaDiagLog(message);
	g_loggedNetUpdateEnd = false;
	g_loggedRenderStart = false;
	F::CampaignTimerMgr.OnLevelInitPreEntity(pMapName);
	NecolaDiagLog("MapStage: LevelInitPreEntity before original");
	Table.Original<FN>(Index)(ecx, edx, pMapName);
	NecolaDiagLog("MapStage: LevelInitPreEntity returned");
}

void __fastcall BaseClient::LevelInitPostEntity::Detour(void* ecx, void* edx)
{
	NecolaDiagLog("MapStage: LevelInitPostEntity enter");
	Table.Original<FN>(Index)(ecx, edx);
	NecolaDiagLog("MapStage: LevelInitPostEntity original returned");
	F::CampaignTimerMgr.OnLevelInitPostEntity();
	NecolaDiagLog("MapStage: LevelInitPostEntity completed");
}

void __fastcall BaseClient::LevelShutdown::Detour(void* ecx, void* edx)
{
	NecolaDiagLog("MapStage: LevelShutdown enter");
	F::CampaignTimerMgr.OnLevelShutdown();
	F::KillFeedbackMgr.Reset();
	F::HitFeedbackMgr.Reset();
	F::BodygroupFix.Reset();
	F::AdsMgr.ResetLevelState();
	F::SModify.init();
	Table.Original<FN>(Index)(ecx, edx);
	NecolaDiagLog("MapStage: LevelShutdown completed");
}


void __fastcall BaseClient::FrameStageNotify::Detour(void* ecx, void* edx, ClientFrameStage_t curStage)
{
	const bool firstNetUpdate = curStage == FRAME_NET_UPDATE_END && !g_loggedNetUpdateEnd;
	const bool firstRender = curStage == FRAME_RENDER_START && !g_loggedRenderStart;
	if (firstNetUpdate) NecolaDiagLog("MapStage: first FRAME_NET_UPDATE_END enter");
	if (firstRender) NecolaDiagLog("MapStage: first FRAME_RENDER_START enter");
	switch(curStage)
	{
		case FRAME_NET_UPDATE_END:
			F::KillFeedbackMgr.RefreshSpecialVictims();
			break;
		case FRAME_RENDER_START:
		{
			if (G::Vars.enableAdsSupport) {
				F::AdsMgr.FrameUpdate();
			}
			F::BodygroupFix.FrameUpdate();
			break;
		}
		default: break;
	}
	if (firstNetUpdate) NecolaDiagLog("MapStage: first FRAME_NET_UPDATE_END before original");
	if (firstRender) NecolaDiagLog("MapStage: first FRAME_RENDER_START before original");
	Table.Original<FN>(Index)(ecx, edx, curStage);
	if (firstNetUpdate) {
		g_loggedNetUpdateEnd = true;
		NecolaDiagLog("MapStage: first FRAME_NET_UPDATE_END completed");
	}
	if (firstRender) {
		g_loggedRenderStart = true;
		NecolaDiagLog("MapStage: first FRAME_RENDER_START completed");
	}
}


int __fastcall BaseClient::IN_KeyEvent::Detour(void* ecx, void* edx, int eventcode, int keynum, const char* pszCurrentBinding)
{
	// Consume both press and release while the menu owns a navigation key, so
	// gameplay binds do not fire underneath the overlay.
	if (F::MenuMgr.IsVisible()) {
		const int digit = MenuDigitFromButton(keynum);
		if (digit >= 0) {
			if (eventcode == 1) F::MenuMgr.ProcessKey(digit);
			return 0;
		}
		if (keynum == KEY_LEFT || keynum == KEY_PAGEUP) {
			if (eventcode == 1) F::MenuMgr.ProcessKey(8);
			return 0;
		}
		if (keynum == KEY_RIGHT || keynum == KEY_PAGEDOWN) {
			if (eventcode == 1) F::MenuMgr.ProcessKey(9);
			return 0;
		}
		if (keynum == KEY_BACKSPACE) {
			if (eventcode == 1) F::MenuMgr.ProcessKey(0);
			return 0;
		}
		if (keynum == KEY_ESCAPE || keynum == KEY_ENTER || keynum == KEY_PAD_ENTER) {
			if (eventcode == 1) F::MenuMgr.ProcessKey(0);
			return 0;
		}
	}
	if (eventcode == 1 && pszCurrentBinding && std::strcmp(pszCurrentBinding, "+attack") == 0) {
		F::HitFeedbackMgr.ArmLocalAttack();
	}

	// ADS zoom / mixed toggle
	if (G::Vars.enableAdsSupport && pszCurrentBinding) {
		if (strcmp(pszCurrentBinding, "+zoom") == 0 && eventcode == 1) {
			bool blockNativeZoom = false;
			if (I::EngineClient && I::ClientEntityList) {
				auto* localEntity = I::ClientEntityList->GetClientEntity(I::EngineClient->GetLocalPlayer());
				C_TerrorPlayer* local = localEntity ? localEntity->As<C_TerrorPlayer*>() : nullptr;
				auto* activeWeapon = local && !local->deadflag() ? local->GetActiveWeapon() : nullptr;
				C_TerrorWeapon* weapon = activeWeapon ? activeWeapon->As<C_TerrorWeapon*>() : nullptr;
				blockNativeZoom = weapon && F::AdsMgr.ShouldBlockNativeZoom(weapon->GetWeaponID());
			}
			F::AdsMgr.OnZoomPressed();
			if (blockNativeZoom && F::AdsMgr.HasAdsAnimations()) return 0;
		} else if (strcmp(pszCurrentBinding, "+use") == 0 && eventcode == 1) {
			// +use triggers MIXED pipeline state toggle in any state (normal or ADS)
			F::AdsMgr.OnMixedPressed();
		}
	}

	return Table.Original<FN>(Index)(ecx, edx, eventcode, keynum, pszCurrentBinding);
}

bool BaseClient::Init()
{
	if (!Table.Init(I::BaseClient, FrameStageNotify::Index + 1)) return false;

	bool ok = Table.Hook(&LevelInitPreEntity::Detour, LevelInitPreEntity::Index);
	ok = Table.Hook(&LevelInitPostEntity::Detour, LevelInitPostEntity::Index) && ok;
	ok = Table.Hook(&LevelShutdown::Detour, LevelShutdown::Index) && ok;
	ok = Table.Hook(&FrameStageNotify::Detour, FrameStageNotify::Index) && ok;
	ok = Table.Hook(&IN_KeyEvent::Detour, IN_KeyEvent::Index) && ok;
	return ok;
}
