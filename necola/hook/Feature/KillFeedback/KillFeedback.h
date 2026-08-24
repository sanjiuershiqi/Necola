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
//  - Render_ScreenOverlay: overlay plus screen/world particle dispatch, with a
//    pump that clears `r_screenoverlay` when the duration elapses
//  - Sound_PlayVol: `playvol <file> <volume>` client command
//  - Commands unlocked once via Cmd_Unlock (clear FCVAR_CHEAT, add
//    FCVAR_CLIENTCMD_CAN_EXECUTE) exactly like skeeto's Cmd_PlaySound.
struct KillStyle {
	bool enabled = false;
	std::string overlay;
	std::string particle;
	std::vector<std::string> particles;
	std::string sound;
	std::vector<std::string> sounds;
	int duration = 0;
	int priority = 0;
	bool world = false;
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
	void PlaySoundVol(const KillStyle& style);
	void PrecacheParticles(const KillStyle& style);
	void WarmParticles();
	void SpawnParticles(const KillStyle& style);
	void CaptureFeedbackPosition(IGameEvent* event, int entityIndex);
	void PumpCommonHitTrace();
	bool UnlockCommands();

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
	bool m_commandsUnlocked = false;
	bool m_themesLoaded = false;
	float m_lastThemeLoadAttempt = -1000.0f;
	bool m_themeFailureReported = false;
	bool m_particlesWarmed = false;
	float m_particleWarmReadyAt = -1.0f;
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
