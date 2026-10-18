#include "DamagePipeline.h"

#include "../../Vars.h"
#include "../../../diag.h"
#include "../HitFeedback/HitFeedback.h"
#include "../KillFeedback/KillFeedback.h"

#include <cstdint>
#include <cstdio>

namespace {

// ---------------------------------------------------------------- constants --
// Queue sizing and lifetimes are skeeto's: a 64-entry producer queue, at most 32
// records consumed per frame, and a record older than 400 ms is discarded.
constexpr int kQueueSize = 64;
constexpr int kMaxPerFrame = 32;
constexpr std::uint32_t kRecordLifetimeMs = 400;

// Gate constants from SKT_Si_TrackHitFeedback.
constexpr std::uint32_t kThrottleMs = 70;
constexpr std::uint32_t kVictimLockMs = 400;
constexpr float kFarConeDistance = 5000.0f;
constexpr float kFarConeDot = 0.93f;
constexpr float kNearConeDistance = 110.0f;
constexpr float kNearConeDot = 0.90f;

std::uint32_t Now() {
	return static_cast<std::uint32_t>(GetTickCount());
}

struct Record {
	std::uint32_t tick = 0;
	std::uint32_t mapGeneration = 0;
	int entIndex = 0;
	int userId = 0; // 0 for entities without a user id (commons, witch)
	int damage = 0;
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	float headZ = 50.0f;
	bool gun = true;
};

// ---------------------------------------------------------------- queue state --
Record g_queue[kQueueSize];
int g_queueCount = 0;
std::uint32_t g_mapGeneration = 1;

// ---------------------------------------------------------------- gate state --
std::uint32_t g_lastAcceptedTick = 0;
int g_lockUserId = 0;
int g_lockEntity = 0;
std::uint32_t g_lockUntil = 0;

// Diagnostics: a small budget so a normal session writes a handful of lines.
int g_logBudget = 24;

void Log(const char* format, ...) {
	if (g_logBudget <= 0) return;
	--g_logBudget;
	char line[256] = {};
	va_list args;
	va_start(args, format);
	_vsnprintf_s(line, sizeof(line), _TRUNCATE, format, args);
	va_end(args);
	NecolaDiagLog(line);
}

bool IsLocalAttacker(IGameEvent* event, const char* field) {
	if (!event || !I::EngineClient) return false;
	const int userId = event->GetInt(field, 0);
	if (userId <= 0) return false;
	const int resolved = I::EngineClient->GetPlayerForUserID(userId);
	return resolved > 0 && resolved == I::EngineClient->GetLocalPlayer();
}

// skeeto aims 40 units above the victim origin and compares the view vector
// against it. Works for any entity, so commons and witches qualify too.
bool InViewCone(int entIndex, float maxDistance, float minDot) {
	if (entIndex <= 0 || !I::EngineClient || !I::ClientEntityList) return false;
	if (!I::EngineClient->IsConnected() || !I::EngineClient->IsInGame()) return false;
	auto* localEntity = I::ClientEntityList->GetClientEntity(I::EngineClient->GetLocalPlayer());
	auto* local = localEntity ? localEntity->As<C_TerrorPlayer*>() : nullptr;
	auto* target = I::ClientEntityList->GetClientEntity(entIndex);
	if (!local || !target) return false;
	const Vector eye = local->m_vecOrigin() + local->m_vecViewOffset();
	Vector point = target->GetAbsOrigin();
	point.z += 40.0f;
	Vector direction = point - eye;
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

// ------------------------------------------------------------- stage 1: queue --
void Enqueue(const Record& record) {
	if (record.damage <= 0 || record.entIndex <= 0) return;
	if (g_queueCount >= kQueueSize) {
		for (int i = 1; i < g_queueCount; ++i) g_queue[i - 1] = g_queue[i];
		--g_queueCount;
	}
	g_queue[g_queueCount++] = record;
}

// -------------------------------------------------------------- stage 3: gate --
bool Gate(const Record& record) {
	if (record.damage <= 0) return false;
	if (!I::ClientEntityList || !I::ClientEntityList->GetClientEntity(record.entIndex)) return false;
	if (record.userId < 0 || record.userId > 64) return false;
	if (record.gun && !InViewCone(record.entIndex, kFarConeDistance, kFarConeDot)) return false;

	const std::uint32_t now = Now();
	const bool locked = g_lockEntity == record.entIndex && g_lockUserId == record.userId &&
		g_lockUntil != 0 && static_cast<std::int32_t>(g_lockUntil - now) >= 0;
	if (!locked) {
		if (now - g_lastAcceptedTick < kThrottleMs) return false;
		// Near cone applies to non-gun hits only, exactly like skeeto's
		// "a7 == 0 && !InViewCone(110, 0.90)".
		if (!record.gun && !InViewCone(record.entIndex, kNearConeDistance, kNearConeDot)) return false;
		g_lockUserId = record.userId;
		g_lockEntity = record.entIndex;
		g_lockUntil = now + kVictimLockMs;
	}
	g_lastAcceptedTick = now;
	return true;
}

// --------------------------------------------------------- stage 2: consumer --
void Drain() {
	if (g_queueCount <= 0) return;
	const std::uint32_t now = Now();
	const int count = g_queueCount < kMaxPerFrame ? g_queueCount : kMaxPerFrame;

	for (int i = 0; i < count; ++i) {
		const Record record = g_queue[i];
		if (record.mapGeneration != g_mapGeneration) continue;
		if (now - record.tick > kRecordLifetimeMs) continue;
		if (record.userId > 0) {
			const int resolved = I::EngineClient ? I::EngineClient->GetPlayerForUserID(record.userId) : 0;
			if (resolved != record.entIndex) continue;
		}
		if (!Gate(record)) continue;

		const Vector origin(record.x, record.y, record.z);
		F::HitFeedbackMgr.AddNumber(record.entIndex, record.damage, origin, record.headZ);
		F::HitFeedbackMgr.FlashMarker();
		// Themed overlay keeps its own HitMode gate inside KillFeedback.
		F::KillFeedbackMgr.OnSpecialHealthDrop(origin);
		Log("hit accepted ent=%d user=%d dmg=%d gun=%d", record.entIndex, record.userId,
			record.damage, record.gun ? 1 : 0);
	}

	if (count >= g_queueCount) {
		g_queueCount = 0;
		return;
	}
	for (int i = count; i < g_queueCount; ++i) g_queue[i - count] = g_queue[i];
	g_queueCount -= count;
}

// ------------------------------------------------------------------- listener --
class PipelineListener final : public IGameEventListener2 {
public:
	void FireGameEvent(IGameEvent* event) override;
	int GetEventDebugID() override { return 43; }
};

PipelineListener g_listener;
bool g_registered = false;

void PipelineListener::FireGameEvent(IGameEvent* event) {
	if (!event) return;
	const char* name = event->GetName();
	if (!name) return;

	// Round/map lifecycle: everything queued belongs to the previous generation.
	if (std::strcmp(name, "round_start") == 0 || std::strcmp(name, "round_end") == 0 ||
		std::strcmp(name, "mission_lost") == 0 || std::strcmp(name, "map_transition") == 0) {
		DamagePipeline::Reset();
		return;
	}

	if (!G::Vars.hitFeedbackEnabled) return;
	if (!I::EngineClient || !I::ClientEntityList) return;

	Record record;
	record.tick = Now();
	record.mapGeneration = g_mapGeneration;

	if (std::strcmp(name, "player_hurt") == 0) {
		if (!IsLocalAttacker(event, "attacker")) return;
		const int victimUserId = event->GetInt("userid", 0);
		const int victimEnt = I::EngineClient->GetPlayerForUserID(victimUserId);
		if (victimEnt <= 0) return;
		auto* entity = I::ClientEntityList->GetClientEntity(victimEnt);
		auto* player = entity ? entity->As<C_TerrorPlayer*>() : nullptr;
		if (!player) return;
		const Vector origin = player->m_vecOrigin();
		const Vector view = player->m_vecViewOffset();
		record.entIndex = victimEnt;
		record.userId = victimUserId;
		record.damage = event->GetInt("dmg_health", 0);
		record.x = origin.x;
		record.y = origin.y;
		record.z = origin.z;
		record.headZ = (view.z > 20.0f && view.z < 120.0f) ? view.z + 2.0f : 50.0f;
		Enqueue(record);
		return;
	}
	if (std::strcmp(name, "infected_hurt") == 0) {
		if (!IsLocalAttacker(event, "attacker")) return;
		const int entIndex = event->GetInt("entityid", 0);
		if (entIndex <= 0) return;
		auto* entity = I::ClientEntityList->GetClientEntity(entIndex);
		if (!entity) return;
		const Vector origin = entity->GetAbsOrigin();
		record.entIndex = entIndex;
		record.userId = 0;
		record.damage = event->GetInt("amount", 0);
		record.x = origin.x;
		record.y = origin.y;
		record.z = origin.z;
		record.headZ = 50.0f;
		Enqueue(record);
		return;
	}
}

} // namespace

namespace DamagePipeline {

bool InitListeners() {
	if (!I::GameEventManager) return false;
	static const char* const kEvents[] = {
		"player_hurt", "infected_hurt",
		"round_start", "round_end", "mission_lost", "map_transition",
	};
	bool allOk = true;
	for (const char* name : kEvents) {
		allOk = I::GameEventManager->AddListener(&g_listener, name, false) && allOk;
	}
	g_registered = true;
	return allOk;
}

void Shutdown() {
	if (g_registered && I::GameEventManager) I::GameEventManager->RemoveListener(&g_listener);
	g_registered = false;
	g_queueCount = 0;
	g_lockUserId = 0;
	g_lockEntity = 0;
	g_lockUntil = 0;
	g_lastAcceptedTick = 0;
}

void Reset() {
	++g_mapGeneration;
	g_queueCount = 0;
	g_lockUserId = 0;
	g_lockEntity = 0;
	g_lockUntil = 0;
	g_lastAcceptedTick = 0;
}

void Pump() {
	Drain();
}

} // namespace DamagePipeline
