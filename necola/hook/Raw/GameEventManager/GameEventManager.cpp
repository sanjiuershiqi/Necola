#include "GameEventManager.h"

#include "../../Feature/AdsSupport/AdsSupport.h"
#include "../../Feature/CampaignTimer/CampaignTimer.h"

using namespace Hooks;

bool __fastcall GameEventManager::FireEventClient::Detour(void* ecx, void* edx,  IGameEvent *event) {
	const char *name = event->GetName();

	// Reset ADS/MIXED state on map transition
	if(strcmp("map_transition", name) == 0) {
		F::CampaignTimerMgr.OnMapTransition();
		if (G::Vars.enableAdsSupport) {
			F::AdsMgr.SilentExitADS();
		}
	}

	// Reset ADS/MIXED state on mission lost
	if(strcmp("mission_lost", name) == 0) {
		F::CampaignTimerMgr.OnMissionLost();
		if (G::Vars.enableAdsSupport) {
			F::AdsMgr.SilentExitADS();
		}
	}

	if (strcmp("round_start", name) == 0) {
		F::CampaignTimerMgr.OnRoundStart();
	}

	// Reset ADS state on local player death
	if(strcmp("player_death", name) == 0) {
		if (G::Vars.enableAdsSupport) {
			int userid = event->GetInt("userid");
			int iLocal = I::EngineClient->GetLocalPlayer();
			if (I::EngineClient->GetPlayerForUserID(userid) == iLocal) {
				F::AdsMgr.SilentExitADS();
			}
		}
	}

	// Reset ADS state on weapon swap
	if (strcmp("weapon_pickup", name) == 0 || strcmp("player_use", name) == 0) {
		if (G::Vars.enableAdsSupport && (F::AdsMgr.IsAdsActive() || F::AdsMgr.IsMixedActive())) {
			int userid = event->GetInt("userid");
			int iLocal = I::EngineClient->GetLocalPlayer();
			if (I::EngineClient->GetPlayerForUserID(userid) == iLocal) {
				C_TerrorPlayer* pLocal = I::ClientEntityList->GetClientEntity(iLocal)->As<C_TerrorPlayer*>();
				if (pLocal && !pLocal->deadflag()) {
					C_TerrorWeapon* weapon = pLocal->GetActiveWeapon()->As<C_TerrorWeapon*>();
					if (weapon) {
						int weaponEntIdx = weapon->entindex();
						bool isDual = weapon->GetWeaponID() == WEAPON_PISTOL && weapon->IsDualWielding();
						if (weaponEntIdx != F::AdsMgr.GetCachedWeaponEntIdx() ||
							(weapon->GetWeaponID() == WEAPON_PISTOL && isDual != F::AdsMgr.GetCachedIsDualPistol())) {
							F::AdsMgr.SilentExitADS();
						}
					}
				}
			}
		}
	}

	return Table.Original<FN>(Index)(ecx, edx, event);
}

bool GameEventManager::Init()
{
	return Table.Init(I::GameEventManager, FireEventClient::Index + 1)
		&& Table.Hook(&FireEventClient::Detour, FireEventClient::Index);
}
