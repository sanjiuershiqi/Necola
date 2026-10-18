#include "HitFeedback.h"

#include "../../../sdk/utils/FeatureConfigManager.h"
#include "../../../sdk/l4d2/includes/dt_recv.h"
#include "../../../sdk/l4d2/interfaces/BaseClientDLL.h"
#include "../../Vars.h"
#include "../KillFeedback/KillFeedback.h"
#include "../../../diag.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <utility>
#include <vector>

namespace {
constexpr float kTwoPi = 6.2831855f;
constexpr float kHalfPiShift = 4.712389f;

std::uint32_t NowTick() {
	return static_cast<std::uint32_t>(GetTickCount());
}

bool IsLocalAttacker(IGameEvent* event, const char* field) {
	if (!event || !I::EngineClient) return false;
	const int attacker = event->GetInt(field, 0);
	if (attacker <= 0) return false;
	const int resolved = I::EngineClient->GetPlayerForUserID(attacker);
	return resolved > 0 && resolved == I::EngineClient->GetLocalPlayer();
}

bool ContainsNoCase(const char* value, const char* needle) {
	if (!value || !needle) return false;
	const std::size_t needleLen = std::strlen(needle);
	for (std::size_t i = 0; value[i]; ++i) {
		std::size_t j = 0;
		while (j < needleLen &&
			std::tolower(static_cast<unsigned char>(value[i + j])) ==
			std::tolower(static_cast<unsigned char>(needle[j]))) {
			++j;
		}
		if (j == needleLen) return true;
	}
	return false;
}

struct HitColor {
	std::uint8_t r;
	std::uint8_t g;
	std::uint8_t b;
};

// 10-color palette replacing skeeto's g_HitMarkerColorTable.
const HitColor kPalette[10] = {
	{255, 255, 255}, {255, 236, 120}, {255, 208, 0},   {255, 160, 0},  {255, 110, 0},
	{255, 70, 70},   {235, 40, 40},   {170, 255, 120}, {120, 210, 255}, {215, 140, 255},
};
int g_hitEventDiagCount = 0;

}

bool HitFeedback::IsWitchEntity(int entIndex) const {
	if (entIndex <= 0 || !I::ClientEntityList) return false;
	auto* entity = I::ClientEntityList->GetClientEntity(entIndex);
	auto* clientClass = entity ? entity->GetClientClass() : nullptr;
	return clientClass && ContainsNoCase(clientClass->m_pNetworkName, "witch");
}

bool HitFeedback::IsSpecialEntity(int entIndex) const {
	if (entIndex <= 0 || !I::ClientEntityList) return false;
	auto* entity = I::ClientEntityList->GetClientEntity(entIndex);
	auto* player = entity ? entity->As<C_TerrorPlayer*>() : nullptr;
	if (!player) return IsWitchEntity(entIndex);
	const int zombieClass = player->m_zombieClass();
	return (zombieClass >= CLASS_SMOKER && zombieClass <= CLASS_TANK) ||
		player->GetTeamNumber() == 3 || IsWitchEntity(entIndex);
}

void HitFeedback::TriggerHitMarker() {
	m_hitMarkerUntil = NowTick() + kHitMarkerDurationMs;
}

// Cone test shared by the hit-confirmation gate. skeeto aims 40 units above the
// victim origin and requires the view vector to point at it, with the threshold
// depending on range (see kFarCone*/kNearCone*). Unlike the old version this does
// not require a C_TerrorPlayer target, so common infected and witches qualify.
bool HitFeedback::IsInViewConeAtRange(int entIndex, float maxDistance, float minDot) const {
	if (entIndex <= 0 || !I::EngineClient || !I::ClientEntityList) return false;
	if (!I::EngineClient->IsConnected() || !I::EngineClient->IsInGame()) return false;
	auto* localEntity = I::ClientEntityList->GetClientEntity(I::EngineClient->GetLocalPlayer());
	auto* local = localEntity ? localEntity->As<C_TerrorPlayer*>() : nullptr;
	auto* target = I::ClientEntityList->GetClientEntity(entIndex);
	if (!local || !target) return false;
	const Vector eye = local->m_vecOrigin() + local->m_vecViewOffset();
	Vector targetPoint = target->GetAbsOrigin();
	targetPoint.z += 40.0f;
	Vector direction = targetPoint - eye;
	const float distance = direction.Normalize();
	if (distance > maxDistance) return false;
	if (distance <= 0.001f) return true;
	Vector viewAngles;
	Vector forward;
	I::EngineClient->GetViewAngles(viewAngles);
	U::Math.AngleVectors(viewAngles, &forward);
	forward.Normalize();
	return forward.Dot(direction) >= minDot;
}

// Port of the gate in SKT_Si_TrackHitFeedback. Order matters and mirrors skeeto:
//   valid ids/entity -> far cone (gun) -> [per-victim lock] -> 70 ms throttle ->
//   near cone -> accept & lock the victim for 400 ms.
// A repeat on the locked victim skips the throttle and both cone tests, which is
// what lets a burst or a shotgun pellet spread register more than once.

void HitFeedback::ArmLocalAttack() {
	m_lastAttackTick = NowTick();
}

void HitFeedback::RecordDamage(int entIndex, int damage, float x, float y, float z, float headZ) {
	if (entIndex <= 0 || damage <= 0) return;
	const std::uint32_t now = NowTick();

	for (int i = 0; i < kMaxSlots; ++i) {
		DamageSlot& slot = m_slots[i];
		if (!slot.used || slot.entIndex != entIndex) continue;
		if (now - slot.tick >= kSlotMergeWindowMs) continue;
		slot.damage += damage;
		if (slot.damage > 9999) slot.damage = 9999;
		slot.worldX = x;
		slot.worldY = y;
		slot.worldZ = z;
		slot.headZ = headZ;
		return;
	}

	int slotIdx = -1;
	for (int i = 0; i < kMaxSlots; ++i) {
		if (!m_slots[i].used) {
			slotIdx = i;
			break;
		}
	}
	if (slotIdx < 0) {
		// skeeto: when all 20 slots are busy, wipe and start from slot 19.
		for (int i = 0; i < kMaxSlots; ++i) m_slots[i] = DamageSlot{};
		slotIdx = kMaxSlots - 1;
	}

	DamageSlot& slot = m_slots[slotIdx];
	slot.used = true;
	slot.entIndex = entIndex;
	slot.damage = damage > 9999 ? 9999 : damage;
	slot.worldX = x;
	slot.worldY = y;
	slot.worldZ = z;
	slot.headZ = headZ;
	slot.tick = now;

	// skeeto scatter angle: deterministic hash, clamped away from straight down.
	const std::uint32_t hash = static_cast<std::uint32_t>(
		0xFFFFFC5Fu * static_cast<std::uint32_t>(entIndex) +
		0xFFFFFE0Du * now +
		0xFFFFFFB5u * static_cast<std::uint32_t>(slotIdx));
	float angle = static_cast<float>(hash & 0x3FFu) * 0.0061359233f;
	float shifted = angle - kHalfPiShift;
	while (shifted > 3.1415927f) shifted -= kTwoPi;
	while (shifted < -3.1415927f) shifted += kTwoPi;
	if (shifted > -0.72f && shifted < 0.72f) {
		angle = (shifted < 0.0f ? -0.72f : 0.72f) + kHalfPiShift;
	}
	slot.angle = angle;

	int colorIdx = static_cast<int>(
		(17u * static_cast<std::uint32_t>(entIndex) +
			31u * static_cast<std::uint32_t>(slotIdx) +
			1664525u * now) % kPaletteSize);
	if (static_cast<std::uint32_t>(colorIdx) == m_lastColorIdx) {
		colorIdx = static_cast<int>((now & 3u) + colorIdx + 1) % kPaletteSize;
	}
	m_lastColorIdx = static_cast<std::uint32_t>(colorIdx);
	slot.colorIdx = static_cast<std::uint8_t>(colorIdx);
}

void HitFeedbackListener::FireGameEvent(IGameEvent* event) {
	F::HitFeedbackMgr.OnGameEvent(event);
}

bool HitFeedback::InitListeners() {
	if (!I::GameEventManager) return false;
	static const char* const kEvents[] = {
		"player_hurt", "infected_hurt", "bullet_impact", "weapon_fire",
		"round_start", "round_end", "mission_lost", "map_transition",
	};
	bool allOk = true;
	bool anyOk = false;
	for (const char* name : kEvents) {
		const bool added = I::GameEventManager->AddListener(&m_listener, name, false);
		allOk = allOk && added;
		anyOk = anyOk || added;
	}
	m_listenersRegistered = anyOk;
	return allOk;
}

void HitFeedback::OnGameEvent(IGameEvent* event) {
	if (!event) return;
	const char* name = event->GetName();
	if (!name) return;
	if (g_hitEventDiagCount < 20) {
		char message[192] = {};
		_snprintf_s(message, sizeof(message), _TRUNCATE,
			"MapStage: HitFeedback event[%d]=%s", g_hitEventDiagCount, name);
		NecolaDiagLog(message);
		++g_hitEventDiagCount;
	}

	if (std::strcmp(name, "round_start") == 0 || std::strcmp(name, "round_end") == 0 ||
		std::strcmp(name, "mission_lost") == 0 ||
		std::strcmp(name, "map_transition") == 0) {
		Reset();
		return;
	}
	if (std::strcmp(name, "bullet_impact") == 0) {
		if (IsLocalAttacker(event, "userid")) {
			m_lastImpactTick = NowTick();
			m_lastAttackTick = m_lastImpactTick;
		}
		return;
	}
	if (std::strcmp(name, "weapon_fire") == 0) {
		if (IsLocalAttacker(event, "userid")) m_lastAttackTick = NowTick();
		return;
	}
	// ---- ingress only ----
	// skeeto's producer (SKT_Stats_QueueDamageEvent) decides nothing either: it
	// stamps a record and lets the per-frame consumer validate it. Keeping the
	// decision here is what made this module drift and regress.
	PendingHit pending;
	pending.tick = NowTick();
	pending.mapGen = m_mapGeneration;
	pending.gun = true;

	if (std::strcmp(name, "player_hurt") == 0) {
		if (!IsLocalAttacker(event, "attacker")) return;
		if (!I::EngineClient || !I::ClientEntityList) return;
		const int victimUserId = event->GetInt("userid", 0);
		const int victimEnt = I::EngineClient->GetPlayerForUserID(victimUserId);
		if (victimEnt <= 0) return;
		auto* entity = I::ClientEntityList->GetClientEntity(victimEnt);
		auto* player = entity ? entity->As<C_TerrorPlayer*>() : nullptr;
		if (!player) return;
		const Vector origin = player->m_vecOrigin();
		const Vector view = player->m_vecViewOffset();
		pending.entIndex = victimEnt;
		pending.userId = victimUserId;
		pending.damage = event->GetInt("dmg_health", 0);
		pending.x = origin.x;
		pending.y = origin.y;
		pending.z = origin.z;
		pending.headZ = (view.z > 20.0f && view.z < 120.0f) ? view.z + 2.0f : 50.0f;
		EnqueueHit(pending);
		return;
	}
	if (std::strcmp(name, "infected_hurt") == 0) {
		if (!IsLocalAttacker(event, "attacker")) return;
		if (!I::ClientEntityList) return;
		const int entIndex = event->GetInt("entityid", 0);
		if (entIndex <= 0) return;
		auto* entity = I::ClientEntityList->GetClientEntity(entIndex);
		if (!entity) return;
		const Vector origin = entity->GetAbsOrigin();
		pending.entIndex = entIndex;
		pending.userId = 0;
		pending.damage = event->GetInt("amount", 0);
		pending.x = origin.x;
		pending.y = origin.y;
		pending.z = origin.z;
		pending.headZ = 50.0f;
		EnqueueHit(pending);
		return;
	}
}

// Producer side of the rewrite: stamp and queue, decide nothing.
void HitFeedback::EnqueueHit(const PendingHit& hit) {
	if (hit.damage <= 0 || hit.entIndex <= 0) return;
	if (!G::Vars.hitFeedbackEnabled) return;
	if (m_pendingCount >= kMaxPendingHits) {
		for (int i = 1; i < m_pendingCount; ++i) m_pending[i - 1] = m_pending[i];
		--m_pendingCount;
	}
	m_pending[m_pendingCount++] = hit;
}

// Gate from SKT_Si_TrackHitFeedback, same order: far cone (gun only, 5000/0.93)
// -> 70 ms throttle -> near cone (non-gun, 110/0.90) -> lock the victim 400 ms.
// Repeats on the locked victim skip the throttle and both cone tests.
bool HitFeedback::GateHit(const PendingHit& hit) {
	if (hit.damage <= 0 || hit.entIndex <= 0) return false;
	if (!I::ClientEntityList) return false;
	if (!I::ClientEntityList->GetClientEntity(hit.entIndex)) return false;
	if (hit.userId < 0 || hit.userId > 64) return false;
	if (hit.gun && !IsInViewConeAtRange(hit.entIndex, kFarConeDistance, kFarConeDot)) return false;

	const std::uint32_t now = NowTick();
	const bool locked = m_lockEntity == hit.entIndex && m_lockUserId == hit.userId &&
		m_lockUntil != 0 && static_cast<std::int32_t>(m_lockUntil - now) >= 0;
	if (!locked) {
		if (now - m_lastFeedbackTick < kHitFeedbackThrottleMs) return false;
		if (!hit.gun && !IsInViewConeAtRange(hit.entIndex, kNearConeDistance, kNearConeDot)) return false;
		m_lockUserId = hit.userId;
		m_lockEntity = hit.entIndex;
		m_lockUntil = now + kVictimLockMs;
	}
	m_lastFeedbackTick = now;
	return true;
}

// Consumer side: validate then dispatch, mirroring SKT_Stats_ConsumePendingDamage.
void HitFeedback::DrainPendingHits() {
	if (m_pendingCount <= 0) return;
	const std::uint32_t now = NowTick();
	const int count = m_pendingCount < kMaxDrainPerFrame ? m_pendingCount : kMaxDrainPerFrame;
	for (int i = 0; i < count; ++i) {
		const PendingHit hit = m_pending[i];
		if (hit.mapGen != m_mapGeneration) continue;
		if (now - hit.tick > kPendingLifetimeMs) continue;
		if (hit.userId > 0) {
			const int resolved = I::EngineClient ? I::EngineClient->GetPlayerForUserID(hit.userId) : 0;
			if (resolved != hit.entIndex) continue;
		}
		if (!GateHit(hit)) continue;
		if (G::Vars.hitFeedbackNumbers) {
			RecordDamage(hit.entIndex, hit.damage, hit.x, hit.y, hit.z, hit.headZ);
		}
		if (G::Vars.hitFeedbackHitMarker) TriggerHitMarker();
		// Themed hit overlay stays in KillFeedback and keeps its own HitMode gate.
		F::KillFeedbackMgr.OnSpecialHealthDrop(Vector(hit.x, hit.y, hit.z));
	}
	if (count >= m_pendingCount) {
		m_pendingCount = 0;
		return;
	}
	for (int i = count; i < m_pendingCount; ++i) m_pending[i - count] = m_pending[i];
	m_pendingCount -= count;
}

void HitFeedback::Pump() {
	DrainPendingHits();
}

void HitFeedback::Draw() {
	if (!G::Vars.hitFeedbackEnabled || !I::GlobalVars || !I::MatSystemSurface ||
		!I::EngineClient) return;
	if (!G::Vars.hitFeedbackNumbers && !G::Vars.hitFeedbackHitMarker) return;
	if (!I::EngineClient->IsConnected() || !I::EngineClient->IsInGame()) return;

	if (m_damageFont == 0) {
		m_damageFont = I::MatSystemSurface->CreateFont();
		I::MatSystemSurface->SetFontGlyphSet(m_damageFont, "Microsoft YaHei", 24, 700,
			0, 0, FONTFLAG_OUTLINE, 0, 0);
		m_damageFontSize = 24;
	}

	int screenW = 0;
	int screenH = 0;
	I::MatSystemSurface->GetScreenSize(screenW, screenH);
	if (screenW < 8 || screenH < 8) return;

	const std::uint32_t now = NowTick();

	if (G::Vars.hitFeedbackHitMarker && m_hitMarkerUntil > now) {
		const float remain = static_cast<float>(m_hitMarkerUntil - now);
		const int alpha = static_cast<int>(255.0f * (remain / kHitMarkerDurationMs));
		if (alpha > 0) {
			const int cx = screenW / 2;
			const int cy = screenH / 2;
			constexpr int gap = 7;
			constexpr int len = 9;
			I::MatSystemSurface->DrawSetColor(255, 90, 90, alpha);
			I::MatSystemSurface->DrawLine(cx - gap, cy - gap, cx - gap - len, cy - gap - len);
			I::MatSystemSurface->DrawLine(cx + gap, cy - gap, cx + gap + len, cy - gap - len);
			I::MatSystemSurface->DrawLine(cx - gap, cy + gap, cx - gap - len, cy + gap + len);
			I::MatSystemSurface->DrawLine(cx + gap, cy + gap, cx + gap + len, cy + gap + len);
		}
	}

	if (!G::Vars.hitFeedbackNumbers || m_damageFont == 0) return;

	const VMatrix& matrix = I::EngineClient->WorldToScreenMatrix();
	I::MatSystemSurface->DrawSetTextFont(m_damageFont);

	for (int i = 0; i < kMaxSlots; ++i) {
		const DamageSlot& slot = m_slots[i];
		if (!slot.used || slot.damage <= 0) continue;
		const std::uint32_t age = now - slot.tick;
		if (age >= static_cast<std::uint32_t>(kSlotLifetimeMs)) {
			m_slots[i] = DamageSlot{};
			continue;
		}
		const float ageFrac = static_cast<float>(age) / kSlotLifetimeMs;
		int alpha = 255;
		if (ageFrac > 0.55f) {
			alpha = static_cast<int>((1.0f - (ageFrac - 0.55f) / 0.45f) * 255.0f);
			if (alpha < 18) {
				m_slots[i] = DamageSlot{};
				continue;
			}
		}

		auto project = [&](float x, float y, float z, float* outX, float* outY) {
			const float w = x * matrix[3][0] + y * matrix[3][1] + z * matrix[3][2] + matrix[3][3];
			if (w < 0.001f) return false;
			*outX = (x * matrix[0][0] + y * matrix[0][1] + z * matrix[0][2] + matrix[0][3]) /
				w * 0.5f * screenW + screenW * 0.5f + 0.5f;
			*outY = screenH * 0.5f -
				(x * matrix[1][0] + y * matrix[1][1] + z * matrix[1][2] + matrix[1][3]) /
				w * 0.5f * screenH + 0.5f;
			return *outX > -80.0f && *outX < screenW + 80.0f &&
				*outY > -80.0f && *outY < screenH + 80.0f;
		};

		float baseX = 0.0f;
		float baseY = 0.0f;
		float headX = 0.0f;
		float headY = 0.0f;
		if (!project(slot.worldX, slot.worldY, slot.worldZ, &baseX, &baseY)) continue;
		if (!project(slot.worldX, slot.worldY, slot.worldZ + slot.headZ, &headX, &headY)) continue;

		wchar_t text[16] = {};
		const int textLen = swprintf_s(text, L"%d", slot.damage);
		if (textLen <= 0) continue;
		const int textWidth = textLen * (3 * (m_damageFontSize > 0 ? m_damageFontSize : 20) / 5);

		// skeeto layout: scatter around the head projection, never straight down.
		const float distAbs = std::fabs(headX - baseX);
		const float distMax = distAbs > 28.0f ? distAbs : 28.0f;
		const float radius = (22.0f > distMax * 0.42f ? 22.0f : distMax * 0.42f) * 0.5f + 20.0f;
		const int drawX = static_cast<int>(std::cos(slot.angle) * radius +
			(baseX + headX) * 0.5f) - textWidth / 2;
		const int drawY = static_cast<int>(std::sin(slot.angle) *
			(distMax * 0.5f + 14.0f) + (baseY + headY) * 0.5f -
			ageFrac * 10.0f + 0.5f);

		const HitColor& color = kPalette[slot.colorIdx < kPaletteSize ? slot.colorIdx : 0];
		I::MatSystemSurface->DrawSetTextColor(Color{0, 0, 0, static_cast<std::uint8_t>(alpha)});
		I::MatSystemSurface->DrawSetTextPos(drawX + 1, drawY + 1);
		I::MatSystemSurface->DrawPrintText(text, textLen);
		I::MatSystemSurface->DrawSetTextColor(
			Color{color.r, color.g, color.b, static_cast<std::uint8_t>(alpha)});
		I::MatSystemSurface->DrawSetTextPos(drawX, drawY);
		I::MatSystemSurface->DrawPrintText(text, textLen);
	}
}

void HitFeedback::Reset() {
	for (int i = 0; i < kMaxSlots; ++i) m_slots[i] = DamageSlot{};
	m_hitMarkerUntil = 0;
	m_lastAttackTick = 0;
	m_lastImpactTick = 0;
	// A new round must not inherit the previous generation's queue or victim lock.
	++m_mapGeneration;
	m_pendingCount = 0;
	m_lastFeedbackTick = 0;
	m_lockUserId = 0;
	m_lockEntity = 0;
	m_lockUntil = 0;
	// Drop the hit-confirmation lock/throttle: a new round must not inherit the
	// previous victim's 400 ms lock.
	m_lastFeedbackTick = 0;
	m_lockUserId = 0;
	m_lockEntity = 0;
	m_lockUntil = 0;
}

void HitFeedback::Shutdown() {
	if (m_listenersRegistered && I::GameEventManager) {
		I::GameEventManager->RemoveListener(&m_listener);
		m_listenersRegistered = false;
	}
	Reset();
}

void HitFeedback::LoadConfig(const nlohmann::json& doc) {
	const auto section = doc.find("HitFeedback");
	if (section == doc.end() || !section->is_object()) return;
	auto readBool = [section](const char* key, bool fallback) {
		const auto value = section->find(key);
		return value != section->end() && value->is_boolean() ? value->get<bool>() : fallback;
	};
	G::Vars.hitFeedbackEnabled = readBool("Enabled", false);
	G::Vars.hitFeedbackNumbers = readBool("DamageNumbers", true);
	G::Vars.hitFeedbackHitMarker = readBool("HitMarker", true);
	G::Vars.hitFeedbackCommon = readBool("CommonHits", false);
}

void HitFeedback::SaveConfig(nlohmann::json& doc) const {
	auto& section = NecolaConfig::EnsureSectionObject(doc, "HitFeedback");
	section["Enabled"] = G::Vars.hitFeedbackEnabled;
	section["DamageNumbers"] = G::Vars.hitFeedbackNumbers;
	section["HitMarker"] = G::Vars.hitFeedbackHitMarker;
	section["CommonHits"] = G::Vars.hitFeedbackCommon;
}

void HitFeedback::SaveConfig() const {
	nlohmann::json doc = NecolaConfig::LoadConfig();
	SaveConfig(doc);
	NecolaConfig::SaveConfig(doc);
}
