#include "HitFeedback.h"

#include "../../../sdk/utils/FeatureConfigManager.h"
#include "../../../sdk/l4d2/includes/dt_recv.h"
#include "../../../sdk/l4d2/interfaces/BaseClientDLL.h"
#include "../../Vars.h"
#include "../KillFeedback/KillFeedback.h"

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

std::vector<std::pair<RecvProp*, RecvVarProxyFn>> g_healthProxies;

RecvProp* FindRecvPropRecursive(RecvTable* table, const char* name, int depth = 0) {
	if (!table || !name || depth > 16) return nullptr;
	for (int i = 0; i < table->m_nProps; ++i) {
		RecvProp* prop = &table->m_pProps[i];
		if (prop->m_pVarName && std::strcmp(prop->m_pVarName, name) == 0) return prop;
		if (RecvProp* nested = FindRecvPropRecursive(prop->GetDataTable(), name, depth + 1)) return nested;
	}
	return nullptr;
}

void HealthProxy(const CRecvProxyData* data, void* structure, void* output) {
	int oldHealth = output ? *static_cast<int*>(output) : 0;
	RecvVarProxyFn original = nullptr;
	for (const auto& [prop, proxy] : g_healthProxies) {
		if (data && data->m_pRecvProp == prop) {
			original = proxy;
			break;
		}
	}
	if (!original && !g_healthProxies.empty()) original = g_healthProxies.front().second;
	if (original) original(data, structure, output);
	else if (output && data) *static_cast<int*>(output) = data->m_Value.m_Int;
	if (data) F::HitFeedbackMgr.OnHealthChanged(data->m_ObjectID, oldHealth, data->m_Value.m_Int);
}
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

bool HitFeedback::InstallHealthProxy() {
	if (m_healthProxyInstalled) return true;
	if (!I::BaseClient) return false;
	for (ClientClass* clientClass = I::BaseClient->GetAllClasses(); clientClass;
		clientClass = clientClass->m_pNext) {
		if (!clientClass->m_pNetworkName ||
			(std::strcmp(clientClass->m_pNetworkName, "CBasePlayer") != 0 &&
			 std::strcmp(clientClass->m_pNetworkName, "CTerrorPlayer") != 0)) continue;
		RecvProp* prop = FindRecvPropRecursive(clientClass->m_pRecvTable, "m_iHealth");
		if (!prop || !prop->GetProxyFn() || prop->GetProxyFn() == HealthProxy) continue;
		bool duplicate = false;
		for (const auto& installed : g_healthProxies) {
			if (installed.first == prop) {
				duplicate = true;
				break;
			}
		}
		if (duplicate) continue;
		g_healthProxies.emplace_back(prop, prop->GetProxyFn());
		prop->SetProxyFn(HealthProxy);
	}
	m_healthProxyInstalled = !g_healthProxies.empty();
	return m_healthProxyInstalled;
}

void HitFeedback::UnhookHealthProxy() {
	for (const auto& [prop, original] : g_healthProxies) {
		if (prop && prop->GetProxyFn() == HealthProxy) prop->SetProxyFn(original);
	}
	g_healthProxies.clear();
	m_healthProxyInstalled = false;
}

bool HitFeedback::IsLocalHitCorrelated(int entIndex) const {
	if (entIndex <= 0 || !I::EngineClient || !I::ClientEntityList) return false;
	if (!I::EngineClient->IsConnected() || !I::EngineClient->IsInGame()) return false;
	const std::uint32_t now = NowTick();
	const bool recentImpact = now - m_lastImpactTick <= 180;
	const bool recentAttack = now - m_lastAttackTick <= 400;
	if (!recentImpact && !recentAttack) return false;
	auto* localEntity = I::ClientEntityList->GetClientEntity(I::EngineClient->GetLocalPlayer());
	auto* local = localEntity ? localEntity->As<C_TerrorPlayer*>() : nullptr;
	auto* targetEntity = I::ClientEntityList->GetClientEntity(entIndex);
	auto* target = targetEntity ? targetEntity->As<C_TerrorPlayer*>() : nullptr;
	if (!local || !target || target->GetTeamNumber() != 3) return false;
	const Vector eye = local->m_vecOrigin() + local->m_vecViewOffset();
	Vector targetPoint = target->m_vecOrigin();
	targetPoint.z += 40.0f;
	Vector direction = targetPoint - eye;
	const float distance = direction.Normalize();
	if (distance > (recentImpact ? 5000.0f : 110.0f)) return false;
	Vector viewAngles;
	Vector forward;
	I::EngineClient->GetViewAngles(viewAngles);
	U::Math.AngleVectors(viewAngles, &forward);
	forward.Normalize();
	return forward.Dot(direction) >= (recentImpact ? 0.93f : 0.90f);
}

void HitFeedback::ProcessSpecialHit(int entIndex, int damage, const Vector& origin, float headZ,
	bool fromProxy) {
	if (entIndex <= 0 || damage <= 0) return;
	const std::uint32_t now = NowTick();
	if (entIndex == m_lastSpecialHitEntity && damage == m_lastSpecialHitDamage &&
		fromProxy != m_lastSpecialHitFromProxy && now - m_lastSpecialHitTick <= 60) return;
	m_lastSpecialHitEntity = entIndex;
	m_lastSpecialHitDamage = damage;
	m_lastSpecialHitFromProxy = fromProxy;
	m_lastSpecialHitTick = now;
	if (G::Vars.hitFeedbackEnabled) {
		if (G::Vars.hitFeedbackNumbers) {
			RecordDamage(entIndex, damage, origin.x, origin.y, origin.z, headZ);
		}
		if (G::Vars.hitFeedbackHitMarker) TriggerHitMarker();
	}
	F::KillFeedbackMgr.OnSpecialHealthDrop(origin);
}

void HitFeedback::OnHealthChanged(int entIndex, int oldHealth, int newHealth) {
	if (oldHealth <= 0 || newHealth >= oldHealth || oldHealth > 100000 || newHealth < 0 ||
		newHealth > 100000) return;
	if (!G::Vars.hitFeedbackEnabled && G::Vars.killFeedbackHitMode == 0) return;
	if (entIndex <= 0 || entIndex > 64) return;
	if (m_pendingHealthChangeCount >= kMaxPendingHealthChanges) return;
	m_pendingHealthChanges[m_pendingHealthChangeCount++] = {entIndex, oldHealth, newHealth};
}

void HitFeedback::ProcessHealthChange(int entIndex, int oldHealth, int newHealth) {
	if (!IsLocalHitCorrelated(entIndex) || !I::ClientEntityList) return;
	auto* entity = I::ClientEntityList->GetClientEntity(entIndex);
	auto* player = entity ? entity->As<C_TerrorPlayer*>() : nullptr;
	if (!player) return;
	const Vector origin = player->m_vecOrigin();
	float headZ = player->m_vecViewOffset().z;
	if (headZ < 20.0f || headZ > 120.0f) headZ = 50.0f;
	ProcessSpecialHit(entIndex, oldHealth - newHealth, origin, headZ + 2.0f, true);
}

void HitFeedback::PumpHealthChanges() {
	const int count = m_pendingHealthChangeCount;
	m_pendingHealthChangeCount = 0;
	for (int i = 0; i < count; ++i) {
		const PendingHealthChange change = m_pendingHealthChanges[i];
		ProcessHealthChange(change.entIndex, change.oldHealth, change.newHealth);
	}
}

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
	if (!G::Vars.hitFeedbackEnabled && G::Vars.killFeedbackHitMode == 0) return;

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
		if (special) ProcessSpecialHit(victimEnt, event->GetInt("dmg_health", 0), origin, headZ, false);
		return;
	}

	if (std::strcmp(name, "infected_hurt") == 0) {
		if (!IsLocalAttacker(event, "attacker")) return;
		const int entIndex = event->GetInt("entityid", 0);
		auto* entity = I::ClientEntityList->GetClientEntity(entIndex);
		if (!entity) return;
		const Vector& origin = entity->GetAbsOrigin();
		const bool witch = IsWitchEntity(entIndex);
		const int damage = event->GetInt("amount", 0);
		if (witch) {
			if (G::Vars.hitFeedbackEnabled && G::Vars.hitFeedbackNumbers) {
				RecordDamage(entIndex, damage, origin.x, origin.y, origin.z, 50.0f);
			}
			if (G::Vars.hitFeedbackEnabled && G::Vars.hitFeedbackHitMarker && damage > 0) {
				TriggerHitMarker();
			}
		} else if (G::Vars.hitFeedbackEnabled && G::Vars.hitFeedbackCommon &&
			G::Vars.hitFeedbackNumbers) {
			RecordDamage(entIndex, damage, origin.x, origin.y, origin.z, 50.0f);
		}
	}
}

void HitFeedback::Pump() {
	PumpHealthChanges();
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
	m_lastSpecialHitTick = 0;
	m_lastSpecialHitEntity = 0;
	m_lastSpecialHitDamage = 0;
	m_lastSpecialHitFromProxy = false;
	m_pendingHealthChangeCount = 0;
}

void HitFeedback::Shutdown() {
	UnhookHealthProxy();
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
