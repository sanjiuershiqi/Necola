#pragma once

#include "../../../sdk/SDK.h"
#include "../../../sdk/l4d2/entities/IGameEventListener2.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Themed hit / kill feedback — rewritten from scratch against skeeto's model.
//
// Data model (skeeto's 4444-byte event block, field for field):
//   overlay            single material
//   overlay_sequence[] multi-frame material animation, <= 30 frames
//   overlay_step_ms    frame step, 16..1000
//   overlay_ms         display time, <= 5000, AUTO-EXTENDED to frames*step
//   particle / particles[]   <= 8, screen or world space ('world')
//   sound / sounds[]         <= 8
//   priority           higher wins inside one channel
//
// Style resolution follows skeeto's CollectBestEventBlock: for 'hit' only the
// hit style competes; for a kill the candidates are streak_N / kill, then melee,
// then headshot, each compared strictly by priority. The SI and CI channels are
// resolved independently and merged (visual preference + sound preference),
// because a kill can be both.
//
// Rendering: r_screenoverlay for the material (one command per changed frame),
// DispatchParticleEffect for particles with a 32-per-second limit, and
// MatSystemSurface::PlaySound / EmitAmbientSound for audio. Nothing here decides
// whether a hit happened — DamagePipeline owns that and calls
// OnSpecialHealthDrop(); kills are resolved from the game events below.
class KillFeedbackListener final : public IGameEventListener2 {
public:
	void FireGameEvent(IGameEvent* event) override;
	int GetEventDebugID() override { return 42; }
};

struct KillStyle {
	static constexpr int kMaxOverlayFrames = 30;
	static constexpr int kMaxOverlayStepMs = 1000;
	static constexpr int kMinOverlayStepMs = 16;
	static constexpr int kMaxOverlayMs = 5000;
	static constexpr int kMaxParticles = 8;
	static constexpr int kMaxSounds = 8;

	bool enabled = false;
	std::string overlay;
	std::vector<std::string> overlaySequence;
	int overlayStepMs = 0;
	std::string particle;
	std::vector<std::string> particles;
	std::string sound;
	std::vector<std::string> sounds;
	int duration = 0;
	int priority = 0;
	bool world = false;

	bool HasSequence() const { return overlaySequence.size() >= 2 && overlayStepMs > 0; }
};

struct KillTheme {
	std::string id;
	std::string name;
	std::string channel; // "si" | "ci"
	int sort = 0;          // theme ordering (skeeto's 'sort')
	std::string note;     // free-form comment (skeeto's 'note')
	bool streakEnabled = false;
	int streakWrap = 0;
	int streakPriority = 10;
	KillStyle hit;
	KillStyle kill;
	KillStyle headshot;
	KillStyle melee;
	KillStyle streak[11];
	KillStyle streakDefault;
	std::unordered_map<std::string, KillStyle> fx;
	bool loaded = false;
};

class KillFeedback {
public:
	bool InitListeners();	void HandleEvent(IGameEvent* event);
	void LoadConfig(const nlohmann::json& doc);
	void SaveConfig(nlohmann::json& doc) const;
	void SaveConfig() const;

	void LoadThemes();
	// Sink for a confirmed local hit (DamagePipeline).
	void OnSpecialHealthDrop(const Vector& position);
	void RefreshSpecialVictims();

	void Draw();
	void Stop();
	void Reset();
	void Shutdown();
	void Preview(int kind);
	void PrintStatus() const;

	bool ResolveCombinedFeedback(bool kill, bool headshot, bool melee, int streak,
		bool allowSi, bool allowCi);

	const std::vector<KillTheme>& Themes() const { return m_themes; }
	const KillTheme* FindTheme(const char* channel, const std::string& themeId) const;
	const std::string& SelectedTheme(const char* channel) const;
	void SetTheme(const char* channel, const std::string& themeId);

private:
	struct Classification {
		bool headshot = false;
		bool melee = false;
		bool explosion = false;
	};
	enum class Victim { Unknown, Smoker, Boomer, Hunter, Spitter, Jockey, Charger, Tank, Witch };

	struct PendingHit {
		Vector position;
		bool valid = false;
		float time = 0.0f;
	};
	struct PendingKill {
		Victim victim = Victim::Unknown;
		Classification classification;
		int userId = 0;
		Vector position;
		bool valid = false;
		float time = 0.0f;
		const char* source = "event";
	};

	bool IsLocalAttacker(IGameEvent* event, const char* field) const;
	Victim VictimFromZombieClass(int zombieClass) const;
	Victim VictimFromEvent(IGameEvent* event) const;
	bool IsVictimEnabled(Victim victim) const;
	bool IsMethodEnabled(const Classification& classification) const;
	Classification Classify(IGameEvent* event, bool special) const;
	// Port of SKT_Evt_CheckWeaponKill (0x100389F0): is this death attributable to a
	// player weapon? 250 ms same-attacker fast path, then a deny list, then a
	// held-weapon class fallback.
	bool IsWeaponKill(IGameEvent* event) const;
	bool HitCheck(const KillTheme& theme, const char* eventName, bool headshot, bool melee,
		int streak, KillStyle& out) const;
	void Trigger(bool special, const Classification& classification, int streak);
	// skeeto's fixed fx keys (MatchFxKey + DetectCrown): skeet, melee_skeet,
	// headshot_skeet, crit_text, melee_crown, perfect_crown, crown.
	bool DispatchFx(const char* key, const Vector& position);
	void HandleKill(const PendingKill& kill);

	void PlayStyle(const KillStyle& style, int defaultDuration, bool allowRepeat,
		const Vector* origin = nullptr);
	void StepOverlay();
	void SendOverlay(const char* material);
	void ClearOverlay();
	void PrecacheStyle(const KillStyle& style);
	void SpawnParticles(const KillStyle& style, const Vector* origin);
	void PlaySound(const KillStyle& style);
	void WarmupParticles();

	std::vector<KillTheme> m_themes;
	std::string m_siTheme = "si_cf";
	std::string m_ciTheme = "ci_cf";
	int m_streak = 0;
	bool m_themesLoaded = false;
	float m_lastThemeLoad = -1000.0f;
	bool m_themeWarned = false;

	KillFeedbackListener m_listener;
	bool m_listenersRegistered = false;

	std::vector<PendingHit> m_pendingHits;
	std::vector<PendingKill> m_pendingKills;

	std::unordered_set<std::string> m_precached;
	std::vector<std::string> m_activeSequence;
	std::size_t m_sequenceFrame = 0;
	int m_sequenceStepMs = 0;
	std::uint32_t m_nextFrameTick = 0;
	std::uint32_t m_overlayUntil = 0;
	bool m_overlayActive = false;
	std::uint32_t m_lastPlayTick = 0;
	std::uint32_t m_particleWindowStart = 0;
	int m_particleWindowCount = 0;

	int m_lastVictimUserId = 0;
	float m_lastVictimTime = -1000.0f;
	int m_lastCommonEntity = 0;
	float m_lastCommonTime = -1000.0f;
	float m_lastRefresh = -1000.0f;
	std::unordered_map<int, Victim> m_victims;
	std::unordered_map<int, Classification> m_tracked;
	// Hunter/Jockey currently pouncing or riding (skeet-family gating).
	std::unordered_set<int> m_airborne;
	int m_logBudget = 30;
	// Victim position of the current event, so world particles (crown/skeet fx) spawn
	// on the target instead of the local player. skeeto reads victim_x/y/z for this.
	Vector m_eventPosition;
	// SKT_Evt_CheckWeaponKill's 250 ms repeat cache.
	mutable std::uint32_t m_lastWeaponKillTick = 0;
	mutable int m_lastWeaponKillAttacker = 0;
	bool m_eventPositionValid = false;
};

namespace F { inline KillFeedback KillFeedbackMgr; }
