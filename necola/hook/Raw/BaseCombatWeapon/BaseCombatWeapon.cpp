#include "BaseCombatWeapon.h"

#include "../../Vars.h"
#include "../../Feature/SequenceModify/SequenceModify.h"
#include "../../Feature/AdsSupport/AdsSupport.h"
#include <spdlog/spdlog.h>
#include <iostream>

using namespace Hooks;

// 判断 activity 是否属于 Necola 自定义的 ADS/MIXED 动画区间
static bool IsNecolaAdsOrMixedActivity(int activity) {
	if (activity >= ACT_PRIMARY_VM_DRAW && activity <= ACT_PRIMARY_VM_LOWERED_TO_IDLE) return true;
	if (activity >= ACT_SECONDARY_VM_DRAW && activity <= ACT_SECONDARY_VM_LOWERED_TO_IDLE) return true;
	if (activity >= ACT_PRIMARY_VM_MELEE && activity <= MIXED_ACT_FOURTH_VM_INSPECT) return true;
	return false;
}

bool __fastcall BaseCombatWeapon::SendWeaponAnim::Detour(C_BaseCombatWeapon* pThis, void* edx, int a2)
{
	C_TerrorPlayer* pLocal = I::ClientEntityList->GetClientEntity(I::EngineClient->GetLocalPlayer())->As<C_TerrorPlayer*>();
	if(pLocal && !pLocal->deadflag()) {
		if (G::Vars.enableAdsSupport) {
			// ADS/MIXED state: weapon slot change detection + Activity redirection
			if (F::AdsMgr.NeedsRemapping()) {
				if (a2 == ACT_VM_IDLE) {
					if (G::Vars.adsLog) spdlog::info("[ADS] SendWeaponAnim: block normal IDLE (activity={}) during ADS/MIXED, return false", a2);
					return false;
				}

				if (IsNecolaAdsOrMixedActivity(a2)) {
					if (G::Vars.adsLog) spdlog::info("[ADS] SendWeaponAnim: pass-through native ADS activity={}", a2);
					return Func.Original<FN>()(pThis, edx, a2);
				}

				C_TerrorWeapon* pWeapon = pLocal->GetActiveWeapon()->As<C_TerrorWeapon*>();
				if (pWeapon) {
					if (pWeapon->entindex() != F::AdsMgr.GetCachedWeaponEntIdx()) {
						if (G::Vars.adsLog) spdlog::info("[ADS] SendWeaponAnim: weapon changed (active={} cached={}), SilentExitADS", pWeapon->entindex(), F::AdsMgr.GetCachedWeaponEntIdx());
						F::AdsMgr.SilentExitADS();
					} else if (pWeapon->entindex() == pThis->entindex()) {
						int adsAct = F::AdsMgr.GetAdsRemappedActivity(a2);
						if (adsAct != -1) {
							if (G::Vars.adsLog) spdlog::info("[ADS] SendWeaponAnim: redirect {} -> {} (level={})", a2, adsAct, F::AdsMgr.GetAdsLevel());
							if (adsAct < ACT_PRIMARY_VM_MELEE) {
								return Func.Original<FN>()(pThis, edx, adsAct);
							}
							// Necola custom remapped activities: resolve draw call batch index and use
							C_BaseViewModel* vm = pLocal->m_hViewModel()->As<C_BaseViewModel*>();
							if (vm) {
								C_BaseAnimating* pAnim = vm->GetBaseAnimating();
								if (pAnim) {
									int seq = F::AdsSupport::LookupRandomSequenceForActivity(pAnim, adsAct);
									if (seq != -1) {
										if (G::Vars.adsLog) spdlog::info("[ADS] SendWeaponAnim: custom remap {} -> seq={}, SendViewModelAnim", adsAct, seq);
										pWeapon->SendViewModelAnim(seq);
										return true;
									}
									if (F::AdsMgr.IsMixedActive()) {
										int nonMixedAct = F::AdsMgr.GetNonMixedAdsRemappedActivity(a2);
										if (nonMixedAct != -1 && nonMixedAct != adsAct) {
											if (nonMixedAct < ACT_PRIMARY_VM_MELEE) {
												if (G::Vars.adsLog) spdlog::info("[ADS] SendWeaponAnim: MIXED fallback -> native act={}", nonMixedAct);
												return Func.Original<FN>()(pThis, edx, nonMixedAct);
											}
											int fallbackSeq = F::AdsSupport::LookupRandomSequenceForActivity(pAnim, nonMixedAct);
											if (fallbackSeq != -1) {
												if (G::Vars.adsLog) spdlog::info("[ADS] SendWeaponAnim: MIXED fallback {} -> seq={}, SendViewModelAnim", nonMixedAct, fallbackSeq);
												pWeapon->SendViewModelAnim(fallbackSeq);
												return true;
											}
										}
									}
								}
							}
							return false;
						}
					}
				}
			}
		}
	}

	return Func.Original<FN>()(pThis, edx, a2);
}

// SequenceModify 协助:在 SetIdealActivity 中为 Necola 自定义 activity 选取随机序列
static void ApplyIdealActivityPick(C_BaseCombatWeapon* pThis,
	int act, int fallbackSeq, const char* logSuffix)
{
	C_TerrorPlayer* pLocal = I::ClientEntityList->GetClientEntity(I::EngineClient->GetLocalPlayer())->As<C_TerrorPlayer*>();
	if (!pLocal || pLocal->deadflag()) return;
	C_TerrorWeapon* pWeapon = pLocal->GetActiveWeapon()->As<C_TerrorWeapon*>();
	if (!pWeapon || pWeapon->entindex() != pThis->entindex()) return;

	int weaponID = pWeapon->GetWeaponID();
	if (weaponID == NECOLA_WEAPON_MELEE) {
		int vmIdx = pWeapon->m_iViewModelIndex();
		const model_t* model = I::ModelInfo->GetModel(vmIdx);
		if (model) {
			weaponID = G::Util.getWeaponIDWithViewModelSubtype(I::ModelInfo->GetModelName(model));
		}
	}
	if (weaponID == NECOLA_WEAPON_PISTOL && pWeapon->IsDualWielding()) {
		weaponID = NECOLA_WEAPON_PISTOL_DUAL;
	}
	if (weaponID == -1 || !G::Util.isSequenceModiferWeapon(weaponID)
			|| F::SequenceModify::ShouldSkipActivity(weaponID, act)) {
		return;
	}

	C_BaseViewModel* pViewModel = pLocal->m_hViewModel()->As<C_BaseViewModel*>();
	if (!pViewModel) return;

	float currentTime = I::GlobalVars->curtime;
	float debounceWindow = 0.15f;
	if (F::SModify.locallyChosenAct == act && F::SModify.locallyChosenSeq != -1) {
		if (F::SModify.locallyApplied
				&& F::SModify.locallyChosenWeaponEntIdx == pThis->entindex()) {
			int cachedSeqAct = pViewModel->GetSequenceActivityOffset(F::SModify.locallyChosenSeq);
			if (cachedSeqAct == act && (currentTime - F::SModify.lastPickTime) < debounceWindow) {
				return;
			}
			if (cachedSeqAct != act && G::Vars.sequenceLog) spdlog::info("[SeqMod] SetIdealActivity: DEBOUNCE-STALE-MODEL{} entindex={} cachedSeq={} expectedAct={} actualAct={} weaponID={}", logSuffix, pThis->entindex(), F::SModify.locallyChosenSeq, act, cachedSeqAct, weaponID);
		}
		if ((currentTime - F::SModify.lastPickTime) < debounceWindow) {
			if (G::Vars.sequenceLog) spdlog::info("[SeqMod] SetIdealActivity: DEBOUNCE{} entindex={} activity={} locallyChosenSeq={} locallyChosenAct={} applied={} weaponID={}", logSuffix, pThis->entindex(), act, F::SModify.locallyChosenSeq, F::SModify.locallyChosenAct, F::SModify.locallyApplied, weaponID);
			return;
		}
	}

	int newSeq = pViewModel->SelectRandomSequence(act);
	if (newSeq == -1) newSeq = fallbackSeq;
	if (G::Vars.sequenceLog) spdlog::info("[SeqMod] SetIdealActivity: PICK{} entindex={} activity={} seq={} weaponID={}", logSuffix, pThis->entindex(), act, newSeq, weaponID);
	if (newSeq == -1) return;

	F::SModify.locallyChosenSeq = newSeq;
	F::SModify.locallyChosenAct = act;
	F::SModify.locallyChosenWeaponEntIdx = pThis->entindex();
	F::SModify.lastPickTime = currentTime;
	F::SModify.locallyApplied = false;
	F::SModify.lastProcessedAnimParity = -1;
	F::SModify.animParityChosenSeq = -1;
	F::SModify.lastLayerPickServerSeq = -1;
	F::SModify.lastLayerPickAct       = -1;
	F::SModify.lastLayerPickSeq       = -1;
	F::SModify.locallyApplied = true;
	if (G::Vars.sequenceLog) spdlog::info("[SeqMod] SetIdealActivity: PICK-APPLIED{} entindex={} activity={} seq={} weaponID={}", logSuffix, pThis->entindex(), act, newSeq, weaponID);
}

bool __fastcall BaseCombatWeapon::SetIdealActivity::Detour(C_BaseCombatWeapon* pThis, void* edx, int a2)
{
	F::SModify.insideSetIdealActivityPickedAct = -1;
	F::SModify.insideSetIdealActivityPickedSeq = -1;
	F::SModify.insideSetIdealActivity = true;
	bool ret = Func.Original<FN>()(pThis, edx, a2);
	F::SModify.insideSetIdealActivity = false;
	int insidePickedAct = F::SModify.insideSetIdealActivityPickedAct;
	int insidePickedSeq = F::SModify.insideSetIdealActivityPickedSeq;
	F::SModify.insideSetIdealActivityPickedAct = -1;
	F::SModify.insideSetIdealActivityPickedSeq = -1;

	if (G::Vars.animSequenceModify) {
		if (G::Util.isNecolaActivity(a2)) {
			ApplyIdealActivityPick(pThis, a2, -1, "");
		} else if (insidePickedAct != -1 && insidePickedSeq != -1
				&& G::Util.isNecolaActivity(insidePickedAct)) {
			ApplyIdealActivityPick(pThis, insidePickedAct, insidePickedSeq, " (resolved)");
		}
	}
	return ret;
}


void BaseCombatWeapon::Init()
{
	{
		using namespace SendWeaponAnim;
		const FN pfSendWeaponAnim = reinterpret_cast<FN>(U::Offsets.m_dwSendWeaponAnim);
		if( pfSendWeaponAnim ) {
			Func.Init(pfSendWeaponAnim, &Detour);
		}
	}

	{
		using namespace SetIdealActivity;
		const FN pfSetIdealActivity = reinterpret_cast<FN>(U::Offsets.m_dwSetIdealActivity);
		if( pfSetIdealActivity ) {
			Func.Init(pfSetIdealActivity, &Detour);
		}
	}
}
