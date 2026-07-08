#pragma once

#include "core/FactionEnum.h"
#include "systems/diplomacy/DiplomaticRegistry.h"

// Epic 6.1: continuous per-faction PLAYER standing (-100..100), replacing
// the fixed DiplomaticRegistry matrix lookup for the player's own relation
// to each faction specifically. The static faction-vs-faction matrix in
// DiplomaticRegistry keeps governing NPC-vs-NPC relations unchanged — this
// registry only answers "how does faction F feel about the player right
// now," which drifts continuously via gameplay events (kills, trade) and
// decay instead of sitting on a fixed enum value.
//
// Shared per player-faction, not per-player — matches how discovery is
// already pooled "same faction" (see galaxy-map feature #5). In
// multiplayer every peer of the same faction reads/writes the same score.
class ReputationRegistry {
public:
    static constexpr float kFriendlyThreshold =  25.0f;
    static constexpr float kHostileThreshold  = -25.0f;

    // Re-seeds every faction's score from the static DiplomaticRegistry
    // matrix (Hostile=-60 / Neutral=0 / Friendly=60) relative to
    // `playerFaction`, so day-one behavior matches the existing tuned
    // matrix before any reputation events have fired. Call once per
    // session, right after kPlayerFaction is assigned (SpaceFlight::OnEnter).
    // Also records this seed as each faction's decay baseline (see Tick).
    static void ResetForPlayerFaction(Faction playerFaction);

    static float    Score(Faction f);
    // No-ops for f == the player's own faction (nothing to adjust against yourself).
    static void     Adjust(Faction f, float delta);
    // f == player's own faction always reads Friendly; otherwise threshold-mapped from Score(f).
    static Relation PlayerRelation(Faction f);
    // Slowly pulls every faction's score back toward its matrix-seeded
    // baseline (not absolute 0/neutral) over time. Bug fix: decaying all the
    // way to 0 eroded a faction's lore-defined stance (e.g. Republic's -60
    // seed vs. Reavers) past the +/-25 relation threshold within ~90s of no
    // reinforcing events, silently flipping already-hostile factions Neutral
    // — and since an individual NPC's NpcFaction is snapshotted once at
    // spawn (SpaceFlight.cpp's MakeNpcEntity) rather than re-evaluated live,
    // that flip would permanently lock in for any NPC spawned afterward.
    // Decaying toward the baseline instead lets event-driven swings (kills,
    // trades) still rise/fall and settle back down, without erasing the
    // underlying diplomatic relationship they swung away from.
    static void     Tick(float dt);

private:
    static Faction s_playerFaction;
    static float   s_score[9];
    static float   s_baseline[9];
};
