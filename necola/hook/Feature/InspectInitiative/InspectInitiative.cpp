#include "InspectInitiative.h"

#include "../AdsSupport/AdsSupport.h"
#include "../InputManager/InputManager.h"
#include "../../Vars.h"
#include "../../../sdk/utils/FeatureConfigManager.h"

#include <algorithm>

void InspectInitiative::LoadConfig(const nlohmann::json& doc) {
	const auto section = doc.find("InspectInitiative");
	if (section == doc.end() || !section->is_object()) return;
	const auto enabled = section->find("Enabled");
	if (enabled != section->end() && enabled->is_boolean()) G::Vars.openInspect = enabled->get<bool>();
	const auto key = section->find("Key");
	if (key != section->end() && key->is_number_integer()) {
		G::Vars.inspectKey = std::clamp(key->get<int>(), 1, 255);
	}
	const auto ignoreAmmo = section->find("IgnoreAmmo");
	if (ignoreAmmo != section->end() && ignoreAmmo->is_boolean()) G::Vars.inspectIgnoreAmmo = ignoreAmmo->get<bool>();
	const auto helpingHand = section->find("HelpingHandRandom");
	if (helpingHand != section->end() && helpingHand->is_number_integer()) {
		G::Vars.helpingHandRandom = std::clamp(helpingHand->get<int>(), 0, 100);
	}
}

void InspectInitiative::SaveConfig(nlohmann::json& doc) const {
	auto& section = NecolaConfig::EnsureSectionObject(doc, "InspectInitiative");
	section["Enabled"] = G::Vars.openInspect;
	section["Key"] = G::Vars.inspectKey;
	section["IgnoreAmmo"] = G::Vars.inspectIgnoreAmmo;
	section["HelpingHandRandom"] = G::Vars.helpingHandRandom;
}

void InspectInitiative::Bind() {
	if (m_bound) return;
	m_bound = true;
	m_boundKey = static_cast<std::uint32_t>(G::Vars.inspectKey);
	G::InputManagerI.AddHotkey(m_boundKey, [this]() {
		if (G::Vars.openInspect) Trigger();
	});
}

void InspectInitiative::Shutdown() {
	if (m_bound) G::InputManagerI.RemHotkey(m_boundKey);
	m_bound = false;
	m_boundKey = 0;
	Reset();
}

void InspectInitiative::Reset() {
	m_maxAmmo.clear();
}

void InspectInitiative::FrameUpdate() {
	if (!G::Vars.openInspect || !I::EngineClient || !I::ClientEntityList ||
		!I::EngineClient->IsConnected() || !I::EngineClient->IsInGame()) return;
	const int localIndex = I::EngineClient->GetLocalPlayer();
	if (localIndex <= 0) return;
	auto* localEntity = I::ClientEntityList->GetClientEntity(localIndex);
	auto* local = localEntity ? localEntity->As<C_TerrorPlayer*>() : nullptr;
	if (!local) return;
	auto* weaponEntity = local->GetActiveWeapon();
	auto* weapon = weaponEntity ? weaponEntity->As<C_TerrorWeapon*>() : nullptr;
	if (!weapon) return;
	const int entityIndex = weapon->entindex();
	const int current = weapon->m_iClip1();
	const int maxClip = weapon->GetMaxClip1();
	if (entityIndex > 0 && maxClip > 0) {
		auto& cached = m_maxAmmo[entityIndex];
		cached = std::max(cached, std::max(current, maxClip));
	}
}

void InspectInitiative::Trigger() {
	if (!G::Vars.openInspect || !I::EngineClient || !I::ClientEntityList || !I::GlobalVars ||
		!I::EngineClient->IsConnected() || !I::EngineClient->IsInGame()) return;
	const int localIndex = I::EngineClient->GetLocalPlayer();
	if (localIndex <= 0) return;
	auto* localEntity = I::ClientEntityList->GetClientEntity(localIndex);
	auto* local = localEntity ? localEntity->As<C_TerrorPlayer*>() : nullptr;
	if (!local || local->deadflag()) return;
	auto* weaponEntity = local->GetActiveWeapon();
	auto* weapon = weaponEntity ? weaponEntity->As<C_TerrorWeapon*>() : nullptr;
	if (!weapon || !local->CanAttackFull() || !weapon->CanPrimaryAttack()) return;

	if (G::Vars.enableAdsSupport && F::AdsMgr.NeedsRemapping()) {
		auto* viewModelEntity = local->m_hViewModel().Get();
		auto* viewModel = viewModelEntity ? viewModelEntity->As<C_BaseViewModel*>() : nullptr;
		auto* anim = viewModel ? viewModel->GetBaseAnimating() : nullptr;
		if (anim) {
			int inspectActivity = ACT_VM_FIDGET;
			int mixedActivity = -1;
			switch (F::AdsMgr.GetAdsLevel()) {
				case 1: inspectActivity = ACT_PRIMARY_VM_INSPECT; mixedActivity = MIXED_ACT_PRIMARY_VM_INSPECT; break;
				case 2: inspectActivity = ACT_SECONDARY_VM_INSPECT; mixedActivity = MIXED_ACT_SECONDARY_VM_INSPECT; break;
				case 3: inspectActivity = ACT_TERTIARY_VM_INSPECT; mixedActivity = MIXED_ACT_TERTIARY_VM_INSPECT; break;
				case 4: inspectActivity = ACT_FOURTH_VM_INSPECT; mixedActivity = MIXED_ACT_FOURTH_VM_INSPECT; break;
				default: break;
			}
			if (F::AdsMgr.IsMixedActive()) {
				const int mixedSeq = F::AdsSupport::LookupRandomSequenceForActivity(anim, mixedActivity);
				if (mixedSeq >= 0) {
					weapon->SendViewModelAnim(mixedSeq);
					return;
				}
			}
			const int inspectSeq = F::AdsSupport::LookupRandomSequenceForActivity(anim, inspectActivity);
			if (inspectSeq >= 0) {
				weapon->SendViewModelAnim(inspectSeq);
				return;
			}
		}
	}

	// Prefer the native fidget entry. Custom weapon packs often expose their
	// inspect sequence through the item-pickup/helping-hand layer instead.
	if (weapon->SendWeaponAnim(ACT_VM_FIDGET)) return;

	const int weaponId = weapon->GetWeaponID();
	const bool sniper = weaponId == WEAPON_HUNTING_RIFLE ||
		weaponId == WEAPON_MILITARY_SNIPER || weaponId == WEAPON_SCOUT ||
		weaponId == WEAPON_AWP;
	const bool throwable = weaponId == WEAPON_MOLOTOV || weaponId == WEAPON_VOMITJAR ||
		weaponId == WEAPON_PIPEBOMB || weaponId == WEAPON_PAINPILLS;
	const bool useHelpingHand = (std::rand() % 100) < std::clamp(G::Vars.helpingHandRandom, 0, 100);
	const int loop = throwable
		? (weaponId == WEAPON_PIPEBOMB ? ACT_VM_ITEMPICKUP_LOOP_PIPEBOMB_LAYER
			: weaponId == WEAPON_PAINPILLS ? ACT_VM_ITEMPICKUP_LOOP_PAINPILLS_LAYER
			: ACT_VM_ITEMPICKUP_LOOP_MOLOTOV_LAYER)
		: sniper ? ACT_VM_ITEMPICKUP_LOOP_SNIPER_LAYER : ACT_VM_ITEMPICKUP_LOOP_LAYER;
	const int extend = throwable
		? (weaponId == WEAPON_PIPEBOMB ? ACT_VM_ITEMPICKUP_EXTEND_PIPEBOMB_LAYER
			: weaponId == WEAPON_PAINPILLS ? ACT_VM_ITEMPICKUP_EXTEND_PAINPILLS_LAYER
			: ACT_VM_ITEMPICKUP_EXTEND_MOLOTOV_LAYER)
		: sniper ? ACT_VM_ITEMPICKUP_EXTEND_SNIPER_LAYER : ACT_VM_ITEMPICKUP_EXTEND_LAYER;
	const int helpingLoop = throwable
		? (weaponId == WEAPON_PIPEBOMB ? ACT_VM_HELPINGHAND_LOOP_PIPEBOMB_LAYER
			: weaponId == WEAPON_PAINPILLS ? ACT_VM_HELPINGHAND_LOOP_PAINPILLS_LAYER
			: ACT_VM_HELPINGHAND_LOOP_MOLOTOV_LAYER)
		: sniper ? ACT_VM_HELPINGHAND_LOOP_SNIPER_LAYER : ACT_VM_HELPINGHAND_LOOP_LAYER;
	const int helpingExtend = throwable
		? (weaponId == WEAPON_PIPEBOMB ? ACT_VM_HELPINGHAND_EXTEND_PIPEBOMB_LAYER
			: weaponId == WEAPON_PAINPILLS ? ACT_VM_HELPINGHAND_EXTEND_PAINPILLS_LAYER
			: ACT_VM_HELPINGHAND_EXTEND_MOLOTOV_LAYER)
		: sniper ? ACT_VM_HELPINGHAND_EXTEND_SNIPER_LAYER : ACT_VM_HELPINGHAND_EXTEND_LAYER;
	if (useHelpingHand && weapon->SendWeaponAnim(helpingLoop)) return;
	if (weapon->SendWeaponAnim(loop)) return;
	weapon->SendWeaponAnim(useHelpingHand ? helpingExtend : extend);
}
