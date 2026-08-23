#include "KillFeedback.h"

#include "../../../sdk/utils/FeatureConfigManager.h"
#include "../../../sdk/l4d2/interfaces/IConVar.h"
#include "../../Vars.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {
constexpr int KF_DMG_BLAST = 1 << 6;
constexpr int KF_DMG_SLASH = 1 << 2;
constexpr int KF_DMG_CLUB = 1 << 7;
constexpr int KF_HITGROUP_HEAD = 1;
constexpr float DAMAGE_RECORD_LIFETIME = 30.0f;
constexpr float SPECIAL_EVENT_GRACE = 0.15f;
constexpr int OVERLAY_THROTTLE_MS = 70;
constexpr int KILL_DURATION_MS = 240;
constexpr int HIT_DURATION_MS = 110;
constexpr uint32_t HITARM_WINDOW_MS = 400;

float SimulationTime() {
	return I::GlobalVars ? I::GlobalVars->curtime : 0.0f;
}

float PresentationTime() {
	return I::GlobalVars ? I::GlobalVars->realtime : 0.0f;
}

int LocalActiveWeaponId() {
	if (!I::EngineClient || !I::ClientEntityList) return -1;
	auto* entity = I::ClientEntityList->GetClientEntity(I::EngineClient->GetLocalPlayer());
	auto* local = entity ? entity->As<C_TerrorPlayer*>() : nullptr;
	auto* weaponEntity = local && !local->deadflag() ? local->GetActiveWeapon() : nullptr;
	auto* weapon = weaponEntity ? weaponEntity->As<C_TerrorWeapon*>() : nullptr;
	return weapon ? weapon->GetWeaponID() : -1;
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

const char* SI_THEME_IDS[] = {
	"si_cf", "si_valorant", "si_lightning", "si_love", "si_star",
	"si_bf1_advanced", "si_bf2042_advanced", "si_deltaforce_advanced",
};

const char* CI_THEME_IDS[] = {
	"ci_cf", "ci_apex", "ci_bf1", "ci_bf2042", "ci_bf5",
	"ci_cod", "ci_deltaforce", "ci_l4d2", "ci_ow",
};
}

void KillFeedbackListener::FireGameEvent(IGameEvent* event) {
	F::KillFeedbackMgr.OnGameEvent(event);
}

bool KillFeedback::UnlockCommands() {
	if (m_commandsUnlocked) return true;
	if (!I::Cvars) return false;
	// skeeto Cmd_PlaySound: one-shot unlock of r_screenoverlay / play /
	// playvol (clear FCVAR_CHEAT, add FCVAR_CLIENTCMD_CAN_EXECUTE) so that
	// IVEngineClient::ClientCmd can drive overlays and sounds on any server.
	const char* const commands[] = {"r_screenoverlay", "play", "playvol"};
	for (const char* name : commands) {
		ConCommandBase* command = I::Cvars->FindCommandBase(name);
		if (!command) {
			KFLog("unlock %s: command not found", name);
			continue;
		}
		command->m_nFlags = (command->m_nFlags & ~0x4000) | 0x40000000;
		KFLog("unlock %s ok", name);
	}
	m_commandsUnlocked = true;
	return true;
}

void KillFeedback::LoadThemes() {
	if (m_themesLoaded) return;
	m_themesLoaded = true;
	if (!I::FileSystem) {
		KFLog("theme load skipped: FileSystem unavailable");
		return;
	}
	const char* const ids[] = {
		"si_cf", "si_valorant", "si_lightning", "si_love", "si_star",
		"si_bf1_advanced", "si_bf2042_advanced", "si_deltaforce_advanced",
		"ci_cf", "ci_apex", "ci_bf1", "ci_bf2042", "ci_bf5",
		"ci_cod", "ci_deltaforce", "ci_l4d2", "ci_ow",
	};
	auto loadStyle = [](const nlohmann::json& section, KillStyle& style) {
		if (!section.is_object()) return;
		auto readStr = [&](const char* key) {
			const auto it = section.find(key);
			return it != section.end() && it->is_string() ? it->get<std::string>() : std::string();
		};
		style.overlay = readStr("overlay");
		style.sound = readStr("sound");
		const auto priority = section.find("priority");
		style.priority = priority != section.end() && priority->is_number_integer()
			? priority->get<int>() : 0;
		const auto duration = section.find("overlay_ms");
		if (duration != section.end() && duration->is_number_integer()) {
			style.duration = std::clamp(duration->get<int>(), 40, 5000);
		}
		style.enabled = !style.overlay.empty() || !style.sound.empty();
	};
	for (const char* id : ids) {
		char path[128] = {};
		_snprintf_s(path, sizeof(path), _TRUNCATE, "skeeto/skeeto_%s.json", id);
		if (!I::FileSystem->FileExists(path)) continue;
		auto handle = I::FileSystem->Open(path, "rb");
		if (!handle) continue;
		char buffer[8192] = {};
		const int size = I::FileSystem->Read(buffer, static_cast<int>(sizeof(buffer)) - 1, handle);
		I::FileSystem->Close(handle);
		if (size <= 0) continue;
		buffer[size] = '\0';
		try {
			nlohmann::json doc = nlohmann::json::parse(buffer, nullptr, false);
			if (doc.is_discarded() || !doc.is_object()) continue;
			KillTheme theme;
			theme.id = id;
			auto readStr = [&](const char* key) {
				const auto it = doc.find(key);
				return it != doc.end() && it->is_string() ? it->get<std::string>() : std::string();
			};
			theme.name = readStr("name");
			theme.channel = readStr("channel");
			const auto streak = doc.find("streak");
			if (streak != doc.end() && streak->is_object()) {
				const auto enabled = streak->find("enabled");
				theme.streakEnabled = enabled != streak->end() && enabled->is_boolean() &&
					enabled->get<bool>();
				const auto wrap = streak->find("wrap");
				theme.streakWrap = wrap != streak->end() && wrap->is_number_integer()
					? wrap->get<int>() : 0;
			}
			auto loadStyleFrom = [&](const char* key, KillStyle& style) {
				const auto it = doc.find(key);
				if (it != doc.end() && it->is_object()) loadStyle(*it, style);
			};
			loadStyleFrom("hit", theme.hit);
			loadStyleFrom("kill", theme.kill);
			loadStyleFrom("headshot", theme.headshot);
			loadStyleFrom("melee", theme.melee);
			for (int i = 1; i <= 10; ++i) {
				char key[16] = {};
				_snprintf_s(key, sizeof(key), _TRUNCATE, "streak_%d", i);
				loadStyleFrom(key, theme.streak[i]);
			}
			loadStyleFrom("streak_default", theme.streakDefault);

			// The stock SI themes share the ow overlay because the original
			// renders streak numbers via particles. Particles are not used
			// here, so substitute the dedicated full-screen materials that
			// ship in the same VPK (skeeto/si/cf/1kill..10kill/headshot/
			// knifed, skeeto/si/valorant/1kill..5kill) when present.
			if (theme.channel == "si" && theme.id.rfind("si_", 0) == 0) {
				const std::string shortName = theme.id.substr(3);
				auto prefer = [&](KillStyle& style, const char* material) {
					char probe[96] = {};
					_snprintf_s(probe, sizeof(probe), _TRUNCATE,
						"skeeto/si/%s/%s", shortName.c_str(), material);
					if (I::FileSystem->FileExists(probe)) style.overlay = probe;
				};
				for (int i = 1; i <= 10; ++i) {
					if (!theme.streak[i].enabled) continue;
					char material[16] = {};
					_snprintf_s(material, sizeof(material), _TRUNCATE, "%dkill", i);
					prefer(theme.streak[i], material);
				}
				if (theme.streakDefault.enabled) prefer(theme.streakDefault, "kill");
				if (theme.headshot.enabled) prefer(theme.headshot, "headshot");
				if (theme.melee.enabled) prefer(theme.melee, "knifed");
				if (theme.kill.enabled) prefer(theme.kill, "1kill");
			}

			theme.loaded = true;
			KFLog("theme %s loaded (%s, %s)", id, theme.channel.c_str(), theme.name.c_str());
			m_themes.push_back(std::move(theme));
		} catch (...) {
			KFLog("theme %s parse failed", id);
		}
	}
	KFLog("theme load complete count=%d", static_cast<int>(m_themes.size()));
}

const KillTheme* KillFeedback::FindTheme(const char* channel, const std::string& themeId) const {
	for (const auto& theme : m_themes) {
		if (theme.channel == channel && theme.id == themeId) return &theme;
	}
	return nullptr;
}

const std::string& KillFeedback::SelectedTheme(const char* channel) const {
	return channel[0] == 's' ? m_siTheme : m_ciTheme;
}

void KillFeedback::SetTheme(const char* channel, const std::string& themeId) {
	if (channel[0] == 's') m_siTheme = themeId;
	else m_ciTheme = themeId;
	SaveConfig();
}

bool KillFeedback::HitCheck(const KillTheme& theme, const char* eventName, bool headshot,
	bool melee, int streak, KillStyle& out) const {
	KillStyle selected;
	auto consider = [&selected](const KillStyle& candidate) {
		if (!candidate.enabled) return;
		if (!selected.enabled || candidate.priority > selected.priority) selected = candidate;
	};

	if (std::strcmp(eventName, "hit") == 0) {
		consider(theme.hit);
	} else {
		if (theme.streakEnabled && theme.streakWrap > 0) {
			int index = streak > 0 ? streak : 1;
			while (index > theme.streakWrap) index -= theme.streakWrap;
			if (index >= 1 && index <= 10 && theme.streak[index].enabled) {
				consider(theme.streak[index]);
			} else if (theme.streakDefault.enabled) {
				consider(theme.streakDefault);
			}
		}
		consider(theme.kill);
		if (melee) consider(theme.melee);
		if (headshot) consider(theme.headshot);
	}
	if (!selected.enabled) return false;
	out = selected;
	return true;
}

void KillFeedback::PlaySoundVol(const char* sound) {
	if (!sound || !*sound || !I::EngineClient) return;
	const char* relative = sound;
	if (std::strncmp(sound, "sound/", 6) == 0) relative = sound + 6;
	if (!*relative) return;
	char command[192] = {};
	_snprintf_s(command, sizeof(command), _TRUNCATE, "play %s", relative);
	I::EngineClient->ClientCmd(command);
}

void KillFeedback::RenderScreenOverlay(const KillStyle& style, int defaultDuration,
	bool allowRepeat) {
	if (!style.enabled) return;
	const std::uint32_t now = GetTickCount();
	if (!allowRepeat && now - m_fxOverlayTick < OVERLAY_THROTTLE_MS) return;
	m_fxOverlayTick = now;

	// skeeto plays the sound even when icons are disabled, gated by the same
	// throttle; icons gate only the r_screenoverlay command.
	PlaySoundVol(style.sound.c_str());

	if (!G::Vars.killFeedbackIcon || style.overlay.empty()) return;
	if (!I::EngineClient) return;
	int duration = style.duration > 0 ? style.duration : defaultDuration;
	duration = std::clamp(duration, 40, 5000);
	char command[192] = {};
	_snprintf_s(command, sizeof(command), _TRUNCATE, "r_screenoverlay %s", style.overlay.c_str());
	I::EngineClient->ClientCmd(command);
	m_fxPumpTick = now + static_cast<std::uint32_t>(duration);
	m_overlayActive = true;
	KFLog("overlay %s duration=%d", style.overlay.c_str(), duration);
}

void KillFeedback::PumpOverlay() {
	if (!m_overlayActive) return;
	if (GetTickCount() < m_fxPumpTick) return;
	m_overlayActive = false;
	if (I::EngineClient) I::EngineClient->ClientCmd("r_screenoverlay off");
	KFLog("overlay cleared");
}

bool KillFeedback::HitFeedbackCheck(const char* channel, bool kill, bool headshot,
	bool melee, int streak) {
	if (!G::Vars.killFeedbackEnabled) return false;
	if (!G::Vars.killFeedbackSound && !G::Vars.killFeedbackIcon) return false;
	if (!kill && G::Vars.killFeedbackHitMode < 2) return false;
	if (!G::Vars.killFeedbackHitOverlay) return false;
	if (!G::Vars.killFeedbackHitSpecial && channel[0] == 's') return false;
	if (!G::Vars.killFeedbackHitCommon && channel[0] == 'c') return false;
	if (I::EngineClient && (!I::EngineClient->IsConnected() || !I::EngineClient->IsInGame())) {
		return false;
	}
	LoadThemes();
	if (!UnlockCommands()) return false;
	const KillTheme* theme = FindTheme(channel, SelectedTheme(channel));
	if (!theme) return false;
	KillStyle style;
	if (!HitCheck(*theme, kill ? "kill" : "hit", headshot, melee, streak, style)) return false;
	RenderScreenOverlay(style, kill ? KILL_DURATION_MS : HIT_DURATION_MS, kill);
	return true;
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
			const SpecialVictim liveVictim = SpecialVictimFromZombieClass(player->m_zombieClass());
			if (liveVictim != SpecialVictim::Unknown) return liveVictim;
		}
	}
	const int victimUserId = event->GetInt("userid", 0);
	const auto cached = m_specialVictims.find(victimUserId);
	if (cached != m_specialVictims.end()) return cached->second;
	const auto tracked = m_playerDamage.find(victimUserId);
	if (tracked != m_playerDamage.end()) return tracked->second.victim;
	const auto pending = m_pendingSpecialKills.find(victimUserId);
	if (pending != m_pendingSpecialKills.end()) return pending->second.victim;

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
	Trigger(true, method, 0);
}

void KillFeedback::QueueSpecialKill(SpecialVictim victim, KillMethod method, int victimUserId, const char* source) {
	if (victimUserId <= 0) {
		HandleSpecialKill(victim, method, victimUserId, source);
		return;
	}
	m_pendingSpecialKills[victimUserId] = {victim, method, PresentationTime(), source};
	KFLog("special queued source=%s victim=%s userid=%d method=%s", source,
		SpecialVictimName(victim), victimUserId, MethodName(method));
}

void KillFeedback::FlushPendingSpecialKills() {
	const float now = PresentationTime();
	for (auto it = m_pendingSpecialKills.begin(); it != m_pendingSpecialKills.end();) {
		if (now - it->second.time < SPECIAL_EVENT_GRACE) {
			++it;
			continue;
		}
		const int victimUserId = it->first;
		const PendingSpecialKill pending = it->second;
		it = m_pendingSpecialKills.erase(it);
		HandleSpecialKill(pending.victim, pending.method, victimUserId, pending.source);
		m_playerDamage.erase(victimUserId);
		m_specialVictims.erase(victimUserId);
	}
}

void KillFeedback::RefreshSpecialVictims() {
	if (!G::Vars.killFeedbackEnabled || !I::GlobalVars || !I::EngineClient || !I::ClientEntityList) return;
	const float now = PresentationTime();
	if (now - m_lastVictimRefresh < 1.0f) return;
	m_lastVictimRefresh = now;

	for (int index = 1; index <= I::EngineClient->GetMaxClients(); ++index) {
		auto* entity = I::ClientEntityList->GetClientEntity(index);
		auto* player = entity ? entity->As<C_TerrorPlayer*>() : nullptr;
		if (!player) continue;
		const SpecialVictim victim = SpecialVictimFromZombieClass(player->m_zombieClass());
		if (victim == SpecialVictim::Unknown) continue;
		player_info_t info = {};
		if (!I::EngineClient->GetPlayerInfo(index, &info) || info.userid <= 0) continue;
		const auto previous = m_specialVictims.find(info.userid);
		if (previous == m_specialVictims.end() || previous->second != victim) {
			m_specialVictims[info.userid] = victim;
			KFLog("cached network scan victim=%s userid=%d index=%d", SpecialVictimName(victim),
				info.userid, index);
		}
	}
}

KillFeedback::KillMethod KillFeedback::ClassifyCommonKill(IGameEvent* event) const {
	const int weaponId = event->GetInt("weapon_id", 0);
	if (event->GetBool("blast", false) || IsExplosionWeaponId(weaponId)) {
		return KillMethod::Explosion;
	}
	if (weaponId == WEAPON_MELEE || weaponId == WEAPON_CHAINSAW) return KillMethod::Melee;
	if (event->GetBool("headshot", false)) return KillMethod::Headshot;
	const auto tracked = m_infectedDamage.find(event->GetInt("infected_id", 0));
	if (tracked != m_infectedDamage.end() && SimulationTime() - tracked->second.time <= DAMAGE_RECORD_LIFETIME) {
		return tracked->second.method;
	}
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
		const int activeWeaponId = LocalActiveWeaponId();
		if (IsExplosionWeaponId(activeWeaponId)) return KillMethod::Explosion;
		if (activeWeaponId == WEAPON_MELEE || activeWeaponId == WEAPON_CHAINSAW) return KillMethod::Melee;
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
	FlushPendingSpecialKills();

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
		const bool local = IsLocalAttacker(event, "attacker");
		if (!local) return;
		const int entityId = event->GetInt("entityid", 0);
		const bool witch = IsWitchEntity(entityId);
		const KillMethod hurtMethod = ClassifyTrackedDamage(event);
		m_infectedDamage[entityId] = {hurtMethod, SimulationTime()};
		if (witch) {
			KFLog("tracked Witch damage entity=%d method=%s", entityId, MethodName(hurtMethod));
			// Witch hits feed the special channel (HitFeedback_Check2).
			HitFeedbackCheck("si", false, hurtMethod == KillMethod::Headshot,
				hurtMethod == KillMethod::Melee, 0);
		}
		if (G::Vars.killFeedbackHitCommon) {
			HitFeedbackCheck("ci", false, hurtMethod == KillMethod::Headshot,
				hurtMethod == KillMethod::Melee, 0);
		}
		return;
	}
	if (std::strcmp(name, "melee_kill") == 0) {
		const int entityId = event->GetInt("entityid", 0);
		if (IsLocalAttacker(event, "userid") && IsWitchEntity(entityId)) {
			m_infectedDamage[entityId] = {KillMethod::Melee, SimulationTime()};
			KFLog("tracked Witch melee_kill entity=%d", entityId);
			return;
		}
		if (!G::Vars.killFeedbackCommon || !G::Vars.killFeedbackMelee ||
			!IsLocalAttacker(event, "userid")) return;
		const float now = SimulationTime();
		if (entityId > 0 && entityId == m_lastCommonEntityId && now - m_lastCommonKillTime <= 0.5f) return;
		m_lastCommonEntityId = entityId;
		m_lastCommonKillTime = now;
		KFLog("common melee accepted source=melee_kill entity=%d", entityId);
		Trigger(false, KillMethod::Melee, 0);
		return;
	}
	if (std::strcmp(name, "player_hurt") == 0) {
		const bool local = IsLocalAttacker(event, "attacker");
		if (!local) return;
		const int victimUserId = event->GetInt("userid", 0);
		const SpecialVictim victim = GetSpecialVictim(event);
		if (victim == SpecialVictim::Unknown) return;
		const KillMethod hurtMethod = ClassifySpecialKill(event);
		m_playerDamage[victimUserId] = {victim, hurtMethod, SimulationTime()};
		m_specialVictims[victimUserId] = victim;
		KFLog("tracked player_hurt victim=%s userid=%d health=%d method=%s",
			SpecialVictimName(victim), victimUserId, event->GetInt("health", -1),
			MethodName(hurtMethod));
		if (event->GetInt("health", 1) <= 0) {
			m_pendingSpecialKills.erase(victimUserId);
			HandleSpecialKill(victim, hurtMethod, victimUserId, "player_hurt");
		} else {
			// Living special infected hit (HitFeedback_Check2).
			HitFeedbackCheck("si", false, hurtMethod == KillMethod::Headshot,
				hurtMethod == KillMethod::Melee, 0);
		}
		return;
	}

	SpecialVictim dedicatedVictim = SpecialVictim::Unknown;
	const char* dedicatedSource = nullptr;
	if (std::strcmp(name, "boomer_exploded") == 0) {
		dedicatedVictim = SpecialVictim::Boomer; dedicatedSource = "boomer_exploded";
	} else if (std::strcmp(name, "charger_killed") == 0) {
		dedicatedVictim = SpecialVictim::Charger; dedicatedSource = "charger_killed";
	} else if (std::strcmp(name, "spitter_killed") == 0) {
		dedicatedVictim = SpecialVictim::Spitter; dedicatedSource = "spitter_killed";
	} else if (std::strcmp(name, "jockey_killed") == 0) {
		dedicatedVictim = SpecialVictim::Jockey; dedicatedSource = "jockey_killed";
	} else if (std::strcmp(name, "tank_killed") == 0) {
		dedicatedVictim = SpecialVictim::Tank; dedicatedSource = "tank_killed";
	}
	if (dedicatedVictim != SpecialVictim::Unknown) {
		if (!IsLocalAttacker(event, "attacker")) return;
		const int victimUserId = event->GetInt("userid", 0);
		m_specialVictims[victimUserId] = dedicatedVictim;
		KillMethod dedicatedMethod = KillMethod::Firearm;
		const auto tracked = m_playerDamage.find(victimUserId);
		if (tracked != m_playerDamage.end()) dedicatedMethod = tracked->second.method;
		if ((dedicatedVictim == SpecialVictim::Charger && event->GetBool("melee", false)) ||
			(dedicatedVictim == SpecialVictim::Tank && event->GetBool("melee_only", false))) {
			dedicatedMethod = KillMethod::Melee;
		} else if (dedicatedVictim == SpecialVictim::Jockey && event->GetString("weapon", "")[0] != '\0') {
			dedicatedMethod = ClassifySpecialKill(event);
		}
		QueueSpecialKill(dedicatedVictim, dedicatedMethod, victimUserId, dedicatedSource);
		m_playerDamage.erase(victimUserId);
		return;
	}
	KillMethod method = KillMethod::Firearm;
	if (std::strcmp(name, "infected_death") == 0) {
		const int infectedId = event->GetInt("infected_id", 0);
		const bool local = IsLocalAttacker(event, "attacker");
		KFLog("event=infected_death attacker=%d local=%d commonEnabled=%d weaponId=%d headshot=%d blast=%d",
			event->GetInt("attacker", 0), local, G::Vars.killFeedbackCommon,
			event->GetInt("weapon_id", 0), event->GetBool("headshot", false), event->GetBool("blast", false));
		if (!G::Vars.killFeedbackCommon || !local) {
			m_infectedDamage.erase(infectedId);
			return;
		}
		const float now = SimulationTime();
		if (infectedId > 0 && infectedId == m_lastCommonEntityId && now - m_lastCommonKillTime <= 0.5f) {
			m_infectedDamage.erase(infectedId);
			return;
		}
		method = ClassifyCommonKill(event);
		m_infectedDamage.erase(infectedId);
	} else if (std::strcmp(name, "player_death") == 0) {
		const bool local = IsLocalAttacker(event, "attacker");
		const SpecialVictim victim = GetSpecialVictim(event);
		KFLog("event=player_death attacker=%d local=%d specialEnabled=%d victim=%s weapon=%s headshot=%d type=%d",
			event->GetInt("attacker", 0), local, G::Vars.killFeedbackSpecial,
			SpecialVictimName(victim),
			event->GetString("weapon", ""), event->GetBool("headshot", false), event->GetInt("type", 0));
		if (!local || victim == SpecialVictim::Unknown) return;
		const int victimUserId = event->GetInt("userid", 0);
		m_pendingSpecialKills.erase(victimUserId);
		const auto tracked = m_playerDamage.find(victimUserId);
		method = tracked != m_playerDamage.end() ? tracked->second.method : ClassifySpecialKill(event);
		HandleSpecialKill(victim, method, victimUserId, "player_death");
		m_playerDamage.erase(victimUserId);
		m_specialVictims.erase(victimUserId);
		return;
	} else if (std::strcmp(name, "witch_killed") == 0) {
		const bool local = IsLocalAttacker(event, "userid");
		KFLog("event=witch_killed userid=%d local=%d specialEnabled=%d witchEnabled=%d witchid=%d meleeOnly=%d",
			event->GetInt("userid", 0), local, G::Vars.killFeedbackSpecial, G::Vars.killFeedbackWitch,
			event->GetInt("witchid", 0), event->GetBool("melee_only", false));
		if (!G::Vars.killFeedbackSpecial || !G::Vars.killFeedbackWitch || !local) return;
		method = ClassifyWitchKill(event);
		m_infectedDamage.erase(event->GetInt("witchid", 0));
		if (!IsMethodEnabled(method)) {
			KFLog("witch kill ignored: method=%s disabled", MethodName(method));
			return;
		}
		KFLog("witch kill accepted: method=%s", MethodName(method));
		Trigger(true, method, 0);
		return;
	} else {
		return;
	}

	if (!IsMethodEnabled(method)) {
		KFLog("kill ignored: method=%s disabled", MethodName(method));
		return;
	}
	KFLog("kill accepted: method=%s", MethodName(method));
	Trigger(false, method, 0);
}

void KillFeedback::Trigger(bool special, KillMethod method, int streak) {
	const bool meleeOnly = method == KillMethod::Melee && method != KillMethod::Explosion;
	const bool headshot = method == KillMethod::Headshot;
	if (method == KillMethod::Melee || method == KillMethod::Explosion) {
		if (method == KillMethod::Explosion) {
			// skeeto has no explosion styles; explosions fall through to the
			// plain kill style on both channels.
			HitFeedbackCheck("si", true, false, false, streak);
			HitFeedbackCheck("ci", true, false, false, 0);
		} else {
			HitFeedbackCheck(special ? "si" : "ci", true, false, true, streak);
		}
		KFLog("trigger method-only method=%s special=%d streak=%d", MethodName(method),
			special, m_streak);
		return;
	}

	const float now = SimulationTime();
	if (now - m_lastKillTime <= G::Vars.killFeedbackWindow) {
		m_streak = std::min(m_streak + 1, 10);
	} else {
		m_streak = 1;
	}
	m_lastKillTime = now;
	const int streakValue = std::max(1, streak > 0 ? streak : m_streak);
	KFLog("trigger streak=%d method=%s special=%d", m_streak, MethodName(method), special);
	HitFeedbackCheck("si", true, headshot, meleeOnly, streakValue);
	HitFeedbackCheck("ci", true, headshot, meleeOnly, 0);
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

KillFeedback::SpecialVictim KillFeedback::SpecialVictimFromZombieClass(int zombieClass) {
	switch (zombieClass) {
		case CLASS_SMOKER: return SpecialVictim::Smoker;
		case CLASS_BOOMER: return SpecialVictim::Boomer;
		case CLASS_HUNTER: return SpecialVictim::Hunter;
		case CLASS_SPITTER: return SpecialVictim::Spitter;
		case CLASS_JOCKEY: return SpecialVictim::Jockey;
		case CLASS_CHARGER: return SpecialVictim::Charger;
		case CLASS_TANK: return SpecialVictim::Tank;
		default: return SpecialVictim::Unknown;
	}
}

KillFeedback::SpecialVictim KillFeedback::SpecialVictimFromAbility(const char* ability) {
	if (!ability) return SpecialVictim::Unknown;
	if (std::strcmp(ability, "ability_tongue") == 0) return SpecialVictim::Smoker;
	if (std::strcmp(ability, "ability_vomit") == 0) return SpecialVictim::Boomer;
	if (std::strcmp(ability, "ability_lunge") == 0) return SpecialVictim::Hunter;
	if (std::strcmp(ability, "ability_spit") == 0) return SpecialVictim::Spitter;
	if (std::strcmp(ability, "ability_charge") == 0) return SpecialVictim::Charger;
	if (std::strcmp(ability, "ability_throw") == 0) return SpecialVictim::Tank;
	return SpecialVictim::Unknown;
}

void KillFeedback::Draw() {
	FlushPendingSpecialKills();
	PumpOverlay();
}

void KillFeedback::Stop() {
	if (m_overlayActive) {
		m_overlayActive = false;
		if (I::EngineClient) I::EngineClient->ClientCmd("r_screenoverlay off");
	}
}

void KillFeedback::Reset() {
	Stop();
	m_streak = 0;
	m_lastKillTime = -1000.0f;
	m_infectedDamage.clear();
	m_playerDamage.clear();
	m_pendingSpecialKills.clear();
	m_specialVictims.clear();
	m_lastSpecialVictimUserId = 0;
	m_lastSpecialKillTime = -1000.0f;
	m_lastCommonEntityId = 0;
	m_lastCommonKillTime = -1000.0f;
	m_lastVictimRefresh = -1000.0f;
}

bool KillFeedback::InitListeners() {
	if (!I::GameEventManager) {
		KFLog("listener init skipped: GameEventManager unavailable");
		return false;
	}
	static const char* const kEvents[] = {
		"round_start", "mission_lost", "map_transition",
		"player_spawn", "ability_use", "tank_spawn",
		"player_hurt", "player_death",
		"infected_hurt", "infected_death", "melee_kill",
		"witch_spawn", "witch_killed",
		"boomer_exploded", "charger_killed", "spitter_killed",
		"jockey_killed", "tank_killed",
	};
	bool allOk = true;
	for (const char* name : kEvents) {
		const bool added = I::GameEventManager->AddListener(&m_listener, name, false);
		KFLog("listener %s=%d", name, added ? 1 : 0);
		allOk = allOk && added;
	}
	m_listenersRegistered = true;
	KFLog("listener registration complete allOk=%d", allOk);
	return allOk;
}

void KillFeedback::Shutdown() {
	if (m_listenersRegistered && I::GameEventManager) {
		I::GameEventManager->RemoveListener(&m_listener);
		m_listenersRegistered = false;
	}
	Stop();
	KFLog("shutdown");
	Reset();
}

void KillFeedback::Preview(int kind) {
	LoadThemes();
	if (!UnlockCommands()) return;
	bool headshot = false;
	bool melee = false;
	int streak = 1;
	const char* channel = "ci";
	switch (kind) {
		case 1: headshot = true; break;
		case 2: melee = true; break;
		case 3: channel = "si"; break;
		case 4: channel = "si"; headshot = true; break;
		case 5: channel = "si"; melee = true; break;
		case 6: channel = "si"; streak = 3; break;
		case 7: channel = "si"; streak = 10; break;
		default: break;
	}
	KFLog("preview requested kind=%d", kind);
	if (!HitFeedbackCheck(channel, true, headshot, melee, streak)) {
		KFLog("preview kind=%d: no style resolved (theme missing or disabled)", kind);
	}
}

void KillFeedback::LoadConfig(const nlohmann::json& doc) {
	const auto section = doc.find("KillFeedback");
	if (section == doc.end() || !section->is_object()) return;

	auto readBool = [section](const char* key, bool fallback) {
		const auto value = section->find(key);
		return value != section->end() && value->is_boolean() ? value->get<bool>() : fallback;
	};
	auto readString = [section](const char* key, const char* fallback) {
		const auto value = section->find(key);
		return value != section->end() && value->is_string()
			? value->get<std::string>() : std::string(fallback);
	};
	G::Vars.killFeedbackEnabled = readBool("Enabled", false);
	G::Vars.killFeedbackLog = readBool("LogEnabled", false);
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
	G::Vars.killFeedbackIcon = readBool("IconEnabled", true);
	G::Vars.killFeedbackSound = readBool("SoundEnabled", true);
	G::Vars.killFeedbackFirearm = readBool("FirearmEnabled", true);
	G::Vars.killFeedbackHeadshot = readBool("HeadshotEnabled", true);
	G::Vars.killFeedbackMelee = readBool("MeleeEnabled", true);
	G::Vars.killFeedbackExplosion = readBool("ExplosionEnabled", true);
	G::Vars.killFeedbackMultiKill = readBool("MultiKillEnabled", true);
	G::Vars.killFeedbackHitMode = std::clamp(readBool("HitMode", true) ? 2 : 1, 1, 2);
	G::Vars.killFeedbackHitOverlay = readBool("HitOverlayEnabled", true);
	G::Vars.killFeedbackHitSpecial = readBool("HitSpecialEnabled", true);
	G::Vars.killFeedbackHitCommon = readBool("HitCommonEnabled", false);
	m_siTheme = readString("SiTheme", "si_cf");
	m_ciTheme = readString("CiTheme", "ci_cf");

	const auto window = section->find("MultiKillWindow");
	if (window != section->end() && window->is_number()) {
		try {
			G::Vars.killFeedbackWindow = std::clamp(
				window->get<float>(), 0.5f, 10.0f);
		} catch (...) {}
	}
	KFLog("config loaded si=%s ci=%s", m_siTheme.c_str(), m_ciTheme.c_str());
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
	section["IconEnabled"] = G::Vars.killFeedbackIcon;
	section["SoundEnabled"] = G::Vars.killFeedbackSound;
	section["FirearmEnabled"] = G::Vars.killFeedbackFirearm;
	section["HeadshotEnabled"] = G::Vars.killFeedbackHeadshot;
	section["MeleeEnabled"] = G::Vars.killFeedbackMelee;
	section["ExplosionEnabled"] = G::Vars.killFeedbackExplosion;
	section["MultiKillEnabled"] = G::Vars.killFeedbackMultiKill;
	section["HitMode"] = G::Vars.killFeedbackHitMode >= 2;
	section["HitOverlayEnabled"] = G::Vars.killFeedbackHitOverlay;
	section["HitSpecialEnabled"] = G::Vars.killFeedbackHitSpecial;
	section["HitCommonEnabled"] = G::Vars.killFeedbackHitCommon;
	section["SiTheme"] = m_siTheme;
	section["CiTheme"] = m_ciTheme;
	section["MultiKillWindow"] = G::Vars.killFeedbackWindow;
}

void KillFeedback::SaveConfig() const {
	nlohmann::json doc = NecolaConfig::LoadConfig();
	SaveConfig(doc);
	NecolaConfig::SaveConfig(doc);
	KFLog("config saved si=%s ci=%d", m_siTheme.c_str(), m_ciTheme.c_str());
}

void KillFeedback::PrintStatus() const {
	char message[512] = {};
	_snprintf_s(message, sizeof(message), _TRUNCATE,
		"enabled=%d si=%s ci=%s themes=%d streak=%d overlayActive=%d window=%.2f",
		G::Vars.killFeedbackEnabled, m_siTheme.c_str(), m_ciTheme.c_str(),
		static_cast<int>(m_themes.size()), m_streak, m_overlayActive,
		G::Vars.killFeedbackWindow);
	KFLog("status %s", message);
	if (I::Cvars) I::Cvars->ConsolePrintf("[Necola] KillFeedback %s\n", message);
}
