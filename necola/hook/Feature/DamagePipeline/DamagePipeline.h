#pragma once

#include "../../../sdk/SDK.h"

// Damage pipeline — a from-scratch port of skeeto's hit-detection core.
//
// Skeeto splits the work into three stages and so does this module; nothing here
// draws anything:
//
//   producer  SKT_Stats_QueueDamageEvent
//             the game-event callback only stamps a record
//                 {tick, mapGeneration, entIndex, userId, damage, position}
//             into a fixed 64-slot queue and returns. No decisions.
//
//   consumer  SKT_Stats_ConsumePendingDamage
//             once per frame drain up to 32 records and reject the ones that are
//             stale (older than 400 ms), from a previous map generation, or whose
//             user id no longer resolves to the recorded entity index.
//
//   gate      SKT_Si_TrackHitFeedback
//             far cone for gun hits (5000 units, dot >= 0.93)
//             -> 70 ms global throttle
//             -> near cone for non-gun hits (110 units, dot >= 0.90)
//             -> lock the victim for 400 ms; repeats inside the lock skip the
//                throttle and both cone tests
//
// Accepted hits are dispatched to the renderers:
//   * HitFeedback   damage number + crosshair marker
//   * KillFeedback  themed hit overlay (it keeps its own HitMode gate)
namespace DamagePipeline {

// Registers the game-event listener (hits, shot confirmation, round resets).
bool InitListeners();
void Shutdown();
// Level/round change: bump the map generation and drop queued records + the lock.
void Reset();
// Called once per frame; drains and dispatches confirmed hits.
void Pump();

} // namespace DamagePipeline
