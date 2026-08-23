#include "HitFeedback.h"

#include "../../../sdk/utils/FeatureConfigManager.h"
#include "../../Vars.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>

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
		"player_hurt", "infected_hurt",
		"round_start", "mission_lost", "map_transition",
	};
	bool allOk = true;
	for (const char* name : kEvents) {
		const bool added = I::GameEventManager->AddListener(&m_listener, name, false);
		allOk = allOk && added;
	}
	m_listenersRegistered = true;
	return allOk;
}

void HitFeedback::OnGameEvent(IGameEvent* event) {
	if (!event || !G::Vars.hitFeedbackEnabled) return;
	const char* name = event->GetName();
	if (!name) return;

	if (std::strcmp(name, "round_start") == 0 || std::strcmp(name, "mission_lost") == 0 ||
		std::strcmp(name, "map_transition") == 0) {
		Reset();
		return;
	}

	if (!I::EngineClient || !I::ClientEntityList) return;

	if (std::strcmp(name, "player_hurt") == 0) {
		if (!IsLocalAttacker(event, "attacker")) return;
		const int victimEnt = I::EngineClient->GetPlayerForUserID(event->GetInt("userid", 0));
		if (victimEnt <= 0) return;
		auto* entity = I::ClientEntityList->GetClientEntity(victimEnt);
		auto* player = entity ? entity->As<C_TerrorPlayer*>() : nullptr;
		if (!player) return;
		const Vector& origin = player->m_vecOrigin();
		float headZ = 50.0f;
		const Vector view = player->m_vecViewOffset();
		if (view.z > 20.0f && view.z < 120.0f) headZ = view.z + 2.0f;
		const bool special = IsSpecialEntity(victimEnt);
		if (special || G::Vars.hitFeedbackCommon) {
			RecordDamage(victimEnt, event->GetInt("dmg_health", 0),
				origin.x, origin.y, origin.z, headZ);
		}
		if (special && G::Vars.hitFeedbackHitMarker) TriggerHitMarker();
		return;
	}

	if (std::strcmp(name, "infected_hurt") == 0) {
		if (!IsLocalAttacker(event, "attacker")) return;
		const int entIndex = event->GetInt("entityid", 0);
		if (!IsWitchEntity(entIndex)) return;
		auto* entity = I::ClientEntityList->GetClientEntity(entIndex);
		if (!entity) return;
		const Vector& origin = entity->GetAbsOrigin();
		RecordDamage(entIndex, event->GetInt("amount", 0),
			origin.x, origin.y, origin.z, 50.0f);
		if (G::Vars.hitFeedbackHitMarker) TriggerHitMarker();
	}
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
