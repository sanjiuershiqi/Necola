#include "BaseClient.h"
#include "../../Feature/MenuManager/MenuManager.h"
#include "../../Feature/AdsSupport/AdsSupport.h"
#include "../../Feature/BodygroupFix/BodygroupFix.h"

#include <spdlog/spdlog.h>


using namespace Hooks;

void __fastcall BaseClient::LevelInitPreEntity::Detour(void* ecx, void* edx, char const* pMapName)
{
	Table.Original<FN>(Index)(ecx, edx, pMapName);
}

void __fastcall BaseClient::LevelInitPostEntity::Detour(void* ecx, void* edx)
{
	Table.Original<FN>(Index)(ecx, edx);
}

void __fastcall BaseClient::LevelShutdown::Detour(void* ecx, void* edx)
{
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
	// Toggle / navigate menu via number keys / F/P
	if((keynum == 70 || keynum == 57) && F::MenuMgr.IsVisible() && eventcode == 1) {
		F::MenuMgr.Toggle();
	}
	if((keynum > 0 && keynum < 11) && eventcode == 1) {
		if(F::MenuMgr.IsVisible()) {
			F::MenuMgr.ProcessKey(keynum - 1);
			return 0;
		}
	}

	// ADS zoom / mixed toggle
	if (G::Vars.enableAdsSupport && pszCurrentBinding) {
		if (strcmp(pszCurrentBinding, "+zoom") == 0 && eventcode == 1) {
			C_TerrorPlayer* pLocalAds = I::ClientEntityList->GetClientEntity(I::EngineClient->GetLocalPlayer())->As<C_TerrorPlayer*>();
			if (pLocalAds && !pLocalAds->deadflag()) {
				C_TerrorWeapon* pWeaponAds = pLocalAds->GetActiveWeapon()->As<C_TerrorWeapon*>();
				if (pWeaponAds && F::AdsMgr.ShouldBlockNativeZoom(pWeaponAds->GetWeaponID())) {
					F::AdsMgr.OnZoomPressed();
					if (F::AdsMgr.HasAdsAnimations()) {
						return 0;
					}
				} else {
					F::AdsMgr.OnZoomPressed();
				}
			} else {
				F::AdsMgr.OnZoomPressed();
			}
		} else if (strcmp(pszCurrentBinding, "+use") == 0 && eventcode == 1) {
			// +use triggers MIXED pipeline state toggle in any state (normal or ADS)
			F::AdsMgr.OnMixedPressed();
		}
	}

	return Table.Original<FN>(Index)(ecx, edx, eventcode, keynum, pszCurrentBinding);
}

void BaseClient::Init()
{
	Table.Init(I::BaseClient);

	Table.Hook(&LevelInitPreEntity::Detour, LevelInitPreEntity::Index);
	Table.Hook(&LevelInitPostEntity::Detour, LevelInitPostEntity::Index);
	Table.Hook(&FrameStageNotify::Detour, FrameStageNotify::Index);
	Table.Hook(&IN_KeyEvent::Detour, IN_KeyEvent::Index);
}
