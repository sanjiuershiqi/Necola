#pragma once

#include "../../../sdk/SDK.h"

#include <cstdint>
#include <string>
#include <unordered_map>

enum class KillFeedbackEffect {
	Kill1,
	Kill2,
	Kill3,
	Kill4,
	Kill5,
	Kill6,
	Headshot,
	Melee,
	Explosion,
};

class KillFeedback {
public:
	void LoadConfig(const nlohmann::json& doc);
	void SaveConfig(nlohmann::json& doc) const;
	void SaveConfig() const;

	void OnGameEvent(IGameEvent* event);
	void Draw();
	void Stop();
	void Reset();
	void Shutdown();
	void Preview(KillFeedbackEffect effect);
	void PrintStatus() const;

private:
	enum class KillMethod {
		Firearm,
		Headshot,
		Melee,
		Explosion,
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
	void HandleSpecialKill(SpecialVictim victim, KillMethod method, int victimUserId, const char* source);
	void QueueSpecialKill(SpecialVictim victim, KillMethod method, int victimUserId, const char* source);
	void FlushPendingSpecialKills();
	KillMethod ClassifyCommonKill(IGameEvent* event) const;
	KillMethod ClassifySpecialKill(IGameEvent* event) const;
	KillMethod ClassifyWitchKill(IGameEvent* event) const;
	KillMethod ClassifyTrackedDamage(IGameEvent* event) const;
	bool IsMethodEnabled(KillMethod method) const;
	bool IsWitchEntity(int entityId) const;
	void Trigger(KillMethod method);
	void StartEffect(KillFeedbackEffect effect, int streakSound);
	bool BindFrameTexture(int frame);
	void PlayEffectSound(KillFeedbackEffect effect, int streakSound) const;

	static const char* EffectName(KillFeedbackEffect effect);
	static int EffectStreak(KillFeedbackEffect effect);
	static const char* MethodName(KillMethod method);
	static const char* SpecialVictimName(SpecialVictim victim);

	KillFeedbackEffect m_effect = KillFeedbackEffect::Kill1;
	float m_animationStart = 0.0f;
	float m_lastKillTime = -1000.0f;
	int m_streak = 0;
	int m_boundFrame = -1;
	int m_textureId = -1;
	unsigned int m_frameCache = 0;
	KillFeedbackEffect m_boundTextureEffect = KillFeedbackEffect::Kill1;
	bool m_textureBound = false;
	bool m_animating = false;
	bool m_drawLogged = false;
	struct DamageRecord {
		KillMethod method = KillMethod::Firearm;
		float time = 0.0f;
	};
	struct PlayerDamageRecord {
		SpecialVictim victim = SpecialVictim::Unknown;
		KillMethod method = KillMethod::Firearm;
		float time = 0.0f;
	};
	struct PendingSpecialKill {
		SpecialVictim victim = SpecialVictim::Unknown;
		KillMethod method = KillMethod::Firearm;
		float time = 0.0f;
		const char* source = "unknown";
	};
	std::unordered_map<int, DamageRecord> m_infectedDamage;
	std::unordered_map<int, PlayerDamageRecord> m_playerDamage;
	std::unordered_map<int, PendingSpecialKill> m_pendingSpecialKills;
	int m_lastSpecialVictimUserId = 0;
	float m_lastSpecialKillTime = -1000.0f;
	int m_lastCommonEntityId = 0;
	float m_lastCommonKillTime = -1000.0f;
};

namespace F { inline KillFeedback KillFeedbackMgr; }
