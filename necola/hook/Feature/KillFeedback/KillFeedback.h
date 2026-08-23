#pragma once

#include "../../../sdk/SDK.h"
#include "../../../sdk/l4d2/entities/IGameEventListener2.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

class KillFeedbackListener final : public IGameEventListener2 {
public:
	void FireGameEvent(IGameEvent* event) override;
	int GetEventDebugID() override { return 42; }
};

// skeeto-style kill feedback: single-frame fullscreen icons and sounds from the
// skeeto_killfeed.vpk addon (skeeto/ci/<theme> for commons, skeeto/si/<theme>
// 1kill..10kill / headshot / knifed for special infected streaks).
class KillFeedback {
public:
	void LoadConfig(const nlohmann::json& doc);
	void SaveConfig(nlohmann::json& doc) const;
	void SaveConfig() const;

	bool InitListeners();
	void OnGameEvent(IGameEvent* event);
	void RefreshSpecialVictims();
	void Draw();
	void Stop();
	void Reset();
	void Shutdown();
	// kind: 0 common kill, 1 common headshot, 2 common melee,
	//       3 special kill, 4 special headshot, 5 special melee,
	//       6 special 3-streak, 7 special 10-streak.
	void Preview(int kind);
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
	void Trigger(KillMethod method, bool special);
	void StartEffect(KillMethod method, int streakSound, bool special);
	bool BindMaterial();
	void PlayEffectSound(const char* sound) const;
	void ResolveAssets(char* material, std::size_t materialSize,
		char* sound, std::size_t soundSize) const;

	static const char* MethodName(KillMethod method);
	static const char* SpecialVictimName(SpecialVictim victim);
	static SpecialVictim SpecialVictimFromZombieClass(int zombieClass);
	static SpecialVictim SpecialVictimFromAbility(const char* ability);

	KillMethod m_method = KillMethod::Firearm;
	int m_streakSound = 1;
	bool m_special = false;
	float m_animationStart = 0.0f;
	float m_lastKillTime = -1000.0f;
	int m_streak = 0;
	int m_boundFrame = -1;
	int m_textureId = -1;
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
	std::unordered_map<int, SpecialVictim> m_specialVictims;
	KillFeedbackListener m_listener;
	bool m_listenersRegistered = false;
	int m_lastSpecialVictimUserId = 0;
	float m_lastSpecialKillTime = -1000.0f;
	int m_lastCommonEntityId = 0;
	float m_lastCommonKillTime = -1000.0f;
	float m_lastVictimRefresh = -1000.0f;
};

namespace F { inline KillFeedback KillFeedbackMgr; }
