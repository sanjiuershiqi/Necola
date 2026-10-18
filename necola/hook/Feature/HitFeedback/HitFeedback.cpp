#include "HitFeedback.h"

#include "../../../sdk/utils/FeatureConfigManager.h"
#include "../../Vars.h"

#include <cmath>
#include <cstdio>
#include <cwchar>

namespace {

constexpr float kTwoPi = 6.2831855f;
constexpr float kHalfPiShift = 4.712389f;

std::uint32_t NowTick() {
	return static_cast<std::uint32_t>(GetTickCount());
}

struct HitColor {
	std::uint8_t r;
	std::uint8_t g;
	std::uint8_t b;
};

// 10-colour palette (replaces skeeto's g_HitMarkerColorTable).
const HitColor kPalette[10] = {
	{255, 255, 255}, {255, 236, 120}, {255, 208, 0},   {255, 160, 0},  {255, 110, 0},
	{255, 70, 70},   {235, 40, 40},   {170, 255, 120}, {120, 210, 255}, {215, 140, 255},
};

} // namespace

void HitFeedback::AddNumber(int entIndex, int damage, const Vector& origin, float headZ) {
	if (entIndex <= 0 || damage <= 0) return;
	const std::uint32_t now = NowTick();

	for (int i = 0; i < kMaxSlots; ++i) {
		DamageSlot& slot = m_slots[i];
		if (!slot.used || slot.entIndex != entIndex) continue;
		if (now - slot.tick >= kSlotMergeWindowMs) continue;
		slot.damage += damage;
		if (slot.damage > 9999) slot.damage = 9999;
		slot.worldX = origin.x;
		slot.worldY = origin.y;
		slot.worldZ = origin.z;
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
		for (int i = 0; i < kMaxSlots; ++i) m_slots[i] = DamageSlot{};
		slotIdx = kMaxSlots - 1;
	}

	DamageSlot& slot = m_slots[slotIdx];
	slot.used = true;
	slot.entIndex = entIndex;
	slot.damage = damage > 9999 ? 9999 : damage;
	slot.worldX = origin.x;
	slot.worldY = origin.y;
	slot.worldZ = origin.z;
	slot.headZ = headZ;
	slot.tick = now;

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

void HitFeedback::FlashMarker() {
	m_hitMarkerUntil = NowTick() + kHitMarkerDurationMs;
}

void HitFeedback::Draw() {
	if (!G::Vars.hitFeedbackEnabled || !I::MatSystemSurface || !I::EngineClient) return;
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
		const int fontHeight = m_damageFontSize > 0 ? m_damageFontSize : 20;
		const int textWidth = textLen * (3 * fontHeight / 5);

		const float distAbs = std::fabs(headX - baseX);
		const float distMax = distAbs > 28.0f ? distAbs : 28.0f;
		const float radius = (22.0f > distMax * 0.42f ? 22.0f : distMax * 0.42f) * 0.5f + 20.0f;
		const float drift = ageFrac * 26.0f;
		const int x = static_cast<int>(headX + std::cos(slot.angle) * radius - textWidth / 2);
		const int y = static_cast<int>(headY + std::sin(slot.angle) * radius - fontHeight / 2 - drift);

		I::MatSystemSurface->DrawSetTextPos(x + 1, y + 1);
		I::MatSystemSurface->DrawSetTextColor(0, 0, 0, alpha);
		I::MatSystemSurface->DrawPrintText(text, textLen);

		const HitColor& color = kPalette[slot.colorIdx % kPaletteSize];
		I::MatSystemSurface->DrawSetTextPos(x, y);
		I::MatSystemSurface->DrawSetTextColor(color.r, color.g, color.b, alpha);
		I::MatSystemSurface->DrawPrintText(text, textLen);
	}
}

void HitFeedback::Reset() {
	for (int i = 0; i < kMaxSlots; ++i) m_slots[i] = DamageSlot{};
	m_hitMarkerUntil = 0;
	m_lastColorIdx = 0;
}

void HitFeedback::Shutdown() {
	Reset();
	m_damageFont = 0;
	m_damageFontSize = 0;
}

void HitFeedback::LoadConfig(const nlohmann::json& doc) {
	const auto section = doc.find("HitFeedback");
	if (section == doc.end() || !section->is_object()) return;
	auto readBool = [&section](const char* key, bool fallback) {
		const auto value = section->find(key);
		return value != section->end() && value->is_boolean() ? value->get<bool>() : fallback;
	};
	G::Vars.hitFeedbackEnabled = readBool("Enabled", G::Vars.hitFeedbackEnabled);
	G::Vars.hitFeedbackNumbers = readBool("Numbers", G::Vars.hitFeedbackNumbers);
	G::Vars.hitFeedbackHitMarker = readBool("HitMarker", G::Vars.hitFeedbackHitMarker);
	G::Vars.hitFeedbackCommon = readBool("CommonHits", G::Vars.hitFeedbackCommon);
}

void HitFeedback::SaveConfig(nlohmann::json& doc) const {
	auto& section = NecolaConfig::EnsureSectionObject(doc, "HitFeedback");
	section["Enabled"] = G::Vars.hitFeedbackEnabled;
	section["Numbers"] = G::Vars.hitFeedbackNumbers;
	section["HitMarker"] = G::Vars.hitFeedbackHitMarker;
	section["CommonHits"] = G::Vars.hitFeedbackCommon;
}

void HitFeedback::SaveConfig() const {
	nlohmann::json doc = NecolaConfig::LoadConfig();
	SaveConfig(doc);
	NecolaConfig::SaveConfig(doc);
}
