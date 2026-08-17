#include "KillFeedback.h"

#include "../../../sdk/utils/FeatureConfigManager.h"
#include "../../Vars.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {
constexpr int FRAME_COUNT = 85;
constexpr float FRAME_INTERVAL = 1.0f / 30.0f;
constexpr float MIN_STREAK_WINDOW = 0.5f;
constexpr float MAX_STREAK_WINDOW = 10.0f;
constexpr int KF_DMG_BLAST = 1 << 6;
constexpr int KF_DMG_SLASH = 1 << 2;
constexpr int KF_DMG_CLUB = 1 << 7;
constexpr int KF_HITGROUP_HEAD = 1;
constexpr float DAMAGE_RECORD_LIFETIME = 30.0f;

float SimulationTime() {
	return I::GlobalVars ? I::GlobalVars->curtime : 0.0f;
}

float PresentationTime() {
	return I::GlobalVars ? I::GlobalVars->realtime : 0.0f;
}

bool Contains(const char* value, const char* needle) {
	return value && needle && std::strstr(value, needle) != nullptr;
}

bool IsExplosionWeaponId(int weaponId) {
	return weaponId == WEAPON_PIPEBOMB || weaponId == WEAPON_PROPANE_TANK ||
		weaponId == WEAPON_OXYGEN_TANK || weaponId == WEAPON_GRENADE_LAUNCHER ||
		weaponId == WEAPON_FIREWORK;
}

}

void KillFeedback::LoadConfig(const nlohmann::json& doc) {
	const auto section = doc.find("KillFeedback");
	if (section == doc.end() || !section->is_object()) return;

	auto readBool = [section](const char* key, bool fallback) {
		const auto value = section->find(key);
		return value != section->end() && value->is_boolean() ? value->get<bool>() : fallback;
	};
	G::Vars.killFeedbackEnabled = readBool("Enabled", false);
	G::Vars.killFeedbackCommon = readBool("CommonEnabled", true);
	G::Vars.killFeedbackSpecial = readBool("SpecialEnabled", true);
	G::Vars.killFeedbackVisual = readBool("VisualEnabled", true);
	G::Vars.killFeedbackSound = readBool("SoundEnabled", true);
	G::Vars.killFeedbackFirearm = readBool("FirearmEnabled", true);
	G::Vars.killFeedbackHeadshot = readBool("HeadshotEnabled", true);
	G::Vars.killFeedbackMelee = readBool("MeleeEnabled", true);
	G::Vars.killFeedbackExplosion = readBool("ExplosionEnabled", true);
	G::Vars.killFeedbackMultiKill = readBool("MultiKillEnabled", true);

	const auto window = section->find("MultiKillWindow");
	if (window != section->end() && window->is_number()) {
		try {
			G::Vars.killFeedbackWindow = std::clamp(
				window->get<float>(), MIN_STREAK_WINDOW, MAX_STREAK_WINDOW);
		} catch (...) {}
	}
}

void KillFeedback::SaveConfig(nlohmann::json& doc) const {
	auto& section = NecolaConfig::EnsureSectionObject(doc, "KillFeedback");
	section["Enabled"] = G::Vars.killFeedbackEnabled;
	section["CommonEnabled"] = G::Vars.killFeedbackCommon;
	section["SpecialEnabled"] = G::Vars.killFeedbackSpecial;
	section["VisualEnabled"] = G::Vars.killFeedbackVisual;
	section["SoundEnabled"] = G::Vars.killFeedbackSound;
	section["FirearmEnabled"] = G::Vars.killFeedbackFirearm;
	section["HeadshotEnabled"] = G::Vars.killFeedbackHeadshot;
	section["MeleeEnabled"] = G::Vars.killFeedbackMelee;
	section["ExplosionEnabled"] = G::Vars.killFeedbackExplosion;
	section["MultiKillEnabled"] = G::Vars.killFeedbackMultiKill;
	section["MultiKillWindow"] = G::Vars.killFeedbackWindow;
}

void KillFeedback::SaveConfig() const {
	nlohmann::json doc = NecolaConfig::LoadConfig();
	SaveConfig(doc);
	NecolaConfig::SaveConfig(doc);
}

bool KillFeedback::IsLocalAttacker(IGameEvent* event, const char* field) const {
	if (!event || !I::EngineClient) return false;
	const int attackerUserId = event->GetInt(field, 0);
	return attackerUserId > 0 &&
		I::EngineClient->GetPlayerForUserID(attackerUserId) == I::EngineClient->GetLocalPlayer();
}

bool KillFeedback::IsSpecialVictim(IGameEvent* event) const {
	if (!event || !I::EngineClient || !I::ClientEntityList) return false;
	const int victim = I::EngineClient->GetPlayerForUserID(event->GetInt("userid", 0));
	if (victim <= 0) return false;
	auto* entity = I::ClientEntityList->GetClientEntity(victim);
	auto* player = entity ? entity->As<C_TerrorPlayer*>() : nullptr;
	return player && player->GetTeamNumber() == 3;
}

bool KillFeedback::IsActiveWeaponMelee() const {
	if (!I::EngineClient || !I::ClientEntityList) return false;
	auto* entity = I::ClientEntityList->GetClientEntity(I::EngineClient->GetLocalPlayer());
	auto* local = entity ? entity->As<C_TerrorPlayer*>() : nullptr;
	auto* weaponEntity = local && !local->deadflag() ? local->GetActiveWeapon() : nullptr;
	auto* weapon = weaponEntity ? weaponEntity->As<C_TerrorWeapon*>() : nullptr;
	if (!weapon) return false;
	const int weaponId = weapon->GetWeaponID();
	return weaponId == WEAPON_MELEE || weaponId == WEAPON_CHAINSAW;
}

bool KillFeedback::IsActiveWeaponExplosion() const {
	if (!I::EngineClient || !I::ClientEntityList) return false;
	auto* entity = I::ClientEntityList->GetClientEntity(I::EngineClient->GetLocalPlayer());
	auto* local = entity ? entity->As<C_TerrorPlayer*>() : nullptr;
	auto* weaponEntity = local && !local->deadflag() ? local->GetActiveWeapon() : nullptr;
	auto* weapon = weaponEntity ? weaponEntity->As<C_TerrorWeapon*>() : nullptr;
	return weapon && IsExplosionWeaponId(weapon->GetWeaponID());
}

KillFeedback::KillMethod KillFeedback::ClassifyCommonKill(IGameEvent* event) const {
	const int weaponId = event->GetInt("weapon_id", 0);
	if (event->GetBool("blast", false) || IsExplosionWeaponId(weaponId)) {
		return KillMethod::Explosion;
	}
	if (weaponId == WEAPON_MELEE || weaponId == WEAPON_CHAINSAW) return KillMethod::Melee;
	if (event->GetBool("headshot", false)) return KillMethod::Headshot;
	if (weaponId == 0 && IsActiveWeaponMelee()) return KillMethod::Melee;
	return KillMethod::Firearm;
}

KillFeedback::KillMethod KillFeedback::ClassifySpecialKill(IGameEvent* event) const {
	const char* weapon = event->GetString("weapon", "");
	const int damageType = event->GetInt("type", 0);
	if ((damageType & KF_DMG_BLAST) != 0 || Contains(weapon, "pipe_bomb") ||
		Contains(weapon, "grenade_launcher") || Contains(weapon, "propane") ||
		Contains(weapon, "oxygen") || Contains(weapon, "firework")) {
		return KillMethod::Explosion;
	}
	if (Contains(weapon, "melee") || Contains(weapon, "chainsaw")) {
		return KillMethod::Melee;
	}
	if (event->GetBool("headshot", false)) return KillMethod::Headshot;
	if (!weapon || weapon[0] == '\0') {
		if (IsActiveWeaponExplosion()) return KillMethod::Explosion;
		if (IsActiveWeaponMelee()) return KillMethod::Melee;
	}
	return KillMethod::Firearm;
}

KillFeedback::KillMethod KillFeedback::ClassifyWitchKill(IGameEvent* event) const {
	if (event->GetBool("melee_only", false)) return KillMethod::Melee;
	const auto tracked = m_infectedDamage.find(event->GetInt("witchid", 0));
	if (tracked != m_infectedDamage.end() && SimulationTime() - tracked->second.time <= DAMAGE_RECORD_LIFETIME) {
		return tracked->second.method;
	}
	return KillMethod::Firearm;
}

KillFeedback::KillMethod KillFeedback::ClassifyTrackedDamage(IGameEvent* event) const {
	const int damageType = event->GetInt("type", 0);
	if ((damageType & KF_DMG_BLAST) != 0) return KillMethod::Explosion;
	if ((damageType & (KF_DMG_SLASH | KF_DMG_CLUB)) != 0) return KillMethod::Melee;
	if (event->GetInt("hitgroup", 0) == KF_HITGROUP_HEAD) return KillMethod::Headshot;
	return KillMethod::Firearm;
}

bool KillFeedback::IsMethodEnabled(KillMethod method) const {
	switch (method) {
		case KillMethod::Firearm: return G::Vars.killFeedbackFirearm;
		case KillMethod::Headshot: return G::Vars.killFeedbackHeadshot;
		case KillMethod::Melee: return G::Vars.killFeedbackMelee;
		case KillMethod::Explosion: return G::Vars.killFeedbackExplosion;
	}
	return false;
}

bool KillFeedback::IsWitchEntity(int entityId) const {
	if (entityId <= 0 || !I::ClientEntityList) return false;
	auto* entity = I::ClientEntityList->GetClientEntity(entityId);
	auto* clientClass = entity ? entity->GetClientClass() : nullptr;
	return clientClass && Contains(clientClass->m_pNetworkName, "Witch");
}

void KillFeedback::OnGameEvent(IGameEvent* event) {
	if (!event) return;
	const char* name = event->GetName();
	if (!name) return;

	if (std::strcmp(name, "round_start") == 0 || std::strcmp(name, "mission_lost") == 0 ||
		std::strcmp(name, "map_transition") == 0) {
		Reset();
		return;
	}

	if (!G::Vars.killFeedbackEnabled) return;
	if (std::strcmp(name, "infected_hurt") == 0) {
		if (G::Vars.killFeedbackSpecial && IsLocalAttacker(event, "attacker")) {
			const int entityId = event->GetInt("entityid", 0);
			if (IsWitchEntity(entityId)) {
				m_infectedDamage[entityId] = {ClassifyTrackedDamage(event), SimulationTime()};
			}
		}
		return;
	}
	KillMethod method = KillMethod::Firearm;
	if (std::strcmp(name, "infected_death") == 0) {
		m_infectedDamage.erase(event->GetInt("infected_id", 0));
		if (!G::Vars.killFeedbackCommon || !IsLocalAttacker(event, "attacker")) return;
		method = ClassifyCommonKill(event);
	} else if (std::strcmp(name, "player_death") == 0) {
		if (!G::Vars.killFeedbackSpecial || !IsLocalAttacker(event, "attacker") || !IsSpecialVictim(event)) return;
		method = ClassifySpecialKill(event);
	} else if (std::strcmp(name, "witch_killed") == 0) {
		if (!G::Vars.killFeedbackSpecial || !IsLocalAttacker(event, "userid")) return;
		method = ClassifyWitchKill(event);
		m_infectedDamage.erase(event->GetInt("witchid", 0));
	} else {
		return;
	}

	if (IsMethodEnabled(method)) Trigger(method);
}

void KillFeedback::Trigger(KillMethod method) {
	const float now = SimulationTime();
	if (now - m_lastKillTime <= G::Vars.killFeedbackWindow) {
		m_streak = std::min(m_streak + 1, 10);
	} else {
		m_streak = 1;
	}
	m_lastKillTime = now;

	KillFeedbackEffect effect = KillFeedbackEffect::Kill1;
	const bool useMultiKill = G::Vars.killFeedbackMultiKill && m_streak >= 2;
	if (useMultiKill) {
		switch (std::min(m_streak, 6)) {
			case 2: effect = KillFeedbackEffect::Kill2; break;
			case 3: effect = KillFeedbackEffect::Kill3; break;
			case 4: effect = KillFeedbackEffect::Kill4; break;
			case 5: effect = KillFeedbackEffect::Kill5; break;
			default: effect = KillFeedbackEffect::Kill6; break;
		}
	} else {
		switch (method) {
			case KillMethod::Headshot: effect = KillFeedbackEffect::Headshot; break;
			case KillMethod::Melee: effect = KillFeedbackEffect::Melee; break;
			case KillMethod::Explosion: effect = KillFeedbackEffect::Explosion; break;
			default: effect = KillFeedbackEffect::Kill1; break;
		}
	}
	StartEffect(effect, useMultiKill ? m_streak : 1);
}

const char* KillFeedback::EffectName(KillFeedbackEffect effect) {
	switch (effect) {
		case KillFeedbackEffect::Kill1: return "1kill";
		case KillFeedbackEffect::Kill2: return "2kill";
		case KillFeedbackEffect::Kill3: return "3kill";
		case KillFeedbackEffect::Kill4: return "4kill";
		case KillFeedbackEffect::Kill5: return "5kill";
		case KillFeedbackEffect::Kill6: return "6kill";
		case KillFeedbackEffect::Headshot: return "headshot";
		case KillFeedbackEffect::Melee: return "meleekill";
		case KillFeedbackEffect::Explosion: return "boom";
	}
	return "1kill";
}

int KillFeedback::EffectStreak(KillFeedbackEffect effect) {
	switch (effect) {
		case KillFeedbackEffect::Kill2: return 2;
		case KillFeedbackEffect::Kill3: return 3;
		case KillFeedbackEffect::Kill4: return 4;
		case KillFeedbackEffect::Kill5: return 5;
		case KillFeedbackEffect::Kill6: return 6;
		default: return 1;
	}
}

void KillFeedback::StartEffect(KillFeedbackEffect effect, int streakSound) {
	Stop();
	m_effect = effect;
	m_animationStart = PresentationTime();
	m_boundFrame = -1;
	m_animating = G::Vars.killFeedbackVisual;
	if (G::Vars.killFeedbackSound) PlayEffectSound(effect, streakSound);
}

void KillFeedback::Preview(KillFeedbackEffect effect) {
	StartEffect(effect, EffectStreak(effect));
}

void KillFeedback::PlayEffectSound(KillFeedbackEffect effect, int streakSound) const {
	char sample[64] = {};
	if (streakSound >= 2) {
		_snprintf_s(sample, sizeof(sample), _TRUNCATE, "cf/multikill_%d.mp3", std::min(streakSound, 10));
	} else if (effect == KillFeedbackEffect::Headshot) {
		strcpy_s(sample, "cf/headshot.mp3");
	} else if (effect == KillFeedbackEffect::Explosion) {
		strcpy_s(sample, "cf/grenadekill.mp3");
	} else {
		strcpy_s(sample, "cf/kill.mp3");
	}
	if (I::MatSystemSurface) I::MatSystemSurface->PlaySound(sample);
}

bool KillFeedback::BindFrameMaterial(int frame) {
	if (!I::MaterialSystem || !I::MatSystemSurface) return false;
	char materialName[128] = {};
	_snprintf_s(materialName, sizeof(materialName), _TRUNCATE,
		"overlays/cf/%s_%03d", EffectName(m_effect), frame);
	IMaterial* material = I::MaterialSystem->FindMaterial(materialName, TEXTURE_GROUP_VGUI, false);
	if (!material || material->IsErrorMaterial()) return false;

	material->IncrementReferenceCount();
	if (m_textureId < 0) m_textureId = I::MatSystemSurface->CreateNewTextureID();
	I::MatSystemSurface->DrawSetTextureMaterial(m_textureId, material);
	ReleaseMaterial();
	m_material = material;
	m_boundFrame = frame;
	return true;
}

void KillFeedback::Draw() {
	if (!m_animating || !G::Vars.killFeedbackVisual || !I::GlobalVars || !I::MatSystemSurface) return;
	const int frame = static_cast<int>((PresentationTime() - m_animationStart) / FRAME_INTERVAL);
	if (frame < 0 || frame >= FRAME_COUNT) {
		Stop();
		return;
	}
	if (frame != m_boundFrame && !BindFrameMaterial(frame)) {
		Stop();
		return;
	}

	int screenWidth = 0;
	int screenHeight = 0;
	I::MatSystemSurface->GetScreenSize(screenWidth, screenHeight);
	if (screenWidth <= 0 || screenHeight <= 0 || m_textureId < 0) return;
	int drawWidth = screenWidth;
	int drawHeight = drawWidth / 2;
	if (drawHeight > screenHeight) {
		drawHeight = screenHeight;
		drawWidth = drawHeight * 2;
	}
	const int x = (screenWidth - drawWidth) / 2;
	const int y = (screenHeight - drawHeight) / 2;
	I::MatSystemSurface->DrawSetColor(255, 255, 255, 255);
	I::MatSystemSurface->DrawSetTexture(m_textureId);
	I::MatSystemSurface->DrawTexturedRect(x, y, x + drawWidth, y + drawHeight);
}

void KillFeedback::ReleaseMaterial() {
	if (m_material) m_material->DecrementReferenceCount();
	m_material = nullptr;
}

void KillFeedback::Stop() {
	m_animating = false;
	m_boundFrame = -1;
	ReleaseMaterial();
	if (m_textureId >= 0 && I::MatSystemSurface) {
		I::MatSystemSurface->DeleteTextureByID(m_textureId);
	}
	m_textureId = -1;
}

void KillFeedback::Reset() {
	Stop();
	m_streak = 0;
	m_lastKillTime = -1000.0f;
	m_infectedDamage.clear();
}

void KillFeedback::Shutdown() {
	Reset();
}
