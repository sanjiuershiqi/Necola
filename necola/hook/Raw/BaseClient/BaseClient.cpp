#include "BaseClient.h"
#include "../../Feature/MenuManager/MenuManager.h"
#include "../../Feature/AdsSupport/AdsSupport.h"
#include "../../Feature/BodygroupFix/BodygroupFix.h"
#include "../../Feature/CampaignTimer/CampaignTimer.h"
#include "../../Feature/KillFeedback/KillFeedback.h"

#include <spdlog/spdlog.h>


using namespace Hooks;

namespace {
int MenuDigitFromButton(int keynum) {
	if (keynum >= KEY_0 && keynum <= KEY_9) return keynum - KEY_0;
	if (keynum >= KEY_PAD_0 && keynum <= KEY_PAD_9) return keynum - KEY_PAD_0;
	return -1;
}
}

void __fastcall BaseClient::LevelInitPreEntity::Detour(void* ecx, void* edx, char const* pMapName)
{
	F::CampaignTimerMgr.OnLevelInitPreEntity(pMapName);
	Table.Original<FN>(Index)(ecx, edx, pMapName);
}

void __fastcall BaseClient::LevelInitPostEntity::Detour(void* ecx, void* edx)
{
	Table.Original<FN>(Index)(ecx, edx);
	F::CampaignTimerMgr.OnLevelInitPostEntity();
}

void __fastcall BaseClient::LevelShutdown::Detour(void* ecx, void* edx)
{
	F::CampaignTimerMgr.OnLevelShutdown();
	F::KillFeedbackMgr.Reset();
	Table.Original<FN>(Index)(ecx, edx);
}


void __fastcall BaseClient::FrameStageNotify::Detour(void* ecx, void* edx, ClientFrameStage_t curStage)
{
	switch(curStage)
	{
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
	Table.Original<FN>(Index)(ecx, edx, curStage);
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
		if (keynum == KEY_ESCAPE || keynum == KEY_ENTER || keynum == KEY_PAD_ENTER) {
			if (eventcode == 1) F::MenuMgr.ProcessKey(0);
			return 0;
		}
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
