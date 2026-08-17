#include "KillFeedback.h"

#include "../../../sdk/utils/FeatureConfigManager.h"
#include "../../../sdk/l4d2/interfaces/IConVar.h"
#include "../../Vars.h"

#include <algorithm>
#include <cstdarg>
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

std::string KillFeedbackLogPath() {
	char exePath[MAX_PATH] = {};
	GetModuleFileNameA(nullptr, exePath, MAX_PATH);
	std::string path(exePath);
	const std::size_t slash = path.find_last_of("\\/");
	return (slash == std::string::npos ? std::string(".") : path.substr(0, slash)) +
		"\\L4N-Necola-ADS-diag.log";
}

void KFLog(const char* format, ...) {
	if (!G::Vars.killFeedbackLog || !format) return;
	char message[1024] = {};
	va_list args;
	va_start(args, format);
	_vsnprintf_s(message, sizeof(message), _TRUNCATE, format, args);
	va_end(args);

	static const std::string path = KillFeedbackLogPath();
	HANDLE file = CreateFileA(path.c_str(), FILE_APPEND_DATA,
		FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) return;
	SYSTEMTIME time = {};
	GetLocalTime(&time);
	char line[1280] = {};
	const int length = _snprintf_s(line, sizeof(line), _TRUNCATE,
		"[%04u-%02u-%02u %02u:%02u:%02u.%03u] KillFeedback: %s\r\n",
		time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
		time.wMilliseconds, message);
	if (length > 0) {
		DWORD written = 0;
		WriteFile(file, line, static_cast<DWORD>(length), &written, nullptr);
	}
	CloseHandle(file);
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
	G::Vars.killFeedbackLog = readBool("LogEnabled", true);
	G::Vars.killFeedbackCommon = readBool("CommonEnabled", true);
	G::Vars.killFeedbackSpecial = readBool("SpecialEnabled", true);
	G::Vars.killFeedbackSmoker = readBool("SmokerEnabled", true);
	G::Vars.killFeedbackBoomer = readBool("BoomerEnabled", true);
	G::Vars.killFeedbackHunter = readBool("HunterEnabled", true);
	G::Vars.killFeedbackSpitter = readBool("SpitterEnabled", true);
	G::Vars.killFeedbackJockey = readBool("JockeyEnabled", true);
	G::Vars.killFeedbackCharger = readBool("ChargerEnabled", true);
	G::Vars.killFeedbackTank = readBool("TankEnabled", true);
	G::Vars.killFeedbackWitch = readBool("WitchEnabled", true);
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
	KFLog("config loaded enabled=%d common=%d special=%d visual=%d sound=%d firearm=%d headshot=%d melee=%d explosion=%d multi=%d window=%.2f",
		G::Vars.killFeedbackEnabled, G::Vars.killFeedbackCommon, G::Vars.killFeedbackSpecial,
		G::Vars.killFeedbackVisual, G::Vars.killFeedbackSound, G::Vars.killFeedbackFirearm,
		G::Vars.killFeedbackHeadshot, G::Vars.killFeedbackMelee, G::Vars.killFeedbackExplosion,
		G::Vars.killFeedbackMultiKill, G::Vars.killFeedbackWindow);
}

void KillFeedback::SaveConfig(nlohmann::json& doc) const {
	auto& section = NecolaConfig::EnsureSectionObject(doc, "KillFeedback");
	section["Enabled"] = G::Vars.killFeedbackEnabled;
	section["LogEnabled"] = G::Vars.killFeedbackLog;
	section["CommonEnabled"] = G::Vars.killFeedbackCommon;
	section["SpecialEnabled"] = G::Vars.killFeedbackSpecial;
	section["SmokerEnabled"] = G::Vars.killFeedbackSmoker;
	section["BoomerEnabled"] = G::Vars.killFeedbackBoomer;
	section["HunterEnabled"] = G::Vars.killFeedbackHunter;
	section["SpitterEnabled"] = G::Vars.killFeedbackSpitter;
	section["JockeyEnabled"] = G::Vars.killFeedbackJockey;
	section["ChargerEnabled"] = G::Vars.killFeedbackCharger;
	section["TankEnabled"] = G::Vars.killFeedbackTank;
	section["WitchEnabled"] = G::Vars.killFeedbackWitch;
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
	KFLog("config saved enabled=%d log=%d", G::Vars.killFeedbackEnabled, G::Vars.killFeedbackLog);
}

bool KillFeedback::IsLocalAttacker(IGameEvent* event, const char* field) const {
	if (!event || !I::EngineClient) return false;
	const int attackerUserId = event->GetInt(field, 0);
	return attackerUserId > 0 &&
		I::EngineClient->GetPlayerForUserID(attackerUserId) == I::EngineClient->GetLocalPlayer();
}

KillFeedback::SpecialVictim KillFeedback::GetSpecialVictim(IGameEvent* event) const {
	if (!event || !I::EngineClient) return SpecialVictim::Unknown;
	const int victim = I::EngineClient->GetPlayerForUserID(event->GetInt("userid", 0));
	if (victim > 0 && I::ClientEntityList) {
		auto* entity = I::ClientEntityList->GetClientEntity(victim);
		auto* player = entity ? entity->As<C_TerrorPlayer*>() : nullptr;
		if (player) {
			switch (player->m_zombieClass()) {
				case CLASS_SMOKER: return SpecialVictim::Smoker;
				case CLASS_BOOMER: return SpecialVictim::Boomer;
				case CLASS_HUNTER: return SpecialVictim::Hunter;
				case CLASS_SPITTER: return SpecialVictim::Spitter;
				case CLASS_JOCKEY: return SpecialVictim::Jockey;
				case CLASS_CHARGER: return SpecialVictim::Charger;
				case CLASS_TANK: return SpecialVictim::Tank;
				default: break;
			}
		}
	}
	const auto tracked = m_playerDamage.find(event->GetInt("userid", 0));
	if (tracked != m_playerDamage.end()) return tracked->second.victim;

	const char* name = event->GetString("victimname", "");
	if (_stricmp(name, "Smoker") == 0) return SpecialVictim::Smoker;
	if (_stricmp(name, "Boomer") == 0) return SpecialVictim::Boomer;
	if (_stricmp(name, "Hunter") == 0) return SpecialVictim::Hunter;
	if (_stricmp(name, "Spitter") == 0) return SpecialVictim::Spitter;
	if (_stricmp(name, "Jockey") == 0) return SpecialVictim::Jockey;
	if (_stricmp(name, "Charger") == 0) return SpecialVictim::Charger;
	if (_stricmp(name, "Tank") == 0) return SpecialVictim::Tank;
	return SpecialVictim::Unknown;
}

bool KillFeedback::IsSpecialVictimEnabled(SpecialVictim victim) const {
	switch (victim) {
		case SpecialVictim::Smoker: return G::Vars.killFeedbackSmoker;
		case SpecialVictim::Boomer: return G::Vars.killFeedbackBoomer;
		case SpecialVictim::Hunter: return G::Vars.killFeedbackHunter;
		case SpecialVictim::Spitter: return G::Vars.killFeedbackSpitter;
		case SpecialVictim::Jockey: return G::Vars.killFeedbackJockey;
		case SpecialVictim::Charger: return G::Vars.killFeedbackCharger;
		case SpecialVictim::Tank: return G::Vars.killFeedbackTank;
		case SpecialVictim::Witch: return G::Vars.killFeedbackWitch;
		default: return false;
	}
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
	if (event->GetBool("headshot", false) || event->GetInt("hitgroup", 0) == KF_HITGROUP_HEAD) {
		return KillMethod::Headshot;
	}
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

void KillFeedback::HandleSpecialKill(SpecialVictim victim, KillMethod method, int victimUserId, const char* source) {
	const float now = SimulationTime();
	if (victimUserId > 0 && victimUserId == m_lastSpecialVictimUserId &&
		now - m_lastSpecialKillTime <= 0.5f) {
		KFLog("special duplicate ignored source=%s victim=%s userid=%d", source,
			SpecialVictimName(victim), victimUserId);
		return;
	}
	if (!G::Vars.killFeedbackSpecial || !IsSpecialVictimEnabled(victim)) {
		KFLog("special ignored source=%s victim=%s userid=%d master=%d victimEnabled=%d", source,
			SpecialVictimName(victim), victimUserId, G::Vars.killFeedbackSpecial,
			IsSpecialVictimEnabled(victim));
		return;
	}
	if (!IsMethodEnabled(method)) {
		KFLog("special ignored source=%s victim=%s method=%s disabled", source,
			SpecialVictimName(victim), MethodName(method));
		return;
	}
	m_lastSpecialVictimUserId = victimUserId;
	m_lastSpecialKillTime = now;
	KFLog("special accepted source=%s victim=%s userid=%d method=%s", source,
		SpecialVictimName(victim), victimUserId, MethodName(method));
	Trigger(method);
}

void KillFeedback::OnGameEvent(IGameEvent* event) {
	if (!event) return;
	const char* name = event->GetName();
	if (!name) return;

	if (std::strcmp(name, "round_start") == 0 || std::strcmp(name, "mission_lost") == 0 ||
		std::strcmp(name, "map_transition") == 0) {
		KFLog("state reset by event=%s", name);
		Reset();
		return;
	}

	const bool killEvent = std::strcmp(name, "infected_death") == 0 ||
		std::strcmp(name, "player_death") == 0 || std::strcmp(name, "witch_killed") == 0;
	if (!G::Vars.killFeedbackEnabled) {
		if (killEvent) KFLog("event=%s ignored: master switch disabled", name);
		return;
	}
	if (std::strcmp(name, "infected_hurt") == 0) {
		if (G::Vars.killFeedbackSpecial && IsLocalAttacker(event, "attacker")) {
			const int entityId = event->GetInt("entityid", 0);
			if (IsWitchEntity(entityId)) {
				m_infectedDamage[entityId] = {ClassifyTrackedDamage(event), SimulationTime()};
				KFLog("tracked Witch damage entity=%d method=%s type=%d hitgroup=%d", entityId,
					MethodName(m_infectedDamage[entityId].method), event->GetInt("type", 0),
					event->GetInt("hitgroup", 0));
			}
		}
		return;
	}
	if (std::strcmp(name, "player_hurt") == 0) {
		if (!G::Vars.killFeedbackSpecial || !IsLocalAttacker(event, "attacker")) return;
		const int victimUserId = event->GetInt("userid", 0);
		const SpecialVictim victim = GetSpecialVictim(event);
		if (victim == SpecialVictim::Unknown) return;
		const KillMethod hurtMethod = ClassifySpecialKill(event);
		m_playerDamage[victimUserId] = {victim, hurtMethod, SimulationTime()};
		KFLog("tracked player_hurt victim=%s userid=%d health=%d method=%s weapon=%s type=%d hitgroup=%d",
			SpecialVictimName(victim), victimUserId, event->GetInt("health", -1), MethodName(hurtMethod),
			event->GetString("weapon", ""), event->GetInt("type", 0), event->GetInt("hitgroup", 0));
		if (event->GetInt("health", 1) <= 0) {
			HandleSpecialKill(victim, hurtMethod, victimUserId, "player_hurt");
		}
		return;
	}

	SpecialVictim dedicatedVictim = SpecialVictim::Unknown;
	if (std::strcmp(name, "boomer_exploded") == 0) dedicatedVictim = SpecialVictim::Boomer;
	else if (std::strcmp(name, "charger_killed") == 0) dedicatedVictim = SpecialVictim::Charger;
	else if (std::strcmp(name, "spitter_killed") == 0) dedicatedVictim = SpecialVictim::Spitter;
	else if (std::strcmp(name, "jockey_killed") == 0) dedicatedVictim = SpecialVictim::Jockey;
	else if (std::strcmp(name, "tank_killed") == 0) dedicatedVictim = SpecialVictim::Tank;
	if (dedicatedVictim != SpecialVictim::Unknown) {
		if (!IsLocalAttacker(event, "attacker")) return;
		const int victimUserId = event->GetInt("userid", 0);
		KillMethod dedicatedMethod = KillMethod::Firearm;
		const auto tracked = m_playerDamage.find(victimUserId);
		if (tracked != m_playerDamage.end()) dedicatedMethod = tracked->second.method;
		if ((dedicatedVictim == SpecialVictim::Charger && event->GetBool("melee", false)) ||
			(dedicatedVictim == SpecialVictim::Tank && event->GetBool("melee_only", false))) {
			dedicatedMethod = KillMethod::Melee;
		} else if (dedicatedVictim == SpecialVictim::Jockey && event->GetString("weapon", "")[0] != '\0') {
			dedicatedMethod = ClassifySpecialKill(event);
		}
		HandleSpecialKill(dedicatedVictim, dedicatedMethod, victimUserId, name);
		m_playerDamage.erase(victimUserId);
		return;
	}
	KillMethod method = KillMethod::Firearm;
	if (std::strcmp(name, "infected_death") == 0) {
		m_infectedDamage.erase(event->GetInt("infected_id", 0));
		const bool local = IsLocalAttacker(event, "attacker");
		KFLog("event=infected_death attacker=%d local=%d commonEnabled=%d weaponId=%d headshot=%d blast=%d",
			event->GetInt("attacker", 0), local, G::Vars.killFeedbackCommon,
			event->GetInt("weapon_id", 0), event->GetBool("headshot", false), event->GetBool("blast", false));
		if (!G::Vars.killFeedbackCommon || !local) return;
		method = ClassifyCommonKill(event);
	} else if (std::strcmp(name, "player_death") == 0) {
		const bool local = IsLocalAttacker(event, "attacker");
		const SpecialVictim victim = GetSpecialVictim(event);
		const bool victimEnabled = IsSpecialVictimEnabled(victim);
		KFLog("event=player_death attacker=%d local=%d specialEnabled=%d victim=%s victimEnabled=%d weapon=%s headshot=%d type=%d",
			event->GetInt("attacker", 0), local, G::Vars.killFeedbackSpecial,
			SpecialVictimName(victim), victimEnabled,
			event->GetString("weapon", ""), event->GetBool("headshot", false), event->GetInt("type", 0));
		if (!local || victim == SpecialVictim::Unknown) return;
		const int victimUserId = event->GetInt("userid", 0);
		const auto tracked = m_playerDamage.find(victimUserId);
		method = tracked != m_playerDamage.end() ? tracked->second.method : ClassifySpecialKill(event);
		HandleSpecialKill(victim, method, victimUserId, "player_death");
		m_playerDamage.erase(victimUserId);
		return;
	} else if (std::strcmp(name, "witch_killed") == 0) {
		const bool local = IsLocalAttacker(event, "userid");
		KFLog("event=witch_killed userid=%d local=%d specialEnabled=%d witchEnabled=%d witchid=%d meleeOnly=%d",
			event->GetInt("userid", 0), local, G::Vars.killFeedbackSpecial, G::Vars.killFeedbackWitch,
			event->GetInt("witchid", 0), event->GetBool("melee_only", false));
		if (!G::Vars.killFeedbackSpecial || !G::Vars.killFeedbackWitch || !local) return;
		method = ClassifyWitchKill(event);
		m_infectedDamage.erase(event->GetInt("witchid", 0));
	} else {
		return;
	}

	if (!IsMethodEnabled(method)) {
		KFLog("kill ignored: method=%s disabled", MethodName(method));
		return;
	}
	KFLog("kill accepted: method=%s", MethodName(method));
	Trigger(method);
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
	KFLog("trigger streak=%d effect=%s multi=%d", m_streak, EffectName(effect), useMultiKill);
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

const char* KillFeedback::MethodName(KillMethod method) {
	switch (method) {
		case KillMethod::Firearm: return "firearm";
		case KillMethod::Headshot: return "headshot";
		case KillMethod::Melee: return "melee";
		case KillMethod::Explosion: return "explosion";
	}
	return "unknown";
}

const char* KillFeedback::SpecialVictimName(SpecialVictim victim) {
	switch (victim) {
		case SpecialVictim::Smoker: return "smoker";
		case SpecialVictim::Boomer: return "boomer";
		case SpecialVictim::Hunter: return "hunter";
		case SpecialVictim::Spitter: return "spitter";
		case SpecialVictim::Jockey: return "jockey";
		case SpecialVictim::Charger: return "charger";
		case SpecialVictim::Tank: return "tank";
		case SpecialVictim::Witch: return "witch";
		default: return "unknown";
	}
}

void KillFeedback::StartEffect(KillFeedbackEffect effect, int streakSound) {
	Stop();
	m_effect = effect;
	m_animationStart = PresentationTime();
	m_boundFrame = -1;
	m_animating = G::Vars.killFeedbackVisual;
	m_drawLogged = false;
	KFLog("effect start name=%s visual=%d sound=%d streakSound=%d", EffectName(effect),
		G::Vars.killFeedbackVisual, G::Vars.killFeedbackSound, streakSound);
	if (G::Vars.killFeedbackSound) PlayEffectSound(effect, streakSound);
}

void KillFeedback::Preview(KillFeedbackEffect effect) {
	KFLog("preview requested effect=%s", EffectName(effect));
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
	if (!I::MatSystemSurface) {
		KFLog("sound skipped sample=%s: MatSystemSurface unavailable", sample);
		return;
	}
	KFLog("sound play begin sample=%s", sample);
	I::MatSystemSurface->PlaySound(sample);
	KFLog("sound play returned sample=%s", sample);
}

bool KillFeedback::BindFrameTexture(int frame) {
	if (!I::MatSystemSurface) {
		KFLog("frame bind failed effect=%s frame=%d: MatSystemSurface unavailable", EffectName(m_effect), frame);
		return false;
	}
	if (m_textureId < 0) m_textureId = I::MatSystemSurface->CreateNewTextureID();
	if (m_textureId < 0) return false;
	char materialName[128] = {};
	_snprintf_s(materialName, sizeof(materialName), _TRUNCATE,
		"overlays/cf/%s_%03d", EffectName(m_effect), frame);
	I::MatSystemSurface->DrawSetTextureFile(m_textureId, materialName, 1, false);
	const bool valid = I::MatSystemSurface->IsTextureIDValid(m_textureId);
	if (frame == 0 || !valid) KFLog("frame bind effect=%s frame=%d textureId=%d valid=%d path=%s",
		EffectName(m_effect), frame, m_textureId, valid, materialName);
	if (!valid) return false;
	m_boundFrame = frame;
	return true;
}

void KillFeedback::Draw() {
	if (!m_animating || !G::Vars.killFeedbackVisual || !I::GlobalVars || !I::MatSystemSurface) return;
	if (!m_drawLogged) {
		KFLog("Draw entered effect=%s textureId=%d", EffectName(m_effect), m_textureId);
		m_drawLogged = true;
	}
	const int frame = static_cast<int>((PresentationTime() - m_animationStart) / FRAME_INTERVAL);
	if (frame < 0 || frame >= FRAME_COUNT) {
		KFLog("animation complete effect=%s frame=%d", EffectName(m_effect), frame);
		Stop();
		return;
	}
	if (frame != m_boundFrame && !BindFrameTexture(frame)) {
		Stop();
		return;
	}

	int screenWidth = 0;
	int screenHeight = 0;
	I::MatSystemSurface->GetScreenSize(screenWidth, screenHeight);
	if (screenWidth <= 0 || screenHeight <= 0 || m_textureId < 0) return;
	int drawWidth = std::min(screenWidth, 2048);
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

void KillFeedback::Stop() {
	if (m_animating || m_boundFrame >= 0) KFLog("effect stop name=%s frame=%d", EffectName(m_effect), m_boundFrame);
	m_animating = false;
	m_boundFrame = -1;
	m_drawLogged = false;
}

void KillFeedback::Reset() {
	Stop();
	m_streak = 0;
	m_lastKillTime = -1000.0f;
	m_infectedDamage.clear();
	m_playerDamage.clear();
	m_lastSpecialVictimUserId = 0;
	m_lastSpecialKillTime = -1000.0f;
}

void KillFeedback::Shutdown() {
	KFLog("shutdown");
	Reset();
}

void KillFeedback::PrintStatus() const {
	char message[512] = {};
	_snprintf_s(message, sizeof(message), _TRUNCATE,
		"enabled=%d log=%d common=%d special=%d visual=%d sound=%d animating=%d effect=%s frame=%d streak=%d textureId=%d window=%.2f",
		G::Vars.killFeedbackEnabled, G::Vars.killFeedbackLog, G::Vars.killFeedbackCommon,
		G::Vars.killFeedbackSpecial, G::Vars.killFeedbackVisual, G::Vars.killFeedbackSound,
		m_animating, EffectName(m_effect), m_boundFrame, m_streak, m_textureId,
		G::Vars.killFeedbackWindow);
	KFLog("status %s", message);
	if (I::Cvars) I::Cvars->ConsolePrintf("[Necola] KillFeedback %s\n", message);
}
