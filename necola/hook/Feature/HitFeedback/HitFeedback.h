#pragma once

#include "../../../sdk/SDK.h"
#include "../../../sdk/l4d2/entities/IGameEventListener2.h"

#include <cstdint>

class HitFeedbackListener final : public IGameEventListener2 {
public:
	void FireGameEvent(IGameEvent* event) override;
	int GetEventDebugID() override { return 42; }
};

// Damage numbers + crosshair hit marker.
//
// The detection core is a rewrite of skeeto's damage pipeline
// (SKT_Stats_QueueDamageEvent -> SKT_Stats_ConsumePendingDamage ->
// SKT_Si_TrackHitFeedback). The old implementation tried to decide inline, in the
// event callback, with a half-installed health proxy and special-cases per victim
// type; that is what kept regressing. This version follows skeeto's split:
//
//   ingress (event)   -> append a 400 ms-stamped record, nothing else
//   drain   (per frame) -> drop stale / wrong-generation / wrong-entity records
//   gate                -> far cone (gun) -> 70 ms throttle -> near cone (non-gun)
//                          -> victim lock 400 ms (repeats inside it bypass the gate)
//   dispatch            -> RecordDamage() (number) + TriggerHitMarker()
//
// Rendering (20 slots, 90 ms merge, 860 ms fade, palette, marker) is unchanged.
class HitFeedback {
public:
	bool InitListeners();
	void LoadConfig(const nlohmann::json& doc);
	void SaveConfig(nlohmann::json& doc) const;
	void SaveConfig() const;

	void OnGameEvent(IGameEvent* event);
	void ArmLocalAttack();
	void Pump();
	void Draw();
	void Reset();
	void Shutdown();

	// Kept for the status line; the health proxy is intentionally not installed
	// (it would fight other plugins over the m_iHealth RecvProp chain).
	bool HasHealthProxy() const { return false; }

private:
	static constexpr int kMaxSlots = 20;
	static constexpr int kSlotMergeWindowMs = 90;
	static constexpr float kSlotLifetimeMs = 860.0f;
	static constexpr std::uint32_t kHitMarkerDurationMs = 300;
	static constexpr int kPaletteSize = 10;

	// ---- queue (skeeto: 64 records, drained 32 per frame) ----
	static constexpr int kMaxPendingHits = 64;
	static constexpr int kMaxDrainPerFrame = 32;
	// skeeto rejects a record that is older than 400 ms when it is drained.
	static constexpr std::uint32_t kPendingLifetimeMs = 400;

	// ---- gate (SKT_Si_TrackHitFeedback) ----
	static constexpr std::uint32_t kHitFeedbackThrottleMs = 70;
	static constexpr std::uint32_t kVictimLockMs = 400;
	static constexpr float kFarConeDistance = 5000.0f;
	static constexpr float kFarConeDot = 0.93f;
	static constexpr float kNearConeDistance = 110.0f;
	static constexpr float kNearConeDot = 0.90f;

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

	struct PendingHit {
		std::uint32_t tick = 0;
		std::uint32_t mapGen = 0;
		int entIndex = 0;   // victim entity index
		int userId = 0;     // victim user id (0 when the entity has none)
		int damage = 0;
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
		float headZ = 50.0f;
		bool gun = true;    // gun hit: far cone only; otherwise near cone
		bool used = false;
	};

	void EnqueueHit(const PendingHit& hit);
	void DrainPendingHits();
	bool GateHit(const PendingHit& hit);
	bool IsInViewConeAtRange(int entIndex, float maxDistance, float minDot) const;
	bool IsWitchEntity(int entIndex) const;
	bool IsSpecialEntity(int entIndex) const;
	bool IsSpecialEntity(int entIndex) const;
	void RecordDamage(int entIndex, int damage, float x, float y, float z, float headZ);
	void TriggerHitMarker();

	DamageSlot m_slots[kMaxSlots];
	std::uint32_t m_lastColorIdx = 0;
	std::uint32_t m_hitMarkerUntil = 0;
	HFont m_damageFont = 0;
	int m_damageFontSize = 0;
	HitFeedbackListener m_listener;
	bool m_listenersRegistered = false;
	std::uint32_t m_lastAttackTick = 0;
	std::uint32_t m_lastImpactTick = 0;

	// queue + generation
	PendingHit m_pending[kMaxPendingHits];
	int m_pendingCount = 0;
	std::uint32_t m_mapGeneration = 1;

	// gate state
	std::uint32_t m_lastFeedbackTick = 0;
	int m_lockUserId = 0;
	int m_lockEntity = 0;
	std::uint32_t m_lockUntil = 0;

	// diagnostics
	int m_logBudget = 20;
};

namespace F { inline HitFeedback HitFeedbackMgr; }
