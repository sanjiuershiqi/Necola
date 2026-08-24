#pragma once

#include "../../../sdk/SDK.h"
#include "../../../sdk/l4d2/entities/IGameEventListener2.h"

#include <cstdint>

class HitFeedbackListener final : public IGameEventListener2 {
public:
	void FireGameEvent(IGameEvent* event) override;
	int GetEventDebugID() override { return 42; }
};

// Ported from the skeeto overlay analysis (Render_HitMarkers / Timer_Draw):
// 20 damage slots with 90ms same-entity accumulation, 860ms fade-out,
// upward-drifting damage numbers with a shadow pass and a 10-color palette,
// plus a short crosshair hit marker for special-infected / witch hits.
class HitFeedback {
public:
	bool InitListeners();
	bool InstallHealthProxy();
	void UnhookHealthProxy();
	bool HasHealthProxy() const { return m_healthProxyInstalled; }
	void LoadConfig(const nlohmann::json& doc);
	void SaveConfig(nlohmann::json& doc) const;
	void SaveConfig() const;

	void OnGameEvent(IGameEvent* event);
	void OnHealthChanged(int entIndex, int oldHealth, int newHealth);
	void ArmLocalAttack();
	void Pump();
	void Draw();
	void Reset();
	void Shutdown();

private:
	static constexpr int kMaxSlots = 20;
	static constexpr int kSlotMergeWindowMs = 90;
	static constexpr float kSlotLifetimeMs = 860.0f;
	static constexpr std::uint32_t kHitMarkerDurationMs = 300;
	static constexpr int kPaletteSize = 10;

	struct DamageSlot {
		int entIndex = 0;
		int damage = 0;
		float worldX = 0.0f;
		float worldY = 0.0f;
		float worldZ = 0.0f;
		float headZ = 0.0f;
		float angle = 0.0f;
		std::uint32_t tick = 0;
		std::uint8_t colorIdx = 0;
		bool used = false;
	};

	void RecordDamage(int entIndex, int damage, float x, float y, float z, float headZ);
	void ProcessSpecialHit(int entIndex, int damage, const Vector& origin, float headZ, bool fromProxy);
	void PumpHealthChanges();
	void ProcessHealthChange(int entIndex, int oldHealth, int newHealth);
	bool IsLocalHitCorrelated(int entIndex) const;
	bool IsWitchEntity(int entIndex) const;
	bool IsSpecialEntity(int entIndex) const;
	void TriggerHitMarker();

	DamageSlot m_slots[kMaxSlots];
	std::uint32_t m_lastColorIdx = 0;
	std::uint32_t m_hitMarkerUntil = 0;
	HFont m_damageFont = 0;
	int m_damageFontSize = 0;
	HitFeedbackListener m_listener;
	bool m_listenersRegistered = false;
	bool m_healthProxyInstalled = false;
	std::uint32_t m_lastAttackTick = 0;
	std::uint32_t m_lastImpactTick = 0;
	std::uint32_t m_lastSpecialHitTick = 0;
	int m_lastSpecialHitEntity = 0;
	int m_lastSpecialHitDamage = 0;
	bool m_lastSpecialHitFromProxy = false;
	struct PendingHealthChange {
		int entIndex = 0;
		int oldHealth = 0;
		int newHealth = 0;
	};
	static constexpr int kMaxPendingHealthChanges = 32;
	PendingHealthChange m_pendingHealthChanges[kMaxPendingHealthChanges];
	int m_pendingHealthChangeCount = 0;
};

namespace F { inline HitFeedback HitFeedbackMgr; }
