#pragma once

#include "../../../sdk/SDK.h"
#include "../../../sdk/l4d2/entities/IGameEventListener2.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class KillFeedbackListener final : public IGameEventListener2 {
public:
	void FireGameEvent(IGameEvent* event) override;
	int GetEventDebugID() override { return 42; }
};

// Faithful port of skeeto's kill feedback:
//  - Themes loaded from skeeto/skeeto_<id>.json (mounted via skeeto_killfeed.vpk)
//  - Style selection with priority override (hit / streak_N / melee / headshot / kill)
//  - Render_ScreenOverlay: overlay OR overlay_sequence (multi-frame material
//    animation) plus screen/world particle dispatch, with a pump that steps the
//    sequence every overlay_step_ms and clears `r_screenoverlay` when it ends
//  - Sound_PlayVol: VGUI PlaySound / engine ambient sound (skeeto uses
//    `playvol`; this port deliberately avoids touching the sound-script cache)
//  - Commands unlocked once via Cmd_Unlock (clear FCVAR_CHEAT, add
//    FCVAR_CLIENTCMD_CAN_EXECUTE) exactly like skeeto's Cmd_PlaySound
//
// Field layout and limits below mirror what we recovered from skeeto.dll's
// 4444-byte hit-feedback event block (docs 25/33/34 of the Skeeto RE):
//   overlay[96] | overlay_sequence[30][96] | count | overlay_step_ms (<=1000)
//   particle[64] | particles[8][64] | sound[96] | sounds[8][96]
//   priority | overlay_ms (<=5000) | has_content | world
// skeeto also AUTO-EXTENDS overlay_ms when it is shorter than
// frames*overlay_step_ms, so the last frame is never cut off.
struct KillStyle {
	static constexpr int kMaxOverlayFrames = 30;   // overlay_sequence[30]
	static constexpr int kMaxOverlayStepMs = 1000; // overlay_step_ms clamp
	static constexpr int kMinOverlayStepMs = 16;   // documented minimum
	static constexpr int kMaxOverlayMs = 5000;     // overlay_ms clamp
	static constexpr int kMaxParticles = 8;        // particles[8]
	static constexpr int kMaxSounds = 8;           // sounds[8]

	bool enabled = false;
	std::string overlay;
	std::vector<std::string> overlaySequence; // overlay_sequence[] (<= kMaxOverlayFrames)
	int overlayStepMs = 0;                    // overlay_step_ms (16..1000)
	std::string particle;
	std::vector<std::string> particles;
	std::string sound;
	std::vector<std::string> sounds;
	int duration = 0;
	int priority = 0;
	bool world = false;

	// True when there is enough material to animate a multi-frame overlay.
	bool HasOverlaySequence() const {
		return overlaySequence.size() >= 2 && overlayStepMs > 0;
	}
};

struct KillTheme {
	std::string id;
	std::string name;
	std::string channel;
	bool streakEnabled = false;
	int streakWrap = 0;
	int streakPriority = 10;
	KillStyle hit;
	KillStyle kill;
	KillStyle headshot;
	KillStyle melee;
	KillStyle streak[11];
	KillStyle streakDefault;
	bool loaded = false;
};

class KillFeedback {
public:
	void LoadConfig(const nlohmann::json& doc);
	void SaveConfig(nlohmann::json& doc) const;
	void SaveConfig() const;

	bool InitListeners();
	void LoadThemes();
	void WarmupParticles();
	void OnGameEvent(IGameEvent* event);
	void OnSpecialHealthDrop(const Vector& position);
	void RefreshSpecialVictims();
	void Draw();
	void Stop();
	void Reset();
	void Shutdown();
	void Preview(int kind);
	void PrintStatus() const;

	// HitFeedback_Check / HitFeedback_Check2 port: kill=true renders the kill
	// style (240ms), kill=false renders the hit style (110ms). Returns true
	// when a style was resolved and rendered.
	bool ResolveCombinedFeedback(bool kill, bool headshot, bool melee, int streak,
		bool allowSi, bool allowCi);

	const KillTheme* FindTheme(const char* channel, const std::string& themeId) const;
	const std::vector<KillTheme>& Themes() const { return m_themes; }
	const std::string& SelectedTheme(const char* channel) const;
	void SetTheme(const char* channel, const std::string& themeId);

private:
	struct KillClassification {
		bool headshot = false;
		bool melee = false;
		bool explosion = false;
	};
	enum class SpecialVictim {
		Unknown,
		Smoker,
		Boomer,
		Hunter,
		Spitter,
		Jockey,
		Charger,
		Tank,
		Witch,
	};

	bool IsLocalAttacker(IGameEvent* event, const char* field) const;
	SpecialVictim GetSpecialVictim(IGameEvent* event) const;
	bool IsSpecialVictimEnabled(SpecialVictim victim) const;
	void HandleSpecialKill(SpecialVictim victim, KillClassification classification, int victimUserId, const char* source);
	void QueueSpecialKill(SpecialVictim victim, KillClassification classification, int victimUserId, const char* source);
	void FlushPendingSpecialKills();
	KillClassification ClassifyCommonKill(IGameEvent* event) const;
	KillClassification ClassifySpecialKill(IGameEvent* event) const;
	KillClassification ClassifyTrackedDamage(IGameEvent* event) const;
	bool IsClassificationEnabled(const KillClassification& classification) const;
	bool IsWitchEntity(int entityId) const;
	void Trigger(bool special, const KillClassification& classification, int streak);
	bool HitCheck(const KillTheme& theme, const char* eventName, bool headshot, bool melee,
		int streak, KillStyle& out) const;
	void RenderScreenOverlay(const KillStyle& style, int defaultDuration, bool allowRepeat);
	void PumpOverlay();
	void SendScreenOverlay(const char* material);
	void PlaySoundVol(const KillStyle& style);
	void PrecacheParticles(const KillStyle& style);
	void SpawnParticles(const KillStyle& style);
	void CaptureFeedbackPosition(IGameEvent* event, int entityIndex);
	void PumpCommonHitTrace();
	bool UnlockCommands(bool needOverlay);

	static const char* ClassificationName(const KillClassification& classification);
	static const char* SpecialVictimName(SpecialVictim victim);
	static SpecialVictim SpecialVictimFromZombieClass(int zombieClass);
	static SpecialVictim SpecialVictimFromAbility(const char* ability);

	std::vector<KillTheme> m_themes;
	std::string m_siTheme = "si_cf";
	std::string m_ciTheme = "ci_cf";
	std::uint32_t m_fxOverlayTick = 0;
	std::uint32_t m_fxPumpTick = 0;
	bool m_overlayActive = false;
	// Active overlay sequence state (skeeto overlay_sequence + overlay_step_ms).
	// When the style has no sequence these stay empty/zero and the single-shot
	// `overlay` path is used exactly as before.
	std::vector<std::string> m_activeOverlaySequence;
	std::size_t m_overlayFrame = 0;
	std::uint32_t m_overlayNextFrameTick = 0;
	int m_overlayStepMs = 0;
	bool m_commandsUnlocked = false;
	bool m_themesLoaded = false;
	float m_lastThemeLoadAttempt = -1000.0f;
	bool m_themeFailureReported = false;
	int m_streak = 0;
	struct DamageRecord {
		KillClassification classification;
		float time = 0.0f;
	};
	struct PlayerDamageRecord {
		SpecialVictim victim = SpecialVictim::Unknown;
		KillClassification classification;
		float time = 0.0f;
	};
	struct PendingSpecialKill {
		SpecialVictim victim = SpecialVictim::Unknown;
		KillClassification classification;
		float time = 0.0f;
		const char* source = "unknown";
		Vector position;
		bool positionValid = false;
	};
	struct FeedbackPosition {
		Vector value;
		bool valid = false;
	};
	std::unordered_map<int, DamageRecord> m_infectedDamage;
	std::unordered_map<int, PlayerDamageRecord> m_playerDamage;
	std::unordered_map<int, PendingSpecialKill> m_pendingSpecialKills;
	std::unordered_map<int, SpecialVictim> m_specialVictims;
	std::unordered_set<std::string> m_precachedParticles;
	// Particle dispatch rate limit (skeeto: at most 32 dispatches per 1000 ms).
	std::uint32_t m_particleWindowStart = 0;
	int m_particleWindowCount = 0;
	FeedbackPosition m_feedbackPosition;
	FeedbackPosition m_lastImpactPosition;
	FeedbackPosition m_pendingImpactPosition;
	std::uint32_t m_pendingImpactTick = 0;
	std::uint32_t m_lastCommonTraceTick = 0;
	std::vector<FeedbackPosition> m_pendingSpecialHits;
	KillFeedbackListener m_listener;
	bool m_listenersRegistered = false;
	int m_lastSpecialVictimUserId = 0;
	float m_lastSpecialKillTime = -1000.0f;
	int m_lastCommonEntityId = 0;
	float m_lastCommonKillTime = -1000.0f;
	float m_lastVictimRefresh = -1000.0f;
};

namespace F { inline KillFeedback KillFeedbackMgr; }
