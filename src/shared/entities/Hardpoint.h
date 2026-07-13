#pragma once
#include "LoadoutComponent.h"
#include "../../core/Module.h"
#include "raylib.h"
#include "raymath.h"
#include <algorithm>
#include <string>
#include <vector>

// Unified mount abstraction shared by fighters, capital ships, and stations:
// a container of module slots at a local offset, with its own hull. Superset
// of the legacy HardpointState (src/core/PlayerStation.h). Introduced
// alongside the existing types per docs/plans/unified_hardpoint_tasks.md P0 —
// no consumers yet; data-model migration happens in P1.
struct Hardpoint {
    std::string id;
    std::string displayName;
    bool        isCore       = false;
    bool        isDockingBay = false; // future capture/boarding target; no combat slots
    float       hull         = 100.0f;
    float       maxHull      = 100.0f;
    bool        alive        = true;
    Vector2     localOffset  = { 0.0f, 0.0f }; // craft/station-local, unrotated
    float       fireCooldown = 0.0f;

    // P3 power budget — derived by RecalculatePowerBudget() each tick/edit,
    // never serialized (SaveManager::HardpointSave deliberately omits these).
    // throttle scales output/recharge (1.0 = full); shed = fully offline.
    float throttle = 1.0f;
    bool  shed     = false;

    // P5 adjacency — derived by RecalculateAdjacency() each tick, never
    // serialized. adjacencyRateMult scales production/throughput (the
    // Mining<->Manufacturing bonus); adjacencyPowerDrawMult cuts effective
    // powerDraw when within range of a Reactor facility (consumed by
    // RecalculatePowerBudget below); shieldCovered marks a hardpoint inside a
    // live ShieldGenerator facility's coverage set (P6 — consumed by
    // ResolveHardpointHit in SpaceFlight.cpp as a flat damage-reduction
    // multiplier; goes false the tick its generator dies or is power-shed).
    float adjacencyRateMult      = 1.0f;
    float adjacencyPowerDrawMult = 1.0f;
    bool  shieldCovered          = false;

    // P7-T4 — player-tunable shed priority (deferred here from P3, which only
    // had the type-based DefaultShedPriority heuristic below). -1 (default)
    // means "not customized, use the heuristic"; once a player nudges it via
    // StationModuleMenu it holds an explicit value and is serialized
    // (SaveManager::HardpointSave), unlike throttle/shed/adjacency*, which
    // are recomputed every tick and never saved.
    int shedPriority = -1;

    std::vector<ModuleSlot> slots;

    // --- Type-filter accessors (P0-T4) ---
    // Let P1 call sites migrate mechanically off the old typed arrays
    // (hp.weapons[i], hp.armor, hp.shields[i], hp.engine, hp.aux[i]).

    ModuleSlot* FirstWeapon() {
        for (auto& s : slots) if (s.type == ModuleType::Weapon) return &s;
        return nullptr;
    }
    const ModuleSlot* FirstWeapon() const {
        for (auto& s : slots) if (s.type == ModuleType::Weapon) return &s;
        return nullptr;
    }

    std::vector<ModuleSlot*> WeaponSlots() {
        std::vector<ModuleSlot*> out;
        for (auto& s : slots) if (s.type == ModuleType::Weapon) out.push_back(&s);
        return out;
    }
    std::vector<const ModuleSlot*> WeaponSlots() const {
        std::vector<const ModuleSlot*> out;
        for (auto& s : slots) if (s.type == ModuleType::Weapon) out.push_back(&s);
        return out;
    }

    ModuleSlot* FirstShield() {
        for (auto& s : slots) if (s.type == ModuleType::Shield) return &s;
        return nullptr;
    }
    const ModuleSlot* FirstShield() const {
        for (auto& s : slots) if (s.type == ModuleType::Shield) return &s;
        return nullptr;
    }

    std::vector<ModuleSlot*> ShieldSlots() {
        std::vector<ModuleSlot*> out;
        for (auto& s : slots) if (s.type == ModuleType::Shield) out.push_back(&s);
        return out;
    }
    std::vector<const ModuleSlot*> ShieldSlots() const {
        std::vector<const ModuleSlot*> out;
        for (auto& s : slots) if (s.type == ModuleType::Shield) out.push_back(&s);
        return out;
    }

    ModuleSlot* Armor() {
        for (auto& s : slots) if (s.type == ModuleType::Armor) return &s;
        return nullptr;
    }
    const ModuleSlot* Armor() const {
        for (auto& s : slots) if (s.type == ModuleType::Armor) return &s;
        return nullptr;
    }

    ModuleSlot* Engine() {
        for (auto& s : slots) if (s.type == ModuleType::Engine) return &s;
        return nullptr;
    }
    const ModuleSlot* Engine() const {
        for (auto& s : slots) if (s.type == ModuleType::Engine) return &s;
        return nullptr;
    }

    std::vector<ModuleSlot*> AuxSlots() {
        std::vector<ModuleSlot*> out;
        for (auto& s : slots) if (s.type == ModuleType::Auxiliary) out.push_back(&s);
        return out;
    }
    std::vector<const ModuleSlot*> AuxSlots() const {
        std::vector<const ModuleSlot*> out;
        for (auto& s : slots) if (s.type == ModuleType::Auxiliary) out.push_back(&s);
        return out;
    }

    ModuleSlot* Hyperdrive() {
        for (auto& s : slots) if (s.type == ModuleType::Hyperdrive) return &s;
        return nullptr;
    }
    const ModuleSlot* Hyperdrive() const {
        for (auto& s : slots) if (s.type == ModuleType::Hyperdrive) return &s;
        return nullptr;
    }

    ModuleSlot* Facility() {
        for (auto& s : slots) if (s.type == ModuleType::Facility) return &s;
        return nullptr;
    }
    const ModuleSlot* Facility() const {
        for (auto& s : slots) if (s.type == ModuleType::Facility) return &s;
        return nullptr;
    }
};

// P3 — unified power budget (docs/plans/unified_hardpoint_tasks.md).
struct PowerBudget {
    float load     = 0.0f;
    float capacity = 0.0f;
    float ratio() const { return capacity > 0.0f ? load / capacity : (load > 0.0f ? 999.0f : 0.0f); }
};

namespace hardpoint_power_detail {
    inline float PerSlotDraw(const ModuleSlot& slot) {
        if (!slot.equipped) return 0.0f;
        switch (slot.equipped->type) {
            case ModuleType::Weapon:   return 2.0f;
            case ModuleType::Shield:   return 1.5f;
            case ModuleType::Armor:    return 1.0f;
            case ModuleType::Engine:   return 0.0f; // engines contribute to capacity, not load
            // P4-T6: a Reactor facility contributes powerOutput to capacity
            // instead (see the caller's enginePool branch) — it never reaches
            // PerSlotDraw as load. Every other facility kind draws its own
            // authored powerDraw rather than the flat non-facility default.
            case ModuleType::Facility: return slot.equipped->facility.powerDraw;
            default:                   return 1.0f;
        }
    }
    // Includes the P5 adjacencyPowerDrawMult (Reactor-efficiency cut) so this
    // stays consistent with RecalculatePowerBudget's own per-hardpoint sum —
    // the shedding pass below subtracts this from remainingLoad, which must
    // match what was added to pb.load in the first place.
    inline float HardpointLoad(const Hardpoint& hp) {
        float load = 0.0f;
        for (const auto& slot : hp.slots) load += PerSlotDraw(slot);
        return load * hp.adjacencyPowerDrawMult;
    }
    // Ascending = shed first. isCore hardpoints are never shed (see caller).
    inline int DefaultShedPriority(const Hardpoint& hp) {
        switch (hp.slots.empty() ? ModuleType::Auxiliary : hp.slots.front().type) {
            case ModuleType::Auxiliary: return 0;
            case ModuleType::Shield:    return 1;
            case ModuleType::Weapon:    return 2;
            case ModuleType::Armor:     return 3;
            default:                    return 4; // Engine/Hyperdrive: shed last
        }
    }
    // P7-T4: a player-set Hardpoint::shedPriority (>= 0) overrides the
    // type-based heuristic above; -1 (untouched) falls back to it.
    inline int EffectiveShedPriority(const Hardpoint& hp) {
        return hp.shedPriority >= 0 ? hp.shedPriority : DefaultShedPriority(hp);
    }
}

// Recomputes load/capacity from equipped modules (engines add capacity on
// top of baseCapacity — "no craft is dead at zero power", the P3-T1 open
// decision) and applies the throttle-then-shed pass: ratio<=1.0 normal;
// 1.0<ratio<=1.25 proportional throttle on every alive hardpoint; ratio>1.25
// sheds non-core hardpoints by ascending EffectiveShedPriority (the type-based
// DefaultShedPriority heuristic, or a player override — P7-T4) until back
// in-band, then throttles whatever's left. Writes derived throttle/shed onto
// each Hardpoint (never serialized). Works uniformly across a fighter's
// HardpointRig::hardpoints, a capital's NpcMeta::hardpoints, and a station's
// PlayerStation::hardpoints — callers pass whichever baseCapacity suits
// their craft type (HardpointRig::kBaseCapacity for fighters,
// HardpointRig::kStationBaseCapacity for capitals/stations).
inline PowerBudget RecalculatePowerBudget(std::vector<Hardpoint>& hardpoints, float baseCapacity) {
    using namespace hardpoint_power_detail;
    PowerBudget pb;
    float enginePool = 0.0f;
    for (auto& hp : hardpoints) {
        hp.throttle = 1.0f;
        hp.shed     = false;
        if (!hp.alive) continue;
        for (const auto& slot : hp.slots) {
            if (!slot.equipped) continue;
            if (slot.equipped->type == ModuleType::Engine) {
                enginePool += slot.equipped->engine.thrustBonus;
            } else if (slot.equipped->type == ModuleType::Facility &&
                       slot.equipped->facility.kind == FacilityKind::Reactor) {
                // P4-T6: Reactor facilities add to capacity, like engines do.
                enginePool += slot.equipped->facility.powerOutput;
            } else {
                pb.load += PerSlotDraw(slot) * hp.adjacencyPowerDrawMult;
            }
        }
    }
    pb.capacity = baseCapacity + enginePool;

    float ratio = pb.ratio();
    if (ratio <= 1.0f) return pb;

    if (ratio <= 1.25f) {
        float throttle = pb.capacity / pb.load;
        for (auto& hp : hardpoints) if (hp.alive) hp.throttle = throttle;
        return pb;
    }

    std::vector<Hardpoint*> order;
    for (auto& hp : hardpoints) if (hp.alive && !hp.isCore) order.push_back(&hp);
    // stable_sort: ties (equal priority, e.g. two untouched weapon hardpoints)
    // keep their original vector order rather than an unspecified one, so
    // shed order stays predictable tick-to-tick for the player.
    std::stable_sort(order.begin(), order.end(), [](Hardpoint* a, Hardpoint* b) {
        return EffectiveShedPriority(*a) < EffectiveShedPriority(*b);
    });
    float remainingLoad = pb.load;
    for (Hardpoint* hp : order) {
        if (remainingLoad <= pb.capacity * 1.25f) break;
        float hpLoad = HardpointLoad(*hp);
        if (hpLoad <= 0.0f) continue;
        hp->shed     = true;
        hp->throttle = 0.0f;
        remainingLoad -= hpLoad;
    }
    if (remainingLoad > pb.capacity) {
        float throttle = pb.capacity / remainingLoad;
        for (auto& hp : hardpoints) if (hp.alive && !hp.shed) hp.throttle = throttle;
    }
    return pb;
}

namespace hardpoint_adjacency_detail {
    // Tuned against capital hardpoint spacing in config/ships.json (~90-140
    // units between neighboring mounts, offset {0,0} at the hull center) so a
    // capital's adjacent turrets/generators actually interact but far mounts
    // don't. NPC/player stations don't have per-hardpoint localOffset data
    // yet — BuildNpcStationHardpoints/FleetManager::SpawnStation leave it at
    // the {0,0} default; a station's only real positions are a cosmetic ring
    // computed at draw time (GetNpcStationHardpointPos/GetHardpointPos in
    // SpaceFlight.cpp), not stored on the Hardpoint. Until P8-T3 gives
    // stations real per-hardpoint offsets, every station hardpoint sits at
    // distance 0 from every other, so adjacency on a station is effectively
    // "anywhere on the station" — a known interim simplification, same
    // category as P3-T1's kStationBaseCapacity placeholder.
    constexpr float kAdjacencyRadius         = 150.0f;
    constexpr float kMiningManufacturingMult = 1.25f; // throughput bonus, both sides
    constexpr float kReactorEfficiencyMult   = 0.85f; // load cut for nearby consumers
    // P6 — damage reduction applied to a shieldCovered hardpoint in
    // ResolveHardpointHit (SpaceFlight.cpp). Collapses to 0 automatically the
    // tick a covering ShieldGenerator dies or gets shed for power, since
    // shieldCovered itself goes false then (see the generatorActive gate in
    // RecalculateAdjacency below) — no separate "collapse" step needed.
    constexpr float kShieldGeneratorMitigation = 0.5f;
}

// Distance-based bonus pass (P5 — docs/plans/unified_hardpoint_tasks.md).
// Walks every alive-hardpoint pair where the first has an installed Facility
// with a set AdjacencyTag and, for pairs within kAdjacencyRadius (localOffset
// distance), applies:
//   Reactor -> any hardpoint with load: adjacencyPowerDrawMult is set to the
//     efficiency multiplier, not stacked (multiple reactors don't compound
//     past the floor — same anti-runaway reasoning as the shed-then-throttle
//     ordering in RecalculatePowerBudget). Consumed by that function, so
//     callers MUST run RecalculateAdjacency first — see
//     UpdatePlayerStations/UpdateNpcShips in SpaceFlight.cpp.
//   Mining <-> Manufacturing: both hardpoints' adjacencyRateMult is raised to
//     the bonus (consumed by whichever production tick reads it, e.g.
//     TickStationMining).
//   ShieldGenerator -> itself + neighbors: shieldCovered = true, but only
//     while the generator itself is alive AND not power-shed (P6 —
//     generatorActive below) — this is the "collapse shields on generator
//     death or shed-for-power" rule from
//     docs/plans/unified_hardpoint_tasks.md P6-T2. Since this function reruns
//     every tick before RecalculatePowerBudget, the moment a generator dies
//     or gets shed, every hardpoint it was covering loses shieldCovered on
//     the very next recompute — no separate collapse pass needed. Consumed by
//     ResolveHardpointHit (SpaceFlight.cpp) as a flat damage-reduction
//     multiplier (kShieldGeneratorMitigation) while shieldCovered holds.
inline void RecalculateAdjacency(std::vector<Hardpoint>& hardpoints) {
    using namespace hardpoint_adjacency_detail;
    for (auto& hp : hardpoints) {
        hp.adjacencyRateMult      = 1.0f;
        hp.adjacencyPowerDrawMult = 1.0f;
        hp.shieldCovered          = false;
    }
    size_t n = hardpoints.size();
    for (size_t i = 0; i < n; ++i) {
        Hardpoint& hi = hardpoints[i];
        if (!hi.alive) continue;
        const ModuleSlot* fi = hi.Facility();
        AdjacencyTag tagI = (fi && fi->equipped) ? fi->equipped->facility.adjTag : AdjacencyTag::None;
        if (tagI == AdjacencyTag::None) continue;
        // P6: a shed generator is still "alive" (shed only zeroes throttle,
        // per RecalculatePowerBudget) but is drawing no effective power, so
        // it must not keep projecting coverage — shed uses last tick's value
        // here since RecalculateAdjacency runs before this tick's
        // RecalculatePowerBudget (same one-tick-stale convention P5 already
        // accepted for the reverse dependency, adjacencyPowerDrawMult).
        bool generatorActive = tagI != AdjacencyTag::ShieldGenerator || !hi.shed;
        if (tagI == AdjacencyTag::ShieldGenerator && generatorActive) hi.shieldCovered = true;

        for (size_t j = 0; j < n; ++j) {
            if (i == j) continue;
            Hardpoint& hj = hardpoints[j];
            if (!hj.alive) continue;
            if (Vector2Distance(hi.localOffset, hj.localOffset) > kAdjacencyRadius) continue;

            if (tagI == AdjacencyTag::Reactor) {
                if (hardpoint_power_detail::HardpointLoad(hj) > 0.0f)
                    hj.adjacencyPowerDrawMult = std::min(hj.adjacencyPowerDrawMult, kReactorEfficiencyMult);
            } else if (tagI == AdjacencyTag::ShieldGenerator) {
                if (generatorActive) hj.shieldCovered = true;
            } else if (tagI == AdjacencyTag::Mining || tagI == AdjacencyTag::Manufacturing) {
                const ModuleSlot* fj = hj.Facility();
                AdjacencyTag tagJ = (fj && fj->equipped) ? fj->equipped->facility.adjTag : AdjacencyTag::None;
                AdjacencyTag wantJ = (tagI == AdjacencyTag::Mining) ? AdjacencyTag::Manufacturing : AdjacencyTag::Mining;
                if (tagJ == wantJ) {
                    hi.adjacencyRateMult = std::max(hi.adjacencyRateMult, kMiningManufacturingMult);
                    hj.adjacencyRateMult = std::max(hj.adjacencyRateMult, kMiningManufacturingMult);
                }
            }
        }
    }
}

// A full mount rig for a craft or station: an ordered set of Hardpoints plus
// its aggregate power budget. Power fields are fleshed out in P3-T1; present
// now so P1 migration code has a stable target shape.
//
// Fighters (P1-T5): each individual mount is its own 1-slot Hardpoint — no
// clustering like a station's multi-slot weapon battery — so the accessors
// below search across `hardpoints` (one level up from Hardpoint's own
// same-named accessors, which search within a single Hardpoint's `slots`).
struct HardpointRig {
    std::vector<Hardpoint> hardpoints;

    float currentLoad = 0.0f;
    float maxCapacity = 0.0f;

    static constexpr float kBaseCapacity         = 5.0f;  // fighters: base power budget without engines
    // Stations/capitals have no player-installed movement engines feeding the
    // same pool, so they need a much larger flat base or every existing
    // station would be instantly overloaded the moment this landed — that
    // would violate "existing gameplay is unchanged unless a task explicitly
    // changes it". This number is a placeholder, not a balance pass (that's
    // P8-T4); it's picked to keep today's station/capital defs comfortably
    // under threshold while still leaving overload reachable if you cram a
    // hardpoint layout well beyond what any current def ships.
    static constexpr float kStationBaseCapacity  = 30.0f;
    static constexpr float kOverloadThrustFactor = 0.5f; // velocity scale when overloaded
    static constexpr float kOverloadCooldownMult = 2.0f; // cooldown multiplier when overloaded

    bool IsOverloaded() const { return currentLoad > maxCapacity; }

    // Recomputes currentLoad/maxCapacity (and per-hardpoint throttle/shed —
    // P3) from equipped modules.
    void RecalculateLoad() {
        PowerBudget pb = RecalculatePowerBudget(hardpoints, kBaseCapacity);
        currentLoad = pb.load;
        maxCapacity = pb.capacity;
    }

    std::vector<ModuleSlot*> WeaponSlots() {
        std::vector<ModuleSlot*> out;
        for (auto& hp : hardpoints) if (ModuleSlot* s = hp.FirstWeapon()) out.push_back(s);
        return out;
    }
    std::vector<const ModuleSlot*> WeaponSlots() const {
        std::vector<const ModuleSlot*> out;
        for (auto& hp : hardpoints) if (const ModuleSlot* s = hp.FirstWeapon()) out.push_back(s);
        return out;
    }

    std::vector<ModuleSlot*> ShieldSlots() {
        std::vector<ModuleSlot*> out;
        for (auto& hp : hardpoints) if (ModuleSlot* s = hp.FirstShield()) out.push_back(s);
        return out;
    }
    std::vector<const ModuleSlot*> ShieldSlots() const {
        std::vector<const ModuleSlot*> out;
        for (auto& hp : hardpoints) if (const ModuleSlot* s = hp.FirstShield()) out.push_back(s);
        return out;
    }

    std::vector<ModuleSlot*> AuxSlots() {
        std::vector<ModuleSlot*> out;
        for (auto& hp : hardpoints) if (!hp.AuxSlots().empty()) out.push_back(hp.AuxSlots()[0]);
        return out;
    }
    std::vector<const ModuleSlot*> AuxSlots() const {
        std::vector<const ModuleSlot*> out;
        for (auto& hp : hardpoints) if (!hp.AuxSlots().empty()) out.push_back(hp.AuxSlots()[0]);
        return out;
    }

    ModuleSlot* Armor() {
        for (auto& hp : hardpoints) if (ModuleSlot* s = hp.Armor()) return s;
        return nullptr;
    }
    const ModuleSlot* Armor() const {
        for (auto& hp : hardpoints) if (const ModuleSlot* s = hp.Armor()) return s;
        return nullptr;
    }

    ModuleSlot* Engine() {
        for (auto& hp : hardpoints) if (ModuleSlot* s = hp.Engine()) return s;
        return nullptr;
    }
    const ModuleSlot* Engine() const {
        for (auto& hp : hardpoints) if (const ModuleSlot* s = hp.Engine()) return s;
        return nullptr;
    }

    ModuleSlot* Hyperdrive() {
        for (auto& hp : hardpoints) if (ModuleSlot* s = hp.Hyperdrive()) return s;
        return nullptr;
    }
    const ModuleSlot* Hyperdrive() const {
        for (auto& hp : hardpoints) if (const ModuleSlot* s = hp.Hyperdrive()) return s;
        return nullptr;
    }

    // Rebuilds the mount list: wSlots weapon mounts, exactly 1 armor mount,
    // shSlots shield mounts, exactly 1 engine mount, exactly 1 hyperdrive
    // mount, auxSlots aux mounts — mirrors the old ShipLoadout::Resize
    // semantics exactly (armor/engine/hyperdrive were always singleton).
    //
    // Also assigns each mount a placeholder localOffset (final on-screen
    // pixel units, same convention as capital/station hardpoints — see
    // GetCapitalHardpointWorldPos in SpaceFlight.cpp) so P2's
    // DrawHardpointRig has somewhere to draw fighter module icons; no
    // per-ship-silhouette layout data exists yet, so mounts of a given type
    // are just spread evenly left/right by row (weapons forward, aux mid,
    // armor/engine/hyperdrive aft) rather than fit to the hull art.
    void Resize(int wSlots, int shSlots, int auxSlots) {
        hardpoints.clear();
        auto mount = [](ModuleType t, Vector2 offset) {
            Hardpoint hp;
            hp.localOffset = offset;
            hp.slots.push_back({ t });
            return hp;
        };
        auto rowOffset = [](int index, int count, float y) -> Vector2 {
            constexpr float kSpacing = 6.0f;
            return { (index - (count - 1) * 0.5f) * kSpacing, y };
        };
        for (int i = 0; i < wSlots; ++i)
            hardpoints.push_back(mount(ModuleType::Weapon, rowOffset(i, wSlots, -6.0f)));
        hardpoints.push_back(mount(ModuleType::Armor, { 0.0f, 5.0f }));
        for (int i = 0; i < shSlots; ++i)
            hardpoints.push_back(mount(ModuleType::Shield, rowOffset(i, shSlots, 0.0f)));
        hardpoints.push_back(mount(ModuleType::Engine, { 0.0f, 8.0f }));
        hardpoints.push_back(mount(ModuleType::Hyperdrive, { 0.0f, 10.0f }));
        for (int i = 0; i < auxSlots; ++i)
            hardpoints.push_back(mount(ModuleType::Auxiliary, rowOffset(i, auxSlots, 3.0f)));
        RecalculateLoad();
    }
};
