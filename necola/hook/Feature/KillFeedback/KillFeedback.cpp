#include "KillFeedback.h"

#include "../../../sdk/utils/FeatureConfigManager.h"
#include "../../../sdk/l4d2/interfaces/IConVar.h"
#include "../../../sdk/l4d2/interfaces/EngineTrace.h"
#include "../../Vars.h"
#include "../../../diag.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {
constexpr int KF_DMG_BLAST = 1 << 6;
constexpr int KF_DMG_SLASH = 1 << 2;
constexpr int KF_DMG_CLUB = 1 << 7;
constexpr int KF_HITGROUP_HEAD = 1;
constexpr float DAMAGE_RECORD_LIFETIME = 1.0f;
constexpr float SPECIAL_EVENT_GRACE = 0.15f;
constexpr int OVERLAY_THROTTLE_MS = 70;
constexpr int KILL_DURATION_MS = 240;
constexpr int HIT_DURATION_MS = 110;
constexpr uint32_t HITARM_WINDOW_MS = 400;
constexpr unsigned int COMMON_HIT_TRACE_MASK = 1174421507u;
int g_killEventDiagCount = 0;

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

bool ContainsNoCase(const char* value, const char* needle) {
	if (!value || !needle) return false;
	const std::size_t length = std::strlen(needle);
	for (std::size_t i = 0; value[i]; ++i) {
		if (_strnicmp(value + i, needle, length) == 0) return true;
	}
	return false;
}

bool IsMeleeWeaponName(const char* weapon) {
	if (!weapon || !*weapon) return false;
	static const char* const names[] = {
		"melee", "chainsaw", "katana", "fireaxe", "machete", "crowbar",
		"cricket_bat", "baseball_bat", "electric_guitar", "frying_pan",
		"golfclub", "pitchfork", "shovel", "tonfa", "knife", "riotshield",
	};
	for (const char* name : names) {
		if (ContainsNoCase(weapon, name)) return true;
	}
	return false;
}

bool IsExplosionWeaponId(int weaponId) {
	return weaponId == WEAPON_PIPEBOMB || weaponId == WEAPON_PROPANE_TANK ||
		weaponId == WEAPON_OXYGEN_TANK || weaponId == WEAPON_GRENADE_LAUNCHER ||
		weaponId == WEAPON_FIREWORK;
}

class LocalHitTraceFilter final : public ITraceFilter {
public:
	explicit LocalHitTraceFilter(C_BaseEntity* local) : m_local(local) {}
	bool ShouldHitEntity(C_BaseEntity* entity, int) override { return entity && entity != m_local; }
	TraceType_t GetTraceType() const override { return TRACE_EVERYTHING; }
private:
	C_BaseEntity* m_local = nullptr;
};

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

void KillFeedbackListener::FireGameEvent(IGameEvent* event) {
	F::KillFeedbackMgr.OnGameEvent(event);
}

bool KillFeedback::UnlockCommands(bool needOverlay) {
	if (m_commandsUnlocked) return true;
	if (!needOverlay) return true;
	if (!I::Cvars) return false;
	const char* const commands[] = {"r_screenoverlay"};
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
	if (!I::FileSystem) {
		KFLog("theme load skipped: FileSystem unavailable");
		return;
	}
	const float now = PresentationTime();
	if (now - m_lastThemeLoadAttempt < 2.0f) return;
	m_lastThemeLoadAttempt = now;
	m_themes.clear();
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
		style.particle = readStr("particle");
		const auto particles = section.find("particles");
		if (particles != section.end() && particles->is_array()) {
			for (const auto& value : *particles) {
				if (value.is_string() && !value.get<std::string>().empty()) {
					style.particles.push_back(value.get<std::string>());
				}
			}
		}
		style.sound = readStr("sound");
		const auto sounds = section.find("sounds");
		if (sounds != section.end() && sounds->is_array()) {
			for (const auto& value : *sounds) {
				if (value.is_string() && !value.get<std::string>().empty()) {
					style.sounds.push_back(value.get<std::string>());
				}
			}
		}
		const auto priority = section.find("priority");
		style.priority = priority != section.end() && priority->is_number_integer()
			? priority->get<int>() : 0;
		for (const char* key : {"overlay_ms", "duration", "overlay_duration"}) {
			const auto duration = section.find(key);
			if (duration != section.end() && duration->is_number_integer()) {
				style.duration = std::clamp(duration->get<int>(), 0, 5000);
				break;
			}
		}
		const auto world = section.find("world");
		style.world = world != section.end() && world->is_boolean() && world->get<bool>();
		style.enabled = !style.overlay.empty() || !style.particle.empty() ||
			!style.particles.empty() || !style.sound.empty() || !style.sounds.empty();
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
				const auto priority = streak->find("priority");
				theme.streakPriority = priority != streak->end() && priority->is_number_integer()
					? priority->get<int>() : 10;
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
				if (theme.streak[i].enabled && theme.streak[i].priority == 0) {
					theme.streak[i].priority = theme.streakPriority != 0 ? theme.streakPriority : 10;
				}
			}
			loadStyleFrom("streak_default", theme.streakDefault);

			theme.loaded = true;
			KFLog("theme %s loaded (%s, %s)", id, theme.channel.c_str(), theme.name.c_str());
			m_themes.push_back(std::move(theme));
		} catch (...) {
			KFLog("theme %s parse failed", id);
		}
	}
	m_themesLoaded = m_themes.size() >= 17;
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
	if (channel[0] == 's') {
		m_siTheme = themeId;
		m_streak = 0;
	}
	else m_ciTheme = themeId;
	m_themeFailureReported = false;
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
		bool initialSelected = false;
		if (theme.streakEnabled && streak > 0) {
			int index = streak > 0 ? streak : 1;
			if (theme.streakWrap > 0) {
				while (index > theme.streakWrap) index -= theme.streakWrap;
			}
			if (index >= 1 && index <= 10 && theme.streak[index].enabled) {
				consider(theme.streak[index]);
				initialSelected = true;
			} else if (theme.streakDefault.enabled) {
				consider(theme.streakDefault);
				initialSelected = true;
			}
		}
		if (!initialSelected) consider(theme.kill);
		if (melee) consider(theme.melee);
		if (headshot) consider(theme.headshot);
	}
	if (!selected.enabled) return false;
	out = selected;
	return true;
}

void KillFeedback::PlaySoundVol(const KillStyle& style) {
	if (!G::Vars.killFeedbackSound) return;
	if (G::Vars.killFeedbackSoundVolume <= 0) return;
	const std::size_t count = style.sounds.size() + (style.sound.empty() ? 0u : 1u);
	if (count == 0) return;
	const std::size_t index = (static_cast<std::size_t>(GetTickCount()) +
		static_cast<std::size_t>(m_streak * 31)) % count;
	const std::string* selected = index < style.sounds.size()
		? &style.sounds[index] : &style.sound;
	const char* relative = selected->c_str();
	if (std::strncmp(relative, "sound/", 6) == 0) relative += 6;
	if (!*relative) return;
	const float volume = std::clamp(G::Vars.killFeedbackSoundVolume, 0, 100) / 100.0f;
	// Keep the default path identical to the original inspect/feedback build:
	// VGUI plays a local UI sample without touching the game's sound-script
	// cache. EngineSound is used only when an explicit volume is requested.
	if (volume >= 0.999f && I::MatSystemSurface) {
		I::MatSystemSurface->PlaySound(relative);
		return;
	}
	if (I::EngineSound) {
		I::EngineSound->EmitAmbientSound(relative, volume, SNDLVL_NORM, 0, 0.0f);
		return;
	}
	if (!I::EngineClient) return;
	char command[192] = {};
	_snprintf_s(command, sizeof(command), _TRUNCATE, "playvol %s %.2f", relative, volume);
	I::EngineClient->ClientCmd(command);
}

void KillFeedback::PrecacheParticles(const KillStyle& style) {
	if (!U::Offsets.m_dwPrecacheParticleSystem) return;
	using Fn = int(__cdecl*)(const char*);
	const Fn precache = reinterpret_cast<Fn>(U::Offsets.m_dwPrecacheParticleSystem);
	auto one = [&](const std::string& name) {
		if (name.empty() || m_precachedParticles.contains(name)) return;
		MEMORY_BASIC_INFORMATION info = {};
		if (!VirtualQuery(reinterpret_cast<const void*>(precache), &info, sizeof(info)) ||
			(info.Protect & 0xF0) == 0) return;
		precache(name.c_str());
		m_precachedParticles.insert(name);
		if (U::Offsets.m_dwDispatchParticleEffect3) {
			using DispatchFn = void(__cdecl*)(const char*, Vector*, Vector*, int, int, int);
			const DispatchFn dispatch = reinterpret_cast<DispatchFn>(U::Offsets.m_dwDispatchParticleEffect3);
			MEMORY_BASIC_INFORMATION dispatchInfo = {};
			if (VirtualQuery(reinterpret_cast<const void*>(dispatch), &dispatchInfo, sizeof(dispatchInfo)) &&
				(dispatchInfo.Protect & 0xF0) != 0) {
				Vector hidden(-8192.0f, -8192.0f, -8192.0f);
				Vector angles;
				dispatch(name.c_str(), &hidden, &angles, 2, 0, 0);
			}
		}
		KFLog("particle precached %s", name.c_str());
	};
	one(style.particle);
	for (const auto& name : style.particles) one(name);
}

void KillFeedback::SpawnParticles(const KillStyle& style) {
	if (!G::Vars.killFeedbackIcon || !U::Offsets.m_dwDispatchParticleEffect3) return;
	PrecacheParticles(style);
	using Fn = void(__cdecl*)(const char*, Vector*, Vector*, int, int, int);
	const Fn dispatch = reinterpret_cast<Fn>(U::Offsets.m_dwDispatchParticleEffect3);
	MEMORY_BASIC_INFORMATION info = {};
	if (!VirtualQuery(reinterpret_cast<const void*>(dispatch), &info, sizeof(info)) ||
		(info.Protect & 0xF0) == 0) return;
	Vector origin;
	if (style.world && m_feedbackPosition.valid) origin = m_feedbackPosition.value;
	Vector angles;
	auto one = [&](const std::string& name) {
		if (name.empty()) return;
		dispatch(name.c_str(), &origin, &angles, 2, 0, 0);
		KFLog("particle spawned %s world=%d at %.0f %.0f %.0f", name.c_str(), style.world,
			origin.x, origin.y, origin.z);
	};
	one(style.particle);
	for (const auto& name : style.particles) one(name);
}

void KillFeedback::CaptureFeedbackPosition(IGameEvent* event, int entityIndex) {
	m_feedbackPosition = {};
	if (event) {
		const float x = event->GetFloat("victim_x", 0.0f);
		const float y = event->GetFloat("victim_y", 0.0f);
		const float z = event->GetFloat("victim_z", 0.0f);
		if (x != 0.0f || y != 0.0f || z != 0.0f) {
			m_feedbackPosition.value = Vector(x, y, z);
			m_feedbackPosition.valid = true;
			return;
		}
	}
	if (entityIndex > 0 && I::ClientEntityList) {
		if (auto* entity = I::ClientEntityList->GetClientEntity(entityIndex)) {
			m_feedbackPosition.value = entity->GetAbsOrigin();
			m_feedbackPosition.valid = true;
			return;
		}
	}
	if (m_lastImpactPosition.valid) m_feedbackPosition = m_lastImpactPosition;
}

void KillFeedback::PumpCommonHitTrace() {
	if (!m_pendingImpactPosition.valid || GetTickCount() - m_pendingImpactTick < 45) return;
	const FeedbackPosition impact = m_pendingImpactPosition;
	m_pendingImpactPosition = {};
	if (G::Vars.killFeedbackHitMode != 2 || !I::EngineTrace || !I::EngineClient ||
		!I::ClientEntityList || GetTickCount() - m_lastCommonTraceTick < 40) return;
	m_lastCommonTraceTick = GetTickCount();
	auto* localEntity = I::ClientEntityList->GetClientEntity(I::EngineClient->GetLocalPlayer());
	auto* local = localEntity ? localEntity->As<C_TerrorPlayer*>() : nullptr;
	if (!local) return;
	const Vector start = local->m_vecOrigin() + local->m_vecViewOffset();
	Vector direction = impact.value - start;
	if (direction.Normalize() <= 0.0f) return;
	const Vector end = impact.value + direction * 2.0f;
	Ray_t ray(start, end);
	trace_t trace = {};
	LocalHitTraceFilter filter(localEntity->As<C_BaseEntity*>());
	I::EngineTrace->TraceRay(ray, COMMON_HIT_TRACE_MASK, &filter, &trace);
	if (!trace.m_pEnt) return;
	auto* clientClass = trace.m_pEnt->GetClientClass();
	const char* className = clientClass ? clientClass->m_pNetworkName : nullptr;
	if (!className || (!ContainsNoCase(className, "infected") &&
		!ContainsNoCase(className, "witch"))) return;
	m_feedbackPosition.value = impact.value;
	m_feedbackPosition.valid = true;
	ResolveCombinedFeedback(false, false, false, 0, false, true);
}

void KillFeedback::RenderScreenOverlay(const KillStyle& style, int defaultDuration,
	bool allowRepeat) {
	if (!style.enabled) return;
	const std::uint32_t now = GetTickCount();
	if (!allowRepeat && now - m_fxOverlayTick < OVERLAY_THROTTLE_MS) return;
	m_fxOverlayTick = now;

	// skeeto plays the sound even when icons are disabled, gated by the same
	// throttle; icons gate only the r_screenoverlay command.
	PlaySoundVol(style);

	if (G::Vars.killFeedbackIcon) SpawnParticles(style);
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
	if (I::EngineClient) I::EngineClient->ClientCmd("r_screenoverlay \"\"");
	KFLog("overlay cleared");
}

bool KillFeedback::ResolveCombinedFeedback(bool kill, bool headshot, bool melee,
	int streak, bool allowSi, bool allowCi) {
	if (kill && !G::Vars.killFeedbackEnabled) return false;
	if (!G::Vars.killFeedbackSound && !G::Vars.killFeedbackIcon) return false;
	if (!kill && G::Vars.killFeedbackHitMode == 0) return false;
	if (!kill && G::Vars.killFeedbackHitMode == 1 && !allowSi) return false;
	if (!I::EngineClient || !I::EngineClient->IsConnected() || !I::EngineClient->IsInGame()) return false;

	LoadThemes();
	if (!UnlockCommands(G::Vars.killFeedbackIcon)) return false;
	KillStyle siStyle;
	KillStyle ciStyle;
	bool siValid = false;
	bool ciValid = false;
	const bool anySelected = (allowSi && m_siTheme != "off") ||
		(allowCi && m_ciTheme != "off");
	if (!anySelected) return false;
	if (allowSi && m_siTheme != "off") {
		if (const KillTheme* theme = FindTheme("si", m_siTheme)) {
			siValid = HitCheck(*theme, kill ? "kill" : "hit", headshot, melee, streak, siStyle);
		}
	}
	if (allowCi && m_ciTheme != "off") {
		if (const KillTheme* theme = FindTheme("ci", m_ciTheme)) {
			// Original KillFeed_Track suppresses CI melee when the same kill is a headshot.
			ciValid = HitCheck(*theme, kill ? "kill" : "hit", headshot,
				melee && !headshot, 0, ciStyle);
		}
	}
	if (!siValid && !ciValid) {
		if (!m_themeFailureReported && I::Cvars) {
			m_themeFailureReported = true;
			I::Cvars->ConsolePrintf("[Necola] KillFeedback: no SI/CI style resolved. Check skeeto_killfeed.vpk and theme settings.\n");
		}
		return false;
	}
	m_themeFailureReported = false;

	// Netvar_SnapshotCopy: choose visual base by si_dedicated; then choose the
	// scalar sound independently by si_sound preference with fallback.
	KillStyle output;
	if (G::Vars.killFeedbackSiDedicated && siValid) output = siStyle;
	else if (ciValid) output = ciStyle;
	else output = siStyle;

	const std::string* preferredSound = nullptr;
	if (G::Vars.killFeedbackSiSound && siValid && !siStyle.sound.empty()) {
		preferredSound = &siStyle.sound;
	} else if (ciValid && !ciStyle.sound.empty()) {
		preferredSound = &ciStyle.sound;
	} else if (siValid && !siStyle.sound.empty()) {
		preferredSound = &siStyle.sound;
	}
	output.sound = preferredSound ? *preferredSound : std::string();
	output.enabled = !output.overlay.empty() || !output.particle.empty() ||
		!output.particles.empty() || !output.sound.empty() || !output.sounds.empty();
	KFLog("combined kill=%d si=%d ci=%d siVisual=%d siSound=%d overlay=%s particle=%s sound=%s",
		kill, siValid, ciValid, G::Vars.killFeedbackSiDedicated,
		G::Vars.killFeedbackSiSound, output.overlay.c_str(), output.particle.c_str(), output.sound.c_str());
	RenderScreenOverlay(output, kill ? KILL_DURATION_MS : HIT_DURATION_MS, kill);
	return true;
}

void KillFeedback::OnSpecialHealthDrop(const Vector& position) {
	if (G::Vars.killFeedbackHitMode == 0) return;
	if (m_pendingSpecialHits.size() >= 32) m_pendingSpecialHits.erase(m_pendingSpecialHits.begin());
	m_pendingSpecialHits.push_back({position, true});
}

bool KillFeedback::IsLocalAttacker(IGameEvent* event, const char* field) const {
	if (!event || !I::EngineClient) return false;
	const int local = I::EngineClient->GetLocalPlayer();
	if (local <= 0) return false;
	const int attackerUserId = event->GetInt(field, 0);
	if (attackerUserId > 0 && I::EngineClient->GetPlayerForUserID(attackerUserId) == local) return true;
	return std::strcmp(field, "attacker") == 0 && event->GetInt("attackerentid", 0) == local;
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
	const int entityId = event->GetInt("entityid", 0);
	if (entityId > 0 && I::ClientEntityList) {
		auto* entity = I::ClientEntityList->GetClientEntity(entityId);
		auto* player = entity ? entity->As<C_TerrorPlayer*>() : nullptr;
		if (player) {
			const SpecialVictim entityVictim = SpecialVictimFromZombieClass(player->m_zombieClass());
			if (entityVictim != SpecialVictim::Unknown) return entityVictim;
		}
	}
	if (IsWitchEntity(entityId)) return SpecialVictim::Witch;
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
	if (_stricmp(name, "Witch") == 0) return SpecialVictim::Witch;
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

void KillFeedback::HandleSpecialKill(SpecialVictim victim, KillClassification classification,
	int victimUserId, const char* source) {
	if (!G::Vars.killFeedbackEnabled) return;
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
	if (!IsClassificationEnabled(classification)) {
		KFLog("special ignored source=%s victim=%s method=%s disabled", source,
			SpecialVictimName(victim), ClassificationName(classification));
		return;
	}
	m_lastSpecialVictimUserId = victimUserId;
	m_lastSpecialKillTime = now;
	KFLog("special accepted source=%s victim=%s userid=%d method=%s", source,
		SpecialVictimName(victim), victimUserId, ClassificationName(classification));
	Trigger(true, classification, 0);
}

void KillFeedback::QueueSpecialKill(SpecialVictim victim, KillClassification classification,
	int victimUserId, const char* source) {
	if (victimUserId <= 0) {
		HandleSpecialKill(victim, classification, victimUserId, source);
		return;
	}
	m_pendingSpecialKills[victimUserId] = {victim, classification, PresentationTime(), source,
		m_feedbackPosition.value, m_feedbackPosition.valid};
	KFLog("special queued source=%s victim=%s userid=%d method=%s", source,
		SpecialVictimName(victim), victimUserId, ClassificationName(classification));
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
		m_feedbackPosition.value = pending.position;
		m_feedbackPosition.valid = pending.positionValid;
		HandleSpecialKill(pending.victim, pending.classification, victimUserId, pending.source);
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

KillFeedback::KillClassification KillFeedback::ClassifyCommonKill(IGameEvent* event) const {
	KillClassification result;
	const int weaponId = event->GetInt("weapon_id", 0);
	result.explosion = event->GetBool("blast", false) || IsExplosionWeaponId(weaponId);
	result.melee = weaponId == WEAPON_MELEE || weaponId == WEAPON_CHAINSAW;
	result.headshot = event->GetBool("headshot", false) ||
		event->GetInt("hitgroup", 0) == KF_HITGROUP_HEAD;
	int infectedId = event->GetInt("infected_id", 0);
	if (infectedId <= 0) infectedId = event->GetInt("entityid", 0);
	const auto tracked = m_infectedDamage.find(infectedId);
	if (tracked != m_infectedDamage.end() && SimulationTime() - tracked->second.time <= DAMAGE_RECORD_LIFETIME) {
		result.headshot = result.headshot || tracked->second.classification.headshot;
		result.melee = result.melee || tracked->second.classification.melee;
		result.explosion = result.explosion || tracked->second.classification.explosion;
	}
	return result;
}

KillFeedback::KillClassification KillFeedback::ClassifySpecialKill(IGameEvent* event) const {
	KillClassification result;
	const char* weapon = event->GetString("weapon", "");
	const int damageType = event->GetInt("type", 0);
	result.explosion = (damageType & KF_DMG_BLAST) != 0 || ContainsNoCase(weapon, "pipe_bomb") ||
		ContainsNoCase(weapon, "grenade_launcher") || ContainsNoCase(weapon, "propane") ||
		ContainsNoCase(weapon, "oxygen") || ContainsNoCase(weapon, "firework");
	result.melee = (damageType & (KF_DMG_SLASH | KF_DMG_CLUB)) != 0 || IsMeleeWeaponName(weapon);
	result.headshot = event->GetBool("headshot", false) ||
		event->GetInt("hitgroup", 0) == KF_HITGROUP_HEAD;
	if (!weapon || weapon[0] == '\0') {
		const int activeWeaponId = LocalActiveWeaponId();
		result.explosion = result.explosion || IsExplosionWeaponId(activeWeaponId);
		result.melee = result.melee || activeWeaponId == WEAPON_MELEE || activeWeaponId == WEAPON_CHAINSAW;
	}
	return result;
}

KillFeedback::KillClassification KillFeedback::ClassifyTrackedDamage(IGameEvent* event) const {
	KillClassification result;
	const int damageType = event->GetInt("type", 0);
	result.explosion = (damageType & KF_DMG_BLAST) != 0;
	result.melee = (damageType & (KF_DMG_SLASH | KF_DMG_CLUB)) != 0;
	result.headshot = event->GetInt("hitgroup", 0) == KF_HITGROUP_HEAD;
	return result;
}

bool KillFeedback::IsClassificationEnabled(const KillClassification& classification) const {
	if (classification.explosion) return G::Vars.killFeedbackExplosion;
	if (classification.melee) return G::Vars.killFeedbackMelee;
	if (classification.headshot) return G::Vars.killFeedbackHeadshot;
	return G::Vars.killFeedbackFirearm;
}

bool KillFeedback::IsWitchEntity(int entityId) const {
	if (entityId <= 0 || !I::ClientEntityList) return false;
	auto* entity = I::ClientEntityList->GetClientEntity(entityId);
	auto* clientClass = entity ? entity->GetClientClass() : nullptr;
	return clientClass && ContainsNoCase(clientClass->m_pNetworkName, "witch");
}

void KillFeedback::OnGameEvent(IGameEvent* event) {
	if (!event) return;
	const char* name = event->GetName();
	if (!name) return;
	if (g_killEventDiagCount < 20) {
		char message[192] = {};
		_snprintf_s(message, sizeof(message), _TRUNCATE,
			"MapStage: KillFeedback event[%d]=%s", g_killEventDiagCount, name);
		NecolaDiagLog(message);
		++g_killEventDiagCount;
	}
	FlushPendingSpecialKills();

	if (std::strcmp(name, "round_start") == 0 || std::strcmp(name, "round_end") == 0 ||
		std::strcmp(name, "mission_lost") == 0 ||
		std::strcmp(name, "map_transition") == 0) {
		KFLog("state reset by event=%s", name);
		Reset();
		return;
	}
	if (std::strcmp(name, "bullet_impact") == 0) {
		if (IsLocalAttacker(event, "userid")) {
			m_lastImpactPosition.value = Vector(event->GetFloat("x", 0.0f),
				event->GetFloat("y", 0.0f), event->GetFloat("z", 0.0f));
			m_lastImpactPosition.valid = true;
			if (G::Vars.killFeedbackHitMode == 2) {
				m_pendingImpactPosition = m_lastImpactPosition;
				m_pendingImpactTick = GetTickCount();
			}
		}
		return;
	}

	const bool killEvent = std::strcmp(name, "infected_death") == 0 ||
		std::strcmp(name, "player_death") == 0 || std::strcmp(name, "witch_killed") == 0;
	if (!G::Vars.killFeedbackEnabled && G::Vars.killFeedbackHitMode == 0) {
		if (killEvent) KFLog("event=%s ignored: master switch disabled", name);
		return;
	}
	if (std::strcmp(name, "player_spawn") == 0) {
		const int userId = event->GetInt("userid", 0);
		m_playerDamage.erase(userId);
		m_pendingSpecialKills.erase(userId);
		const int index = I::EngineClient ? I::EngineClient->GetPlayerForUserID(userId) : 0;
		auto* entity = index > 0 && I::ClientEntityList
			? I::ClientEntityList->GetClientEntity(index) : nullptr;
		auto* player = entity ? entity->As<C_TerrorPlayer*>() : nullptr;
		const SpecialVictim victim = player
			? SpecialVictimFromZombieClass(player->m_zombieClass()) : SpecialVictim::Unknown;
		if (victim == SpecialVictim::Unknown) m_specialVictims.erase(userId);
		else m_specialVictims[userId] = victim;
		return;
	}
	if (std::strcmp(name, "ability_use") == 0) {
		const int userId = event->GetInt("userid", 0);
		const SpecialVictim victim = SpecialVictimFromAbility(event->GetString("ability", ""));
		if (userId > 0 && victim != SpecialVictim::Unknown) m_specialVictims[userId] = victim;
		return;
	}
	if (std::strcmp(name, "tank_spawn") == 0) {
		const int userId = event->GetInt("userid", 0);
		if (userId > 0) m_specialVictims[userId] = SpecialVictim::Tank;
		return;
	}
	if (std::strcmp(name, "witch_spawn") == 0) {
		m_infectedDamage.erase(event->GetInt("witchid", 0));
		return;
	}
	if (std::strcmp(name, "infected_hurt") == 0) {
		const bool local = IsLocalAttacker(event, "attacker");
		if (!local) return;
		const int entityId = event->GetInt("entityid", 0);
		CaptureFeedbackPosition(event, entityId);
		const bool witch = IsWitchEntity(entityId);
		const KillClassification classification = ClassifyTrackedDamage(event);
		if (entityId > 0) m_infectedDamage[entityId] = {classification, SimulationTime()};
		if (witch) {
			KFLog("tracked Witch damage entity=%d method=%s", entityId,
				ClassificationName(classification));
		}
		if (G::Vars.killFeedbackHitMode >= 2) {
			ResolveCombinedFeedback(false, false, false, 0, false, true);
		}
		return;
	}
	if (std::strcmp(name, "melee_kill") == 0) {
		const int entityId = event->GetInt("entityid", 0);
		CaptureFeedbackPosition(event, entityId);
		if (IsLocalAttacker(event, "userid") && IsWitchEntity(entityId)) {
			KillClassification classification;
			classification.melee = true;
			m_infectedDamage[entityId] = {classification, SimulationTime()};
			KFLog("tracked Witch melee_kill entity=%d", entityId);
			return;
		}
		if (!IsLocalAttacker(event, "userid") || entityId <= 0) return;
		KillClassification classification;
		classification.melee = true;
		m_infectedDamage[entityId] = {classification, SimulationTime()};
		KFLog("tracked common melee_kill entity=%d", entityId);
		return;
	}
	if (std::strcmp(name, "player_hurt") == 0) {
		const bool local = IsLocalAttacker(event, "attacker");
		if (!local) return;
		const int victimUserId = event->GetInt("userid", 0);
		if (victimUserId <= 0) return;
		CaptureFeedbackPosition(event, I::EngineClient->GetPlayerForUserID(victimUserId));
		const SpecialVictim victim = GetSpecialVictim(event);
		if (victim == SpecialVictim::Unknown) return;
		const KillClassification classification = ClassifySpecialKill(event);
		m_playerDamage[victimUserId] = {victim, classification, SimulationTime()};
		m_specialVictims[victimUserId] = victim;
		KFLog("tracked player_hurt victim=%s userid=%d health=%d method=%s",
			SpecialVictimName(victim), victimUserId, event->GetInt("health", -1),
			ClassificationName(classification));
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
		if (!G::Vars.killFeedbackEnabled) return;
		if (!IsLocalAttacker(event, "attacker")) return;
		const int victimUserId = event->GetInt("userid", 0);
		CaptureFeedbackPosition(event, I::EngineClient->GetPlayerForUserID(victimUserId));
		m_specialVictims[victimUserId] = dedicatedVictim;
		KillClassification classification = ClassifySpecialKill(event);
		const auto tracked = m_playerDamage.find(victimUserId);
		if (tracked != m_playerDamage.end() &&
			SimulationTime() - tracked->second.time <= DAMAGE_RECORD_LIFETIME) {
			classification = tracked->second.classification;
		}
		if ((dedicatedVictim == SpecialVictim::Charger && event->GetBool("melee", false)) ||
			(dedicatedVictim == SpecialVictim::Tank && event->GetBool("melee_only", false))) {
			classification.melee = true;
		}
		QueueSpecialKill(dedicatedVictim, classification, victimUserId, dedicatedSource);
		m_playerDamage.erase(victimUserId);
		return;
	}
	KillClassification classification;
	if (std::strcmp(name, "infected_death") == 0) {
		if (!G::Vars.killFeedbackEnabled) return;
		int infectedId = event->GetInt("infected_id", 0);
		if (infectedId <= 0) infectedId = event->GetInt("entityid", 0);
		CaptureFeedbackPosition(event, infectedId);
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
		classification = ClassifyCommonKill(event);
		m_lastCommonEntityId = infectedId;
		m_lastCommonKillTime = now;
		m_infectedDamage.erase(infectedId);
	} else if (std::strcmp(name, "player_death") == 0) {
		if (!G::Vars.killFeedbackEnabled) return;
		const bool local = IsLocalAttacker(event, "attacker");
		const int victimUserId = event->GetInt("userid", 0);
		const SpecialVictim victim = GetSpecialVictim(event);
		KFLog("event=player_death attacker=%d local=%d specialEnabled=%d victim=%s weapon=%s headshot=%d type=%d",
			event->GetInt("attacker", 0), local, G::Vars.killFeedbackSpecial,
			SpecialVictimName(victim),
			event->GetString("weapon", ""), event->GetBool("headshot", false), event->GetInt("type", 0));
		if (!local || victim == SpecialVictim::Unknown) {
			m_playerDamage.erase(victimUserId);
			m_pendingSpecialKills.erase(victimUserId);
			if (!local) m_specialVictims.erase(victimUserId);
			return;
		}
		int victimEntity = I::EngineClient->GetPlayerForUserID(victimUserId);
		if (victimEntity <= 0) victimEntity = event->GetInt("entityid", 0);
		CaptureFeedbackPosition(event, victimEntity);
		m_pendingSpecialKills.erase(victimUserId);
		classification = ClassifySpecialKill(event);
		HandleSpecialKill(victim, classification, victimUserId, "player_death");
		m_playerDamage.erase(victimUserId);
		m_specialVictims.erase(victimUserId);
		return;
	} else if (std::strcmp(name, "witch_killed") == 0) {
		m_infectedDamage.erase(event->GetInt("witchid", 0));
		KFLog("witch_killed cleanup; themed feedback is owned by player_death");
		return;
	} else {
		return;
	}

	if (!IsClassificationEnabled(classification)) {
		KFLog("kill ignored: method=%s disabled", ClassificationName(classification));
		return;
	}
	KFLog("kill accepted: method=%s", ClassificationName(classification));
	Trigger(false, classification, 0);
}

void KillFeedback::Trigger(bool special, const KillClassification& classification, int streak) {
	if (!special) {
		ResolveCombinedFeedback(true, classification.headshot, classification.melee, 0, false, true);
		return;
	}
	m_streak = m_streak < 1000000 ? m_streak + 1 : 1;
	const int streakValue = G::Vars.killFeedbackMultiKill
		? std::max(1, streak > 0 ? streak : m_streak) : 0;
	KFLog("trigger streak=%d method=%s special=%d", m_streak,
		ClassificationName(classification), special);
	ResolveCombinedFeedback(true, classification.headshot, classification.melee, streakValue,
		special, true);
}

const char* KillFeedback::ClassificationName(const KillClassification& classification) {
	if (classification.explosion) return classification.headshot ? "explosion_headshot" : "explosion";
	if (classification.melee) return classification.headshot ? "melee_headshot" : "melee";
	return classification.headshot ? "headshot" : "firearm";
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
	if (std::strcmp(ability, "ability_leap") == 0) return SpecialVictim::Jockey;
	if (std::strcmp(ability, "ability_charge") == 0) return SpecialVictim::Charger;
	if (std::strcmp(ability, "ability_throw") == 0) return SpecialVictim::Tank;
	return SpecialVictim::Unknown;
}

void KillFeedback::Draw() {
	PumpCommonHitTrace();
	for (const FeedbackPosition& hit : m_pendingSpecialHits) {
		m_feedbackPosition = hit;
		ResolveCombinedFeedback(false, false, false, 0, true, true);
	}
	m_pendingSpecialHits.clear();
	FlushPendingSpecialKills();
	PumpOverlay();
}

void KillFeedback::Stop() {
	if (m_overlayActive) {
		m_overlayActive = false;
		if (I::EngineClient) I::EngineClient->ClientCmd("r_screenoverlay \"\"");
	}
}

void KillFeedback::Reset() {
	Stop();
	m_streak = 0;
	m_infectedDamage.clear();
	m_playerDamage.clear();
	m_pendingSpecialKills.clear();
	m_specialVictims.clear();
	m_lastSpecialVictimUserId = 0;
	m_lastSpecialKillTime = -1000.0f;
	m_lastCommonEntityId = 0;
	m_lastCommonKillTime = -1000.0f;
	m_lastVictimRefresh = -1000.0f;
	m_themeFailureReported = false;
	m_precachedParticles.clear();
	m_feedbackPosition = {};
	m_lastImpactPosition = {};
	m_pendingImpactPosition = {};
	m_pendingImpactTick = 0;
	m_lastCommonTraceTick = 0;
	m_pendingSpecialHits.clear();
}

bool KillFeedback::InitListeners() {
	if (!I::GameEventManager) {
		KFLog("listener init skipped: GameEventManager unavailable");
		return false;
	}
	static const char* const kEvents[] = {
		"round_start", "round_end", "mission_lost", "map_transition",
		"bullet_impact",
		"player_spawn", "ability_use", "tank_spawn",
		"player_hurt", "player_death",
		"infected_hurt", "infected_death", "melee_kill",
		"witch_spawn", "witch_killed",
		"boomer_exploded", "charger_killed", "spitter_killed",
		"jockey_killed", "tank_killed",
	};
	bool allOk = true;
	bool anyOk = false;
	for (const char* name : kEvents) {
		const bool added = I::GameEventManager->AddListener(&m_listener, name, false);
		KFLog("listener %s=%d", name, added ? 1 : 0);
		allOk = allOk && added;
		anyOk = anyOk || added;
	}
	m_listenersRegistered = anyOk;
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
	if (!UnlockCommands(G::Vars.killFeedbackIcon)) return;
	if (I::EngineClient && I::ClientEntityList) {
		const int localIndex = I::EngineClient->GetLocalPlayer();
		auto* entity = I::ClientEntityList->GetClientEntity(localIndex);
		auto* player = entity ? entity->As<C_TerrorPlayer*>() : nullptr;
		if (player) {
			Vector angles;
			Vector forward;
			I::EngineClient->GetViewAngles(angles);
			U::Math.AngleVectors(angles, &forward);
			m_feedbackPosition.value = player->m_vecOrigin() + player->m_vecViewOffset() + forward * 180.0f;
			m_feedbackPosition.valid = true;
		}
	}
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
	const bool previousEnabled = G::Vars.killFeedbackEnabled;
	G::Vars.killFeedbackEnabled = true;
	const bool rendered = ResolveCombinedFeedback(true, headshot, melee, streak,
		channel[0] == 's', true);
	G::Vars.killFeedbackEnabled = previousEnabled;
	if (!rendered) {
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
	auto readInt = [section](const char* key, int fallback) {
		const auto value = section->find(key);
		return value != section->end() && value->is_number_integer()
			? value->get<int>() : fallback;
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
	G::Vars.killFeedbackHitMode = std::clamp(readInt("HitMode", 1), 0, 2);
	G::Vars.killFeedbackSiDedicated = readBool("SiDedicated", true);
	G::Vars.killFeedbackSiSound = readBool("SiSound", true);
	G::Vars.killFeedbackSoundVolume = std::clamp(readInt("SoundVolume", 100), 0, 100);
	m_siTheme = readString("SiTheme", "si_cf");
	m_ciTheme = readString("CiTheme", "ci_cf");

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
	section["HitMode"] = G::Vars.killFeedbackHitMode;
	section["SiDedicated"] = G::Vars.killFeedbackSiDedicated;
	section["SiSound"] = G::Vars.killFeedbackSiSound;
	section["SoundVolume"] = G::Vars.killFeedbackSoundVolume;
	section["SiTheme"] = m_siTheme;
	section["CiTheme"] = m_ciTheme;
}

void KillFeedback::SaveConfig() const {
	nlohmann::json doc = NecolaConfig::LoadConfig();
	SaveConfig(doc);
	NecolaConfig::SaveConfig(doc);
	KFLog("config saved si=%s ci=%s", m_siTheme.c_str(), m_ciTheme.c_str());
}

void KillFeedback::PrintStatus() const {
	char message[512] = {};
	_snprintf_s(message, sizeof(message), _TRUNCATE,
		"kill=%d hitMode=%d si=%s ci=%s themes=%d streak=%d overlay=%d particles=%d precache=%p dispatch=%p",
		G::Vars.killFeedbackEnabled, G::Vars.killFeedbackHitMode,
		m_siTheme.c_str(), m_ciTheme.c_str(),
		static_cast<int>(m_themes.size()), m_streak, m_overlayActive,
		static_cast<int>(m_precachedParticles.size()),
		reinterpret_cast<void*>(U::Offsets.m_dwPrecacheParticleSystem),
		reinterpret_cast<void*>(U::Offsets.m_dwDispatchParticleEffect3));
	KFLog("status %s", message);
	if (I::Cvars) I::Cvars->ConsolePrintf("[Necola] KillFeedback %s\n", message);
}
