#include "KillFeedback.h"

#include "../../Vars.h"
#include "../../../diag.h"
#include "../../../sdk/l4d2/interfaces/IConVar.h"
#include "../../../sdk/utils/FeatureConfigManager.h"
#include "../../../sdk/utils/Offsets.h"
#include "../../../sdk/l4d2/interfaces/FileSystem.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cctype>
#include <cstring>

namespace {

constexpr int kOverlayThrottleMs = 70;
constexpr int kKillDurationMs = 240;
constexpr int kHitDurationMs = 110;
constexpr int kParticleWindowMs = 1000;
constexpr int kParticlePerWindow = 32;
constexpr int kMaxScannedThemes = 64;
constexpr int kMaxThemeIdLen = 64;
constexpr int kStreakCap = 10;
constexpr std::uint32_t kKillDedupeMs = 500;
constexpr std::uint32_t kTrackLifetimeMs = 1000;

constexpr int kDmgBlast = 1 << 6;

float PresentationTime() {
	if (I::GlobalVars) return I::GlobalVars->curtime;
	return static_cast<float>(GetTickCount()) / 1000.0f;
}

std::uint32_t Now() {
	return static_cast<std::uint32_t>(GetTickCount());
}

int g_logBudget = 30;
void Log(const char* format, ...) {
	if (g_logBudget <= 0) return;
	--g_logBudget;
	char line[256] = {};
	va_list args;
	va_start(args, format);
	_vsnprintf_s(line, sizeof(line), _TRUNCATE, format, args);
	va_end(args);
	NecolaDiagLog(line);
}

bool ContainsNoCase(const char* value, const char* needle) {
	if (!value || !needle) return false;
	const std::size_t len = std::strlen(needle);
	for (std::size_t i = 0; value[i]; ++i) {
		std::size_t j = 0;
		while (j < len &&
			std::tolower(static_cast<unsigned char>(value[i + j])) ==
			std::tolower(static_cast<unsigned char>(needle[j]))) {
			++j;
		}
		if (j == len) return true;
	}
	return false;
}

// skeeto keeps a fixed list of melee weapon names; match on substrings so the
// weapon_id-less events still classify.
bool IsMeleeWeaponName(const char* weapon) {
	if (!weapon || !*weapon) return false;
	static const char* const kMelee[] = {
		"melee", "chainsaw", "machete", "katana", "cricket", "crowbar", "tonfa",
		"frying", "golf", "knife", "shovel", "pitchfork", "electric", "fireaxe",
		"axe", "bat", "pipe", "pool", "riot", "nightstick", "sword",
	};
	for (const char* needle : kMelee) {
		if (ContainsNoCase(weapon, needle)) return true;
	}
	return false;
}

// SEH-guarded wildcard scan (noinline + POD only, MSVC C2712).
__declspec(noinline) int ScanThemeIds(char ids[kMaxScannedThemes][kMaxThemeIdLen]) {
	FileFindHandle_t handle = -1;
	int count = 0;
	__try {
		const char* found = I::FileSystem->FindFirst("skeeto/skeeto_*.json", &handle);
		while (found && found[0] && count < kMaxScannedThemes) {
			const char* base = found;
			for (const char* cursor = found; *cursor; ++cursor) {
				if (*cursor == '/' || *cursor == '\\') base = cursor + 1;
			}
			static constexpr char kPrefix[] = "skeeto_";
			static constexpr char kSuffix[] = ".json";
			const int prefixLen = static_cast<int>(sizeof(kPrefix) - 1);
			const int suffixLen = static_cast<int>(sizeof(kSuffix) - 1);
			int length = 0;
			while (base[length]) ++length;
			if (length > prefixLen + suffixLen) {
				bool match = true;
				for (int i = 0; i < prefixLen && match; ++i) {
					if (base[i] != kPrefix[i]) match = false;
				}
				for (int i = 0; i < suffixLen && match; ++i) {
					if (base[length - suffixLen + i] != kSuffix[i]) match = false;
				}
				const int idLen = length - prefixLen - suffixLen;
				if (match && idLen > 0 && idLen < kMaxThemeIdLen) {
					for (int i = 0; i < idLen; ++i) ids[count][i] = base[prefixLen + i];
					ids[count][idLen] = '\0';
					++count;
				}
			}
			found = I::FileSystem->FindNext(handle);
		}
		if (handle != -1) I::FileSystem->FindClose(handle);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		count = -1;
	}
	return count;
}

} // namespace

// ------------------------------------------------------------------ themes --
void KillFeedback::LoadThemes() {
	if (m_themesLoaded) return;
	if (!I::FileSystem) return;
	const float now = PresentationTime();
	if (now - m_lastThemeLoad < 2.0f) return;
	m_lastThemeLoad = now;
	m_themes.clear();

	std::vector<std::string> ids;
	char scanned[kMaxScannedThemes][kMaxThemeIdLen] = {};
	const int scannedCount = ScanThemeIds(scanned);
	if (scannedCount > 0) {
		for (int i = 0; i < scannedCount; ++i) ids.emplace_back(scanned[i]);
		Log("theme scan found %d candidate(s)", scannedCount);
	} else {
		// Built-in fallback only; a pack that ships skeeto_*.json overrides it.
		ids = {"si_cf", "si_valorant", "si_lightning", "si_love", "si_star",
			"si_bf1_advanced", "si_bf2042_advanced", "si_deltaforce_advanced",
			"ci_cf", "ci_apex", "ci_bf1", "ci_bf2042", "ci_bf5", "ci_cod",
			"ci_deltaforce", "ci_l4d2", "ci_ow"};
		Log("theme scan unavailable (%d); using built-in list", scannedCount);
	}

	auto loadStyle = [](const nlohmann::json& section, KillStyle& style) {
		if (!section.is_object()) return;
		auto str = [&section](const char* key) {
			const auto it = section.find(key);
			return it != section.end() && it->is_string() ? it->get<std::string>() : std::string();
		};
		style.overlay = str("overlay");
		if (const auto seq = section.find("overlay_sequence");
			seq != section.end() && seq->is_array()) {
			for (const auto& value : *seq) {
				if (static_cast<int>(style.overlaySequence.size()) >= KillStyle::kMaxOverlayFrames) break;
				if (value.is_string() && !value.get<std::string>().empty()) {
					style.overlaySequence.push_back(value.get<std::string>());
				}
			}
		}
		if (const auto step = section.find("overlay_step_ms");
			step != section.end() && step->is_number_integer()) {
			style.overlayStepMs = std::clamp(step->get<int>(),
				KillStyle::kMinOverlayStepMs, KillStyle::kMaxOverlayStepMs);
		}
		style.particle = str("particle");
		if (const auto list = section.find("particles"); list != section.end() && list->is_array()) {
			for (const auto& value : *list) {
				if (static_cast<int>(style.particles.size()) >= KillStyle::kMaxParticles) break;
				if (value.is_string() && !value.get<std::string>().empty()) {
					style.particles.push_back(value.get<std::string>());
				}
			}
		}
		style.sound = str("sound");
		if (const auto list = section.find("sounds"); list != section.end() && list->is_array()) {
			for (const auto& value : *list) {
				if (static_cast<int>(style.sounds.size()) >= KillStyle::kMaxSounds) break;
				if (value.is_string() && !value.get<std::string>().empty()) {
					style.sounds.push_back(value.get<std::string>());
				}
			}
		}
		if (const auto priority = section.find("priority");
			priority != section.end() && priority->is_number_integer()) {
			style.priority = priority->get<int>();
		}
		for (const char* key : {"overlay_ms", "duration", "overlay_duration"}) {
			const auto value = section.find(key);
			if (value != section.end() && value->is_number_integer()) {
				style.duration = std::clamp(value->get<int>(), 0, KillStyle::kMaxOverlayMs);
				break;
			}
		}
		if (const auto world = section.find("world");
			world != section.end() && world->is_boolean()) {
			style.world = world->get<bool>();
		}
		style.enabled = !style.overlay.empty() || !style.overlaySequence.empty() ||
			!style.particle.empty() || !style.particles.empty() ||
			!style.sound.empty() || !style.sounds.empty();
	};

	for (const std::string& id : ids) {
		char path[128] = {};
		_snprintf_s(path, sizeof(path), _TRUNCATE, "skeeto/skeeto_%s.json", id.c_str());
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
			auto str = [&doc](const char* key) {
				const auto it = doc.find(key);
				return it != doc.end() && it->is_string() ? it->get<std::string>() : std::string();
			};
			theme.name = str("name");
			theme.channel = str("channel");
			theme.note = str("note");
			if (const auto sort = doc.find("sort");
				sort != doc.end() && sort->is_number_integer()) {
				theme.sort = sort->get<int>();
			}
			if (const auto streak = doc.find("streak"); streak != doc.end() && streak->is_object()) {
				if (const auto v = streak->find("enabled"); v != streak->end() && v->is_boolean()) {
					theme.streakEnabled = v->get<bool>();
				}
				if (const auto v = streak->find("wrap"); v != streak->end() && v->is_number_integer()) {
					theme.streakWrap = v->get<int>();
				}
				if (const auto v = streak->find("priority"); v != streak->end() && v->is_number_integer()) {
					theme.streakPriority = v->get<int>();
				}
			}
			auto styleFrom = [&doc, &loadStyle](const char* key, KillStyle& style) {
				const auto it = doc.find(key);
				if (it != doc.end() && it->is_object()) loadStyle(*it, style);
			};
			styleFrom("hit", theme.hit);
			styleFrom("kill", theme.kill);
			styleFrom("headshot", theme.headshot);
			styleFrom("melee", theme.melee);
			for (int i = 1; i <= kStreakCap; ++i) {
				char key[24] = {};
				_snprintf_s(key, sizeof(key), _TRUNCATE, "streak_%d", i);
				styleFrom(key, theme.streak[i]);
				if (theme.streak[i].enabled && theme.streak[i].priority == 0) {
					theme.streak[i].priority = theme.streakPriority != 0 ? theme.streakPriority : 10;
				}
			}
			styleFrom("streak_default", theme.streakDefault);
			// skeeto's fixed fx event keys (MatchFxKey + DetectCrown).
			for (const char* key : {"skeet", "melee_skeet", "headshot_skeet", "crit_text",
					"melee_crown", "perfect_crown", "crown"}) {
				const auto it = doc.find(key);
				if (it != doc.end() && it->is_object()) {
					KillStyle style;
					loadStyle(*it, style);
					if (style.enabled) theme.fx[key] = style;
				}
			}
			theme.loaded = true;
			m_themes.push_back(std::move(theme));
		} catch (...) {
			Log("theme %s parse failed", id.c_str());
		}
	}
	// skeeto's 'sort' key orders themes; keep resolution deterministic.
	std::sort(m_themes.begin(), m_themes.end(),
		[](const KillTheme& a, const KillTheme& b) { return a.sort < b.sort; });
	m_themesLoaded = !m_themes.empty();
	Log("themes loaded=%d", static_cast<int>(m_themes.size()));
	WarmupParticles();
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
	} else {
		m_ciTheme = themeId;
	}
	SaveConfig();
	WarmupParticles();
}

// -------------------------------------------------------------- victims ----
bool KillFeedback::IsLocalAttacker(IGameEvent* event, const char* field) const {
	if (!event || !I::EngineClient) return false;
	const int userId = event->GetInt(field, 0);
	if (userId <= 0) return false;
	const int resolved = I::EngineClient->GetPlayerForUserID(userId);
	return resolved > 0 && resolved == I::EngineClient->GetLocalPlayer();
}

KillFeedback::Victim KillFeedback::VictimFromZombieClass(int zombieClass) const {
	switch (zombieClass) {
		case CLASS_SMOKER: return Victim::Smoker;
		case CLASS_BOOMER: return Victim::Boomer;
		case CLASS_HUNTER: return Victim::Hunter;
		case CLASS_SPITTER: return Victim::Spitter;
		case CLASS_JOCKEY: return Victim::Jockey;
		case CLASS_CHARGER: return Victim::Charger;
		case CLASS_TANK: return Victim::Tank;
		default: return Victim::Unknown;
	}
}

KillFeedback::Victim KillFeedback::VictimFromEvent(IGameEvent* event) const {
	// skeeto matches the event's victimname string (MatchFxKey); the entity class is
	// only a fallback, because m_zombieClass() can already be gone on player_death.
	if (event) {
		const char* name = event->GetString("victimname", "");
		if (name && *name) {
			if (std::strcmp(name, "Smoker") == 0) return Victim::Smoker;
			if (std::strcmp(name, "Boomer") == 0) return Victim::Boomer;
			if (std::strcmp(name, "Hunter") == 0) return Victim::Hunter;
			if (std::strcmp(name, "Spitter") == 0) return Victim::Spitter;
			if (std::strcmp(name, "Jockey") == 0) return Victim::Jockey;
			if (std::strcmp(name, "Charger") == 0) return Victim::Charger;
			if (std::strcmp(name, "Tank") == 0) return Victim::Tank;
			if (std::strcmp(name, "Witch") == 0) return Victim::Witch;
		}
	}
	if (!event || !I::EngineClient || !I::ClientEntityList) return Victim::Unknown;
	const int userId = event->GetInt("userid", 0);
	const int ent = I::EngineClient->GetPlayerForUserID(userId);
	if (ent > 0) {
		auto* entity = I::ClientEntityList->GetClientEntity(ent);
		auto* player = entity ? entity->As<C_TerrorPlayer*>() : nullptr;
		if (player) {
			const Victim live = VictimFromZombieClass(player->m_zombieClass());
			if (live != Victim::Unknown) return live;
		}
	}
	const auto cached = m_victims.find(ent);
	if (cached != m_victims.end()) return cached->second;
	return Victim::Unknown;
}

bool KillFeedback::IsVictimEnabled(Victim victim) const {
	switch (victim) {
		case Victim::Smoker: return G::Vars.killFeedbackSmoker;
		case Victim::Boomer: return G::Vars.killFeedbackBoomer;
		case Victim::Hunter: return G::Vars.killFeedbackHunter;
		case Victim::Spitter: return G::Vars.killFeedbackSpitter;
		case Victim::Jockey: return G::Vars.killFeedbackJockey;
		case Victim::Charger: return G::Vars.killFeedbackCharger;
		case Victim::Tank: return G::Vars.killFeedbackTank;
		case Victim::Witch: return G::Vars.killFeedbackWitch;
		default: return false;
	}
}

bool KillFeedback::IsMethodEnabled(const Classification& classification) const {
	if (classification.explosion && !G::Vars.killFeedbackExplosion) return false;
	if (classification.melee && !G::Vars.killFeedbackMelee) return false;
	if (classification.headshot && !G::Vars.killFeedbackHeadshot) return false;
	if (!classification.headshot && !classification.melee && !classification.explosion &&
		!G::Vars.killFeedbackFirearm) return false;
	return true;
}

KillFeedback::Classification KillFeedback::Classify(IGameEvent* event, bool special) const {
	Classification classification;
	if (!event) return classification;
	classification.headshot = event->GetBool("headshot", false);
	classification.melee = IsMeleeWeaponName(event->GetString("weapon", ""));
	const int type = event->GetInt("type", 0);
	classification.explosion = (type & kDmgBlast) != 0;
	if (special && !classification.headshot && !classification.melee) {
		// player_death carries "type" only for some deaths; keep the tracked value.
		const int userId = event->GetInt("userid", 0);
		const auto tracked = m_tracked.find(userId);
		if (tracked != m_tracked.end()) {
			classification.headshot = classification.headshot || tracked->second.headshot;
			classification.melee = classification.melee || tracked->second.melee;
			classification.explosion = classification.explosion || tracked->second.explosion;
		}
	}
	return classification;
}

// ------------------------------------------------------------ selection ----
bool KillFeedback::HitCheck(const KillTheme& theme, const char* eventName, bool headshot,
	bool melee, int streak, KillStyle& out) const {
	KillStyle best;
	auto consider = [&best](const KillStyle& candidate) {
		if (!candidate.enabled) return;
		if (!best.enabled || candidate.priority > best.priority) best = candidate;
	};
	if (std::strcmp(eventName, "hit") == 0) {
		consider(theme.hit);
	} else {
		if (theme.streakEnabled && streak > 0) {
			int index = streak;
			if (theme.streakWrap > 0) {
				while (index > theme.streakWrap) index -= theme.streakWrap;
			}
			if (index >= 1 && index <= kStreakCap) consider(theme.streak[index]);
			consider(theme.streakDefault);
		}
		consider(theme.kill);
		if (melee) consider(theme.melee);
		if (headshot) consider(theme.headshot);
	}
	out = best;
	return best.enabled;
}

void KillFeedback::Trigger(bool special, const Classification& classification, int streak) {
	if (special) {
		m_streak = m_streak < 1000000 ? m_streak + 1 : 1;
	}
	const int streakValue = (special && G::Vars.killFeedbackMultiKill)
		? std::max(1, streak > 0 ? streak : m_streak) : 0;
	ResolveCombinedFeedback(true, classification.headshot, classification.melee,
		streakValue, special, true);
}

bool KillFeedback::ResolveCombinedFeedback(bool kill, bool headshot, bool melee, int streak,
	bool allowSi, bool allowCi) {
	if (kill && !G::Vars.killFeedbackEnabled) return false;
	if (!G::Vars.killFeedbackSound && !G::Vars.killFeedbackIcon) return false;
	if (!kill && G::Vars.killFeedbackHitMode == 0) return false;
	if (!I::EngineClient || !I::EngineClient->IsConnected() || !I::EngineClient->IsInGame()) return false;

	LoadThemes();
	KillStyle siStyle;
	KillStyle ciStyle;
	bool siValid = false;
	bool ciValid = false;
	if (allowSi && m_siTheme != "off") {
		if (const KillTheme* theme = FindTheme("si", m_siTheme)) {
			siValid = HitCheck(*theme, kill ? "kill" : "hit", headshot, melee, streak, siStyle);
		}
	}
	if (allowCi && m_ciTheme != "off") {
		if (const KillTheme* theme = FindTheme("ci", m_ciTheme)) {
			ciValid = HitCheck(*theme, kill ? "kill" : "hit", headshot, melee && !headshot, 0, ciStyle);
		}
	}
	if (!siValid && !ciValid) {
		if (!m_themeWarned && I::Cvars) {
			m_themeWarned = true;
			I::Cvars->ConsolePrintf("[Necola] KillFeedback: no style resolved; check the theme addon.\n");
		}
		return false;
	}
	m_themeWarned = false;

	KillStyle output;
	if (G::Vars.killFeedbackSiDedicated && siValid) output = siStyle;
	else if (ciValid) output = ciStyle;
	else output = siStyle;

	if (G::Vars.killFeedbackSiSound && siValid && !siStyle.sound.empty()) {
		output.sound = siStyle.sound;
	} else if (ciValid && !ciStyle.sound.empty()) {
		output.sound = ciStyle.sound;
	} else if (siValid && !siStyle.sound.empty()) {
		output.sound = siStyle.sound;
	}
	output.enabled = output.enabled || !output.overlay.empty() || !output.sound.empty();

	Log("resolve kill=%d si=%d ci=%d overlay=%s", kill, siValid, ciValid, output.overlay.c_str());
	PlayStyle(output, kill ? kKillDurationMs : kHitDurationMs, kill);
	return true;
}

// ------------------------------------------------------------ rendering ----
void KillFeedback::SendOverlay(const char* material) {
	if (!I::EngineClient || !material || !*material) return;
	char command[192] = {};
	_snprintf_s(command, sizeof(command), _TRUNCATE, "r_screenoverlay %s", material);
	I::EngineClient->ClientCmd(command);
}

void KillFeedback::ClearOverlay() {
	if (I::EngineClient) I::EngineClient->ClientCmd("r_screenoverlay \"\"");
	m_activeSequence.clear();
	m_sequenceFrame = 0;
	m_sequenceStepMs = 0;
	m_overlayActive = false;
}

void KillFeedback::PrecacheStyle(const KillStyle& style) {
	if (!U::Offsets.m_dwPrecacheParticleSystem) return;
	using PrecacheFn = int(__cdecl*)(const char*);
	const PrecacheFn precache =
		reinterpret_cast<PrecacheFn>(U::Offsets.m_dwPrecacheParticleSystem);
	auto one = [&](const std::string& name) {
		if (name.empty() || m_precached.contains(name)) return;
		MEMORY_BASIC_INFORMATION info = {};
		if (!VirtualQuery(reinterpret_cast<const void*>(precache), &info, sizeof(info)) ||
			(info.Protect & 0xF0) == 0) {
			return;
		}
		const int index = precache(name.c_str());
		m_precached.insert(name);
		Log("particle precached %s (index=%d)", name.c_str(), index);
	};
	one(style.particle);
	for (const auto& name : style.particles) one(name);
}

void KillFeedback::WarmupParticles() {
	if (!I::EngineClient || !I::EngineClient->IsConnected() || !I::EngineClient->IsInGame()) return;
	auto warm = [this](const KillTheme& theme) {
		PrecacheStyle(theme.hit);
		PrecacheStyle(theme.kill);
		PrecacheStyle(theme.headshot);
		PrecacheStyle(theme.melee);
		for (const auto& streak : theme.streak) PrecacheStyle(streak);
		PrecacheStyle(theme.streakDefault);
	};
	if (m_siTheme != "off") {
		if (const KillTheme* theme = FindTheme("si", m_siTheme)) warm(*theme);
	}
	if (m_ciTheme != "off") {
		if (const KillTheme* theme = FindTheme("ci", m_ciTheme)) warm(*theme);
	}
}

void KillFeedback::SpawnParticles(const KillStyle& style) {
	if (!G::Vars.killFeedbackIcon || !U::Offsets.m_dwDispatchParticleEffect3) return;
	PrecacheStyle(style);
	using DispatchFn = void(__cdecl*)(const char*, Vector*, Vector*, int, int, int);
	const DispatchFn dispatch =
		reinterpret_cast<DispatchFn>(U::Offsets.m_dwDispatchParticleEffect3);
	MEMORY_BASIC_INFORMATION info = {};
	if (!VirtualQuery(reinterpret_cast<const void*>(dispatch), &info, sizeof(info)) ||
		(info.Protect & 0xF0) == 0) {
		return;
	}

	Vector origin;
	if (style.world) {
		if (I::EngineClient && I::ClientEntityList) {
			const int localIndex = I::EngineClient->GetLocalPlayer();
			if (localIndex > 0) {
				if (auto* entity = I::ClientEntityList->GetClientEntity(localIndex)) {
					origin = entity->GetAbsOrigin();
				}
			}
		}
		if (m_pendingHits.empty()) {
			// no confirmed hit position cached: keep the local origin
		}
	}
	Vector angles;

	// 32 dispatches per second, like SKT_Particle_Dispatch.
	const std::uint32_t now = Now();
	if (m_particleWindowStart == 0 || now - m_particleWindowStart >= kParticleWindowMs) {
		m_particleWindowStart = now;
		m_particleWindowCount = 0;
	}
	auto one = [&](const std::string& name) {
		if (name.empty()) return;
		if (m_particleWindowCount >= kParticlePerWindow) return;
		++m_particleWindowCount;
		dispatch(name.c_str(), &origin, &angles, 2, 0, 0);
	};
	one(style.particle);
	for (const auto& name : style.particles) one(name);
}

void KillFeedback::PlaySound(const KillStyle& style) {
	if (!G::Vars.killFeedbackSound || G::Vars.killFeedbackSoundVolume <= 0) return;
	const std::size_t count = style.sounds.size() + (style.sound.empty() ? 0u : 1u);
	if (count == 0) return;
	const std::size_t index = (static_cast<std::size_t>(Now()) +
		static_cast<std::size_t>(m_streak * 31)) % count;
	const std::string* selected =
		index < style.sounds.size() ? &style.sounds[index] : &style.sound;
	const char* relative = selected->c_str();
	if (std::strncmp(relative, "sound/", 6) == 0) relative += 6;
	if (!*relative) return;
	const float volume =
		std::clamp(G::Vars.killFeedbackSoundVolume, 0, 100) / 100.0f;
	if (volume >= 0.999f && I::MatSystemSurface) {
		I::MatSystemSurface->PlaySound(relative);
		return;
	}
	if (I::EngineSound) {
		I::EngineSound->EmitAmbientSound(relative, volume, SNDLVL_NORM, 0, 0.0f);
		return;
	}
	if (I::EngineClient) {
		char command[192] = {};
		_snprintf_s(command, sizeof(command), _TRUNCATE, "playvol %s %.2f", relative, volume);
		I::EngineClient->ClientCmd(command);
	}
}

void KillFeedback::PlayStyle(const KillStyle& style, int defaultDuration, bool allowRepeat) {
	if (!style.enabled) return;
	const std::uint32_t now = Now();
	if (!allowRepeat && now - m_lastPlayTick < kOverlayThrottleMs) return;
	m_lastPlayTick = now;

	PlaySound(style);
	if (G::Vars.killFeedbackIcon) SpawnParticles(style);
	if (!G::Vars.killFeedbackIcon) return;

	// r_screenoverlay is FCVAR_CHEAT; clear it once and allow client commands.
	static bool overlayUnlocked = false;
	if (!overlayUnlocked && I::Cvars) {
		if (ConCommandBase* command = I::Cvars->FindCommandBase("r_screenoverlay")) {
			command->m_nFlags = (command->m_nFlags & ~0x4000) | 0x40000000;
			overlayUnlocked = true;
		}
	}
	const bool sequence = style.HasSequence();
	if (!sequence && style.overlay.empty()) return;
	if (!I::EngineClient) return;

	int duration = style.duration > 0 ? style.duration : defaultDuration;
	duration = std::clamp(duration, 40, KillStyle::kMaxOverlayMs);

	if (sequence) {
		const std::size_t frames = std::min<std::size_t>(
			style.overlaySequence.size(), static_cast<std::size_t>(KillStyle::kMaxOverlayFrames));
		const int step = std::clamp(style.overlayStepMs,
			KillStyle::kMinOverlayStepMs, KillStyle::kMaxOverlayStepMs);
		// skeeto auto-extends overlay_ms so the last frame is not cut off.
		duration = std::max(duration, static_cast<int>(frames) * step);
		m_activeSequence.assign(style.overlaySequence.begin(),
			style.overlaySequence.begin() + static_cast<std::ptrdiff_t>(frames));
		m_sequenceFrame = 0;
		m_sequenceStepMs = step;
		SendOverlay(m_activeSequence[0].c_str());
		m_nextFrameTick = now + static_cast<std::uint32_t>(step);
	} else {
		m_activeSequence.clear();
		m_sequenceFrame = 0;
		m_sequenceStepMs = 0;
		SendOverlay(style.overlay.c_str());
	}
	m_overlayUntil = now + static_cast<std::uint32_t>(duration);
	m_overlayActive = true;
}

void KillFeedback::StepOverlay() {
	if (!m_overlayActive) return;
	const std::uint32_t now = Now();
	if (m_sequenceFrame + 1 < m_activeSequence.size() && now >= m_nextFrameTick) {
		++m_sequenceFrame;
		SendOverlay(m_activeSequence[m_sequenceFrame].c_str());
		m_nextFrameTick = now + static_cast<std::uint32_t>(m_sequenceStepMs);
	}
	if (now < m_overlayUntil) return;
	ClearOverlay();
}

// ---------------------------------------------------------------- events ----
void KillFeedback::HandleEvent(IGameEvent* event) {
	if (!event) return;
	const char* name = event->GetName();
	if (!name) return;

	if (std::strcmp(name, "round_start") == 0 || std::strcmp(name, "round_end") == 0 ||
		std::strcmp(name, "mission_lost") == 0 || std::strcmp(name, "map_transition") == 0) {
		Reset();
		return;
	}
	if (!G::Vars.killFeedbackEnabled && G::Vars.killFeedbackHitMode == 0) return;
	if (!I::EngineClient) return;

	// --- damage bookkeeping: the method that caused the kill ---
	if (std::strcmp(name, "player_hurt") == 0 || std::strcmp(name, "melee_kill") == 0 ||
		std::strcmp(name, "infected_hurt") == 0) {
		const int userId = event->GetInt("userid", 0);
		const Classification classification = Classify(event, false);
		if (userId > 0) m_tracked[userId] = classification;
		return;
	}
	if (std::strcmp(name, "lunge_pounce") == 0 || std::strcmp(name, "jockey_ride") == 0) {
		// skeeto tracks the pounce/ride state itself (SKT_GameEvent_Dispatch_60);
		// this is the equivalent marker used by the skeet family below.
		const int userId = event->GetInt("userid", 0);
		if (userId > 0) m_airborne.insert(userId);
		return;
	}
	if (std::strcmp(name, "pounce_stopped") == 0 || std::strcmp(name, "jockey_ride_end") == 0) {
		m_airborne.erase(event->GetInt("userid", 0));
		return;
	}

	if (std::strcmp(name, "player_spawn") == 0) {
		RefreshSpecialVictims();
		return;
	}

	// --- kills ---
	if (std::strcmp(name, "infected_death") == 0) {
		if (!G::Vars.killFeedbackEnabled) return;
		if (!IsLocalAttacker(event, "attacker")) return;
		if (!G::Vars.killFeedbackCommon) return;
		int entityId = event->GetInt("infected_id", 0);
		if (entityId <= 0) entityId = event->GetInt("entityid", 0);
		const std::uint32_t now = Now();
		if (entityId > 0 && entityId == m_lastCommonEntity && now - m_lastCommonTime <= kKillDedupeMs) {
			return;
		}
		m_lastCommonEntity = entityId;
		m_lastCommonTime = now;
		const Classification classification = Classify(event, false);
		if (!IsMethodEnabled(classification)) return;
		Trigger(false, classification, 0);
		return;
	}
	if (std::strcmp(name, "player_death") == 0) {
		if (!G::Vars.killFeedbackEnabled) return;
		if (!IsLocalAttacker(event, "attacker")) return;
		const Victim victim = VictimFromEvent(event);
		const int userId = event->GetInt("userid", 0);
		// Unresolved victim: leave it to the per-class dedicated event below.
		if (victim == Victim::Unknown) {
			Log("player_death unresolved vn=%s atk=%d userid=%d",
				event->GetString("victimname", "?"), event->GetInt("attacker", 0), userId);
			return;
		}
		PendingKill kill;
		kill.victim = victim;
		kill.classification = Classify(event, true);
		kill.userId = userId;
		kill.time = PresentationTime();
		HandleKill(kill);
		return;
	}
	if (std::strcmp(name, "witch_killed") == 0) {
		// The witch is not a player: no player_death ever follows, so this event is
		// the authoritative source (skeeto's own witch branch).
		if (!G::Vars.killFeedbackEnabled) return;
		if (!IsLocalAttacker(event, "userid")) return;
		const int witchId = event->GetInt("witchid", 0);
		const std::uint32_t now = Now();
		if (witchId > 0 && witchId == m_lastCommonEntity && now - m_lastCommonTime <= kKillDedupeMs) {
			return;
		}
		m_lastCommonEntity = witchId;
		m_lastCommonTime = now;
		PendingKill kill;
		kill.victim = Victim::Witch;
		kill.classification = Classify(event, false);
		kill.userId = witchId;
		kill.time = PresentationTime();
		kill.source = "witch_killed";
		// DetectCrown: melee weapon => melee_crown, otherwise a one-shot kill =>
		// perfect_crown, and crown as the generic fallback.
		const char* fxKey = nullptr;
		if (IsMeleeWeaponName(event->GetString("weapon", ""))) fxKey = "melee_crown";
		else if (event->GetBool("oneshot", false)) fxKey = "perfect_crown";
		else fxKey = "crown";
		DispatchFx(fxKey, Vector());
		HandleKill(kill);
		return;
	}

	// --- per-class death events: 150 ms fallback behind player_death ---
	Victim dedicated = Victim::Unknown;
	if (std::strcmp(name, "boomer_exploded") == 0) dedicated = Victim::Boomer;
	else if (std::strcmp(name, "charger_killed") == 0) dedicated = Victim::Charger;
	else if (std::strcmp(name, "spitter_killed") == 0) dedicated = Victim::Spitter;
	else if (std::strcmp(name, "jockey_killed") == 0) dedicated = Victim::Jockey;
	else if (std::strcmp(name, "tank_killed") == 0) dedicated = Victim::Tank;
	if (dedicated == Victim::Unknown) return;
	if (!IsLocalAttacker(event, "userid")) return;

	PendingKill kill;
	kill.victim = dedicated;
	kill.classification = Classify(event, false);
	kill.userId = event->GetInt("userid", 0);
	kill.time = PresentationTime();
	kill.source = name;
	m_pendingKills.push_back(kill);
	m_airborne.erase(kill.userId);
}

void KillFeedback::HandleKill(const PendingKill& kill) {
	if (!G::Vars.killFeedbackEnabled) return;
	if (!G::Vars.killFeedbackSpecial || !IsVictimEnabled(kill.victim)) return;
	if (!IsMethodEnabled(kill.classification)) return;
	const float now = PresentationTime();
	if (kill.userId > 0 && kill.userId == m_lastVictimUserId &&
		now - m_lastVictimTime <= 0.5f) {
		return;
	}
	m_lastVictimUserId = kill.userId;
	m_lastVictimTime = now;
	Log("kill %s source=%s", kill.source, kill.classification.headshot ? "headshot" : "body");
	Trigger(true, kill.classification, 0);
}

void KillFeedbackListener::FireGameEvent(IGameEvent* event) {
	F::KillFeedbackMgr.HandleEvent(event);
}

bool KillFeedback::InitListeners() {
	if (!I::GameEventManager) return false;
	static const char* const kEvents[] = {
		"round_start", "round_end", "mission_lost", "map_transition",
		"player_hurt", "infected_hurt", "melee_kill", "player_spawn",
		"player_death", "infected_death", "witch_killed",
		"lunge_pounce", "pounce_stopped", "jockey_ride", "jockey_ride_end",
		"boomer_exploded", "charger_killed", "spitter_killed", "jockey_killed", "tank_killed",
	};
	bool allOk = true;
	for (const char* name : kEvents) {
		allOk = I::GameEventManager->AddListener(&m_listener, name, false) && allOk;
	}
	m_listenersRegistered = true;
	return allOk;
}

// ------------------------------------------------------------------ sinks ----
void KillFeedback::OnSpecialHealthDrop(const Vector& position) {
	if (G::Vars.killFeedbackHitMode == 0) return;
	PendingHit hit;
	hit.position = position;
	hit.valid = true;
	hit.time = PresentationTime();
	if (m_pendingHits.size() >= 32) m_pendingHits.erase(m_pendingHits.begin());
	m_pendingHits.push_back(hit);
}

void KillFeedback::RefreshSpecialVictims() {
	if (!I::EngineClient || !I::ClientEntityList) return;
	const float now = PresentationTime();
	if (now - m_lastRefresh < 1.0f) return;
	m_lastRefresh = now;
	for (int index = 1; index <= I::EngineClient->GetMaxClients(); ++index) {
		auto* entity = I::ClientEntityList->GetClientEntity(index);
		auto* player = entity ? entity->As<C_TerrorPlayer*>() : nullptr;
		if (!player) continue;
		const Victim victim = VictimFromZombieClass(player->m_zombieClass());
		if (victim == Victim::Unknown) m_victims.erase(index);
		else m_victims[index] = victim;
	}
}

void KillFeedback::Draw() {
	const float now = PresentationTime();
	// Confirmed hits queued by DamagePipeline.
	for (auto it = m_pendingHits.begin(); it != m_pendingHits.end();) {
		const PendingHit hit = *it;
		it = m_pendingHits.erase(it);
		ResolveCombinedFeedback(false, false, false, 0, true, true);
	}
	// Deferred per-class kills (150 ms behind player_death).
	for (auto it = m_pendingKills.begin(); it != m_pendingKills.end();) {
		if (now - it->time < 0.15f) {
			++it;
			continue;
		}
		const PendingKill kill = *it;
		it = m_pendingKills.erase(it);
		HandleKill(kill);
	}
	StepOverlay();
}

void KillFeedback::Stop() {
	if (m_overlayActive) ClearOverlay();
	m_pendingHits.clear();
	m_pendingKills.clear();
}

void KillFeedback::Reset() {
	Stop();
	m_streak = 0;
	m_tracked.clear();
	m_victims.clear();
	m_pendingHits.clear();
	m_pendingKills.clear();
	m_lastVictimUserId = 0;
	m_lastVictimTime = -1000.0f;
	m_lastCommonEntity = 0;
	m_lastCommonTime = 0;
	m_lastRefresh = -1000.0f;
	m_themeWarned = false;
	m_precached.clear();
	m_airborne.clear();
}

void KillFeedback::Shutdown() {
	Stop();
	if (m_listenersRegistered && I::GameEventManager) {
		I::GameEventManager->RemoveListener(&m_listener);
	}
	m_listenersRegistered = false;
	m_precached.clear();
}

void KillFeedback::Preview(int kind) {
	if (!G::Vars.killFeedbackEnabled) G::Vars.killFeedbackEnabled = true;
	Classification classification;
	const bool special = kind >= 4;
	if (kind == 1 || kind == 5) classification.headshot = true;
	if (kind == 2 || kind == 6) classification.melee = true;
	Trigger(special, classification, special ? (kind >= 8 ? 10 : 3) : 0);
}

void KillFeedback::PrintStatus() const {
	if (!I::Cvars) return;
	I::Cvars->ConsolePrintf("[Necola] KillFeedback: si=%s ci=%s themes=%d streak=%d overlay=%d\n",
		m_siTheme.c_str(), m_ciTheme.c_str(), static_cast<int>(m_themes.size()),
		m_streak, m_overlayActive ? 1 : 0);
}

// ---------------------------------------------------------------- config ----
void KillFeedback::LoadConfig(const nlohmann::json& doc) {
	const auto section = doc.find("KillFeedback");
	if (section == doc.end() || !section->is_object()) return;
	auto boolOf = [&section](const char* key, bool fallback) {
		const auto value = section->find(key);
		return value != section->end() && value->is_boolean() ? value->get<bool>() : fallback;
	};
	auto intOf = [&section](const char* key, int fallback) {
		const auto value = section->find(key);
		return value != section->end() && value->is_number_integer() ? value->get<int>() : fallback;
	};
	auto strOf = [&section](const char* key, const std::string& fallback) {
		const auto value = section->find(key);
		return value != section->end() && value->is_string() ? value->get<std::string>() : fallback;
	};
	G::Vars.killFeedbackEnabled = boolOf("Enabled", G::Vars.killFeedbackEnabled);
	G::Vars.killFeedbackCommon = boolOf("Common", G::Vars.killFeedbackCommon);
	G::Vars.killFeedbackSpecial = boolOf("Special", G::Vars.killFeedbackSpecial);
	G::Vars.killFeedbackSmoker = boolOf("Smoker", G::Vars.killFeedbackSmoker);
	G::Vars.killFeedbackBoomer = boolOf("Boomer", G::Vars.killFeedbackBoomer);
	G::Vars.killFeedbackHunter = boolOf("Hunter", G::Vars.killFeedbackHunter);
	G::Vars.killFeedbackSpitter = boolOf("Spitter", G::Vars.killFeedbackSpitter);
	G::Vars.killFeedbackJockey = boolOf("Jockey", G::Vars.killFeedbackJockey);
	G::Vars.killFeedbackCharger = boolOf("Charger", G::Vars.killFeedbackCharger);
	G::Vars.killFeedbackTank = boolOf("Tank", G::Vars.killFeedbackTank);
	G::Vars.killFeedbackWitch = boolOf("Witch", G::Vars.killFeedbackWitch);
	G::Vars.killFeedbackIcon = boolOf("Icon", G::Vars.killFeedbackIcon);
	G::Vars.killFeedbackSound = boolOf("Sound", G::Vars.killFeedbackSound);
	G::Vars.killFeedbackFirearm = boolOf("Firearm", G::Vars.killFeedbackFirearm);
	G::Vars.killFeedbackHeadshot = boolOf("Headshot", G::Vars.killFeedbackHeadshot);
	G::Vars.killFeedbackMelee = boolOf("Melee", G::Vars.killFeedbackMelee);
	G::Vars.killFeedbackExplosion = boolOf("Explosion", G::Vars.killFeedbackExplosion);
	G::Vars.killFeedbackMultiKill = boolOf("MultiKill", G::Vars.killFeedbackMultiKill);
	G::Vars.killFeedbackSiDedicated = boolOf("SiDedicated", G::Vars.killFeedbackSiDedicated);
	G::Vars.killFeedbackSiSound = boolOf("SiSound", G::Vars.killFeedbackSiSound);
	G::Vars.killFeedbackHitMode = std::clamp(intOf("HitMode", G::Vars.killFeedbackHitMode), 0, 2);
	G::Vars.killFeedbackSoundVolume =
		std::clamp(intOf("SoundVolume", G::Vars.killFeedbackSoundVolume), 0, 100);
	m_siTheme = strOf("SiTheme", m_siTheme);
	m_ciTheme = strOf("CiTheme", m_ciTheme);
}

void KillFeedback::SaveConfig(nlohmann::json& doc) const {
	auto& section = NecolaConfig::EnsureSectionObject(doc, "KillFeedback");
	section["Enabled"] = G::Vars.killFeedbackEnabled;
	section["Common"] = G::Vars.killFeedbackCommon;
	section["Special"] = G::Vars.killFeedbackSpecial;
	section["Smoker"] = G::Vars.killFeedbackSmoker;
	section["Boomer"] = G::Vars.killFeedbackBoomer;
	section["Hunter"] = G::Vars.killFeedbackHunter;
	section["Spitter"] = G::Vars.killFeedbackSpitter;
	section["Jockey"] = G::Vars.killFeedbackJockey;
	section["Charger"] = G::Vars.killFeedbackCharger;
	section["Tank"] = G::Vars.killFeedbackTank;
	section["Witch"] = G::Vars.killFeedbackWitch;
	section["Icon"] = G::Vars.killFeedbackIcon;
	section["Sound"] = G::Vars.killFeedbackSound;
	section["Firearm"] = G::Vars.killFeedbackFirearm;
	section["Headshot"] = G::Vars.killFeedbackHeadshot;
	section["Melee"] = G::Vars.killFeedbackMelee;
	section["Explosion"] = G::Vars.killFeedbackExplosion;
	section["MultiKill"] = G::Vars.killFeedbackMultiKill;
	section["SiDedicated"] = G::Vars.killFeedbackSiDedicated;
	section["SiSound"] = G::Vars.killFeedbackSiSound;
	section["HitMode"] = G::Vars.killFeedbackHitMode;
	section["SoundVolume"] = G::Vars.killFeedbackSoundVolume;
	section["SiTheme"] = m_siTheme;
	section["CiTheme"] = m_ciTheme;
}

void KillFeedback::SaveConfig() const {
	nlohmann::json doc = NecolaConfig::LoadConfig();
	SaveConfig(doc);
	NecolaConfig::SaveConfig(doc);
}

// ------------------------------------------------------------- fx channel ----
// skeeto's SKT_Fx_DispatchNamedFx + DetectCrown: the fixed keys below are looked
// up in the theme and played like a kill style, at the victim's world position.
bool KillFeedback::DispatchFx(const char* key, const Vector& position) {
	if (!key || !*key) return false;
	LoadThemes();
	const KillTheme* themes[2] = {nullptr, nullptr};
	if (m_siTheme != "off") themes[0] = FindTheme("si", m_siTheme);
	if (m_ciTheme != "off") themes[1] = FindTheme("ci", m_ciTheme);
	for (const KillTheme* theme : themes) {
		if (!theme) continue;
		const auto it = theme->fx.find(key);
		if (it == theme->fx.end()) continue;
		Log("fx %s overlay=%s", key, it->second.overlay.c_str());
		PlayStyle(it->second, kKillDurationMs, /*allowRepeat=*/true);
		return true;
	}
	return false;
}
