#pragma once

#include "../../../sdk/SDK.h"

#include <cstdint>

// Renderer only: floating damage numbers + the crosshair hit marker.
//
// Detection lives in Feature/DamagePipeline (a port of skeeto's
// QueueDamageEvent -> ConsumePendingDamage -> Si_TrackHitFeedback chain). This
// class receives already-confirmed hits through AddNumber()/FlashMarker() and
// owns nothing but the drawing state, so the two concerns can no longer drift
// into each other the way they did before.
class HitFeedback {
public:
	void LoadConfig(const nlohmann::json& doc);
	void SaveConfig(nlohmann::json& doc) const;
	void SaveConfig() const;

	// Confirmed hit (see DamagePipeline): spawn or merge a damage number.
	void AddNumber(int entIndex, int damage, const Vector& origin, float headZ);
	// Confirmed hit: flash the crosshair marker for kHitMarkerDurationMs.
	void FlashMarker();

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

	DamageSlot m_slots[kMaxSlots];
	std::uint32_t m_lastColorIdx = 0;
	std::uint32_t m_hitMarkerUntil = 0;
	HFont m_damageFont = 0;
	int m_damageFontSize = 0;
};

namespace F { inline HitFeedback HitFeedbackMgr; }
