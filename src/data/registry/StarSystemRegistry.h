#pragma once
#include "core/StarSystem.h"
#include "raylib.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Galactic distances from Sol (0, 0) — hyperdrive tier requirements (see
// HyperdriveDefs.h for the full rationale behind each range):
//   Short-Jump        3 000 u — in-system only
//   Sector Drive     50 000 u — closest neighboring star systems
//   Warp Core       300 000 u — local cluster of systems
//   Fold Engine   2 000 000 u — deeper into the galaxy
//   Quantum Leap 10 000 000 u — far reaches of the galaxy
//   Void Piercer 50 000 000 u — intergalactic; see UniverseRegistry.h
//   Singularity / Cosmic Fold 6 000 000 000 u — anywhere in the universe
//
// The galaxy is a procedurally generated grid-hash lattice keyed off a single
// master seed (see Init()) — no per-system data is ever stored in bulk.
// Generate()/ById() compute a system's seed/position on demand in O(1);
// QueryRegion() is the only entry point that touches more than one system at
// a time, and it's bounded to a caller-supplied result budget regardless of
// how large a world-space rect it's asked about (see its comment below).
//
// Scale: kGalaxySpan/GridDim gives a ~80,000u nominal star-to-star spacing,
// with jitter capped so the worst-case adjacent pair is still ~40,000u apart.
// That's kept deliberately well above the largest possible in-system travel
// distance (~25,000u — two planets at opposite max orbits around a rare
// O-class star; see SpaceFlight::SpawnPlanetsAndStations) so the map reads
// at two clearly distinct scales: tight, planet/station-scale hops within a
// system, and vastly larger star-to-star gaps between systems.

namespace StarSystemRegistry {

// 3,000,000 id-slots instead of 1,000,000: the density field below thins out
// a large fraction of cells (between spiral arms, far rim), so the id-space
// needs headroom for populated regions (core, arms) to still feel dense.
// Generation stays O(1) regardless of count, so this costs nothing idle.
inline constexpr unsigned int kDefaultCount = 3'000'000u;    // adjustable via Init()
inline constexpr float        kGalaxySpan   = 80'000'000.0f; // world spans [-span/2, +span/2]

// Fraction of systems controlled by a single faction (and so get exactly one
// NPC station) rather than uncontrolled (no station at all) — see the
// control roll in Generate() and SpaceFlight::SpawnPlanetsAndStations, which
// reads StarSystem::isControlled/controllingFaction instead of independently
// rolling a station count/faction per system as before.
inline constexpr float kControlChance = 0.15f;

// Selects which closed-form density() formula shapes the galaxy. Spiral and
// Elliptical share the same core+disk formula (see DensityShape) — a
// "circular" galaxy is just an Elliptical one whose seed rolled an aspect
// ratio near 1, the same way astronomers classify circular galaxies as E0
// ellipticals rather than a separate category. Irregular is meant to slot in
// here later as a genuinely different (noise-based) formula, not a rewrite
// of the pipeline. Auto is only valid as an Init() argument — it resolves to
// Spiral or Elliptical from the seed before anything reads ShapeRef().
enum class GalaxyShape { Spiral, Elliptical, Auto };

// Tunable parameters for DensityShape, derived once from the master seed
// (see ComputeShapeParams) so different seeds produce visibly different
// galaxies — bigger/smaller cores, tighter/looser arms, rounder or more
// elongated bodies at different orientations — rather than every game
// sharing the exact same shape and only the individual stars within it
// varying.
struct GalaxyShapeParams {
    float coreRadius   = 0.12f; // fraction of half-span: core body size
    float coreInner    = 0.65f; // fraction of coreRadius that's fully solid
    float diskScale    = 0.32f; // fraction of half-span: disk falloff
    int   armCount     = 3;     // Spiral only
    float armTanPitch  = 0.30f; // Spiral only: controls how tightly arms wind
    float armWidth     = 0.38f; // Spiral only, radians: arm thickness
    float armBaseline  = 0.12f; // Spiral only: field-star floor between arms
    float aspectRatio  = 1.0f;  // 1 = circular; lower = more elongated
    float axisRotation = 0.0f;  // radians: direction of elongation
};

namespace detail {

// Simple integer hash (Wang/xorshift-style mix) used to derive per-system
// seeds and position jitter from (masterSeed, id, salt) — deterministic,
// no allocation, cheap enough to call thousands of times per frame.
inline uint32_t Hash32(uint32_t a, uint32_t b, uint32_t salt) {
    uint32_t h = a * 0x9E3779B1u ^ b * 0x85EBCA77u ^ salt * 0xC2B2AE3Du;
    h ^= h >> 15; h *= 0x2545F491u;
    h ^= h >> 13; h *= 0x9E3779B1u;
    h ^= h >> 16;
    return h;
}

inline std::string GenerateName(unsigned int seed) {
    auto rng = [s = seed + 1u]() mutable -> unsigned int {
        s ^= s << 13u;
        s ^= s >> 17u;
        s ^= s << 5u;
        return s;
    };

    static const char* kOnsets[] = {
        "b","c","d","f","g","h","k","l","m","n","p","r","s","t","v","z",
        "br","cr","dr","fr","gr","pr","tr","kh","th","sh","zh","vr","xl"
    };
    static const char* kVowels[] = {
        "a","e","i","o","u","ae","ei","io","ou","ai","ia","eo"
    };
    static const char* kCodas[] = {
        "","n","r","s","x","k","l","m","nd","st","rk","rx","th"
    };
    const int nOnsets = (int)(sizeof(kOnsets) / sizeof(kOnsets[0]));
    const int nVowels = (int)(sizeof(kVowels) / sizeof(kVowels[0]));
    const int nCodas  = (int)(sizeof(kCodas)  / sizeof(kCodas[0]));

    int syllables = 2 + (int)(rng() % 2);
    std::string name;

    for (int i = 0; i < syllables; ++i) {
        if (i == 0 && rng() % 4 == 0) {
            // vowel-initial start
        } else {
            name += kOnsets[rng() % nOnsets];
        }
        name += kVowels[rng() % nVowels];
        if (rng() % 2 == 0)
            name += kCodas[1 + rng() % (nCodas - 1)];
    }

    if (!name.empty())
        name[0] = (char)toupper((unsigned char)name[0]);
    return name;
}

inline unsigned int&       MasterSeedRef() { static unsigned int       s = 1u; return s; }
inline unsigned int&       CountRef()      { static unsigned int       s = kDefaultCount; return s; }
inline GalaxyShape&        ShapeRef()      { static GalaxyShape        s = GalaxyShape::Spiral; return s; }
inline GalaxyShapeParams&  ShapeParamsRef(){ static GalaxyShapeParams  s{}; return s; }

inline float HashToUnit(uint32_t h) { return (h & 0xFFFFFFu) / 16777216.0f; } // 24 bits -> [0,1)
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

// Rolls each shape parameter from an independent salt so they vary
// independently of each other, all driven by the one master seed. Elliptical
// galaxies get a wide aspect-ratio range (round to quite elongated, E0-E7
// style); Spiral stays closer to circular so the arm structure — already the
// dominant visual feature — isn't also fighting a strong ellipse distortion.
inline void ComputeShapeParams(unsigned int masterSeed, GalaxyShape shape, GalaxyShapeParams& p) {
    auto roll = [&](uint32_t salt) { return HashToUnit(Hash32(masterSeed, 0u, salt)); };
    p.coreRadius   = Lerp(0.06f, 0.20f, roll(0x1001u));
    p.coreInner    = Lerp(0.50f, 0.80f, roll(0x1002u));
    p.diskScale    = Lerp(0.22f, 0.42f, roll(0x1003u));
    p.armCount     = 2 + (int)(roll(0x1004u) * 4.0f); // 2..5
    p.armTanPitch  = Lerp(0.18f, 0.42f, roll(0x1005u));
    p.armWidth     = Lerp(0.28f, 0.50f, roll(0x1006u));
    p.armBaseline  = Lerp(0.06f, 0.20f, roll(0x1007u));
    p.aspectRatio  = (shape == GalaxyShape::Elliptical)
                    ? Lerp(0.35f, 1.0f, roll(0x1008u))
                    : Lerp(0.80f, 1.0f, roll(0x1008u));
    p.axisRotation = Lerp(0.0f, 2.0f * PI, roll(0x1009u));
}

// Wraps an angle (radians) into (-pi, pi].
inline float WrapAngle(float a) {
    while (a >  PI) a -= 2.0f * PI;
    while (a < -PI) a += 2.0f * PI;
    return a;
}

inline float SmoothstepLocal(float edge0, float edge1, float x) {
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Rotates into the galaxy's elongation axis, then squashes the minor axis —
// turns a circle into an ellipse of the seed-rolled aspect ratio, oriented
// in the seed-rolled direction (without a rotation, every elongated galaxy
// would stretch along the same world axis, which would look repetitive
// across seeds).
inline Vector2 ApplyEllipseTransform(Vector2 pos, float aspect, float rotation) {
    float c = cosf(-rotation), s = sinf(-rotation);
    float rx = pos.x * c - pos.y * s;
    float ry = pos.x * s + pos.y * c;
    ry /= std::max(aspect, 0.05f);
    return { rx, ry };
}

// Closed-form density in [0,1], pure function of position, no simulation or
// storage, cheap enough to call once per candidate id inside Generate().
// Shared by Spiral and Elliptical: a solid core body (smoothstep-edged, so
// it reads as an actual body rather than a gradient that never resolves
// into a shape) plus a disk falloff, both evaluated in ellipse-transformed
// space so the whole galaxy can be round or elongated at any orientation.
// Spiral additionally boosts density near a handful of logarithmic spiral
// arms; Elliptical (which covers "circular" as the aspect~=1 case) doesn't
// need a different formula, just the arm term switched off.
inline float DensityShape(Vector2 pos) {
    constexpr float kHalfSpan = kGalaxySpan * 0.5f;
    const GalaxyShapeParams& p = ShapeParamsRef();

    Vector2 tp    = ApplyEllipseTransform(pos, p.aspectRatio, p.axisRotation);
    float   rNorm = sqrtf(tp.x * tp.x + tp.y * tp.y) / kHalfSpan;

    // Core: solid out to coreRadius*coreInner, then a clean smoothstep taper
    // to 0 by coreRadius — a real body with an edge, not a gradient that
    // never quite resolves into a shape.
    float coreDensity = 1.0f - SmoothstepLocal(p.coreRadius * p.coreInner, p.coreRadius, rNorm);

    float diskFalloff  = expf(-rNorm / p.diskScale);
    float shapeDensity = diskFalloff;

    if (ShapeRef() == GalaxyShape::Spiral) {
        float theta     = atan2f(tp.y, tp.x);
        float logR      = logf(std::max(rNorm, 0.01f));
        float armFactor = 0.0f;
        for (int k = 0; k < p.armCount; ++k) {
            float phase   = (float)k * (2.0f * PI / (float)p.armCount);
            float armAng  = phase + logR / p.armTanPitch;
            float dTheta  = WrapAngle(theta - armAng);
            float contrib = expf(-(dTheta * dTheta) / (2.0f * p.armWidth * p.armWidth));
            armFactor     = std::max(armFactor, contrib);
        }
        shapeDensity = diskFalloff * (p.armBaseline + (1.0f - p.armBaseline) * armFactor);
    }

    // Tapers density to exactly 0 by rNorm==1 regardless of diskScale/coreRadius
    // (which otherwise can leave a non-trivial density right up to the edge of
    // the finite id-grid — e.g. ~9% at the loosest diskScale). Without this,
    // stars exist at some rate all the way to the last populated cell, then
    // the grid simply runs out — a hard wall rather than a dissipating edge.
    // This also covers square-grid corners/rotated axes: rNorm==1 is reached
    // at or before the closest point of the square boundary for any aspect
    // ratio or rotation (squashing the minor axis only inflates rNorm faster),
    // so the fade always completes strictly inside the grid, never past it.
    float edgeFade = 1.0f - SmoothstepLocal(0.80f, 1.0f, rNorm);

    float density = std::max(coreDensity, shapeDensity) * edgeFade;
    return std::clamp(density, 0.0f, 1.0f);
}

inline float Density(Vector2 pos) {
    switch (ShapeRef()) {
    case GalaxyShape::Spiral:
    case GalaxyShape::Elliptical:
    default:
        return DensityShape(pos);
    }
}

} // namespace detail

// Turns a player-typed seed string into a deterministic master seed (FNV-1a).
inline uint32_t HashSeedString(const std::string& text) {
    uint32_t h = 2166136261u;
    for (unsigned char c : text) {
        h ^= c;
        h *= 16777619u;
    }
    return h == 0u ? 1u : h; // 0 is reserved as "uninitialized"
}

// Must be called once per game session (New Game / Load / multiplayer
// WorldSync) before any other function in this namespace is used. Shape
// defaults to Auto — a coin flip derived from the seed itself — so existing
// callers that don't care get seed-driven variety for free; pass Spiral or
// Elliptical explicitly to force one.
inline void Init(unsigned int masterSeed, unsigned int count = kDefaultCount,
                  GalaxyShape shape = GalaxyShape::Auto) {
    detail::MasterSeedRef() = masterSeed == 0u ? 1u : masterSeed;
    detail::CountRef()      = count == 0u ? 1u : count;

    GalaxyShape resolved = shape;
    if (resolved == GalaxyShape::Auto) {
        float r  = detail::HashToUnit(detail::Hash32(detail::MasterSeedRef(), 0u, 0x2A2Au));
        resolved = (r < 0.5f) ? GalaxyShape::Spiral : GalaxyShape::Elliptical;
    }
    detail::ShapeRef() = resolved;
    detail::ComputeShapeParams(detail::MasterSeedRef(), resolved, detail::ShapeParamsRef());
}

inline unsigned int Count() { return detail::CountRef(); }

// Systems live on a GridDim x GridDim lattice (GridDim = ceil(sqrt(Count()))
// so the default 1,000,000 is an exact 1000x1000 grid). id = cy*dim + cx + 1.
inline unsigned int GridDim() {
    return (unsigned int)std::ceil(std::sqrt((double)detail::CountRef()));
}

inline float CellSize() { return kGalaxySpan / (float)GridDim(); }

// O(1), no storage — computes a system's seed + galactic position purely
// from its id and the current master seed. id == 1 is special-cased to the
// origin (home/Sol). Caller must ensure id is in [1, Count()].
inline StarSystem Generate(unsigned int id) {
    StarSystem s;
    s.id      = id;
    s.seed    = detail::Hash32(detail::MasterSeedRef(), id, 0xA53Fu);
    s.hasStar = true;

    if (id == 1) {
        s.galacticPos = { 0.0f, 0.0f }; // home system always exists
        s.cellCenter  = { 0.0f, 0.0f };
        // Forced controlled by the player's own starting faction (see
        // kPlayerFaction in SpaceFlight.cpp) rather than rolled like every
        // other system — guarantees a friendly station to dock at from the
        // very start, regardless of how the control roll below would land.
        s.isControlled      = true;
        s.controllingFaction = Faction::Republic;
        return s;
    }

    unsigned int dim = GridDim();
    unsigned int idx = id - 1;
    unsigned int cx  = idx % dim;
    unsigned int cy  = idx / dim;

    float cellSize     = CellSize();
    float originOffset = kGalaxySpan * 0.5f;
    float cellCX = (cx + 0.5f) * cellSize - originOffset;
    float cellCY = (cy + 0.5f) * cellSize - originOffset;
    s.cellCenter = { cellCX, cellCY };

    // Jitter within the cell (up to 50% of cell size, i.e. +/-25%) so systems
    // don't sit on a visibly perfect grid, while keeping the worst-case
    // adjacent-pair distance (cellSize * (1 - 2*0.25) = cellSize * 0.5) safely
    // above the max in-system travel distance — see the scale note up top.
    uint32_t jh = detail::Hash32(detail::MasterSeedRef(), id, 0x51ED27u);
    float jx = ((jh & 0xFFFFu) / 65535.0f - 0.5f) * cellSize * 0.5f;
    float jy = (((jh >> 16) & 0xFFFFu) / 65535.0f - 0.5f) * cellSize * 0.5f;

    s.galacticPos = { cellCX + jx, cellCY + jy };

    // Existence roll against the galaxy's density field at this cell — the
    // grid is still the id-space/lookup lattice, but most cells outside the
    // populated core/arms come up empty, which is what actually produces an
    // organic shape instead of a uniform field of stars.
    uint32_t eh   = detail::Hash32(detail::MasterSeedRef(), id, 0x9A11u);
    float    roll = detail::HashToUnit(eh);
    s.density     = detail::Density(s.galacticPos);
    s.hasStar     = roll < s.density;

    // Control roll: most systems are uncontrolled (no station at all); a
    // minority (kControlChance) are controlled by a single faction and get
    // exactly one station for it — see SpawnPlanetsAndStations. Independent
    // of the existence roll above (a system that doesn't exist never gets
    // this far anyway, per ById()'s hasStar filter) and of each other's
    // per-station rolls, unlike the old id-modulo-per-station scheme.
    uint32_t ch = detail::Hash32(detail::MasterSeedRef(), id, 0xC0117Fu);
    s.isControlled = detail::HashToUnit(ch) < kControlChance;
    if (s.isControlled) {
        uint32_t fh = detail::Hash32(detail::MasterSeedRef(), id, 0xFAC7104u);
        s.controllingFaction = static_cast<Faction>(fh % static_cast<uint32_t>(Faction::COUNT));
    }

    return s;
}

inline std::optional<StarSystem> ById(unsigned int id) {
    if (id == 0u || id > detail::CountRef()) return std::nullopt;
    StarSystem s = Generate(id);
    if (!s.hasStar) return std::nullopt;
    return s;
}

// The only function that touches more than a handful of systems. Bounds the
// number of systems returned to maxResults regardless of how large worldRect
// is — beyond that budget it strides through the grid so the result set
// thins out uniformly rather than growing unbounded. This is what makes it
// safe to call every frame while the galactic map is open, at any zoom
// level, even though the underlying galaxy may hold 1,000,000 systems.
//
// outSampleSpacing (optional): the actual world-space distance between
// adjacent samples (stride * cellSize) for this call. A caller rendering
// the returned points as tiles sized to this spacing gets a gapless mosaic —
// adjacent surviving cells touch edge-to-edge — instead of small markers
// floating with visible gaps in a much larger cell footprint.
inline std::vector<StarSystem> QueryRegion(Rectangle worldRect, int maxResults,
                                            float* outSampleSpacing = nullptr) {
    std::vector<StarSystem> out;

    unsigned int dim         = GridDim();
    float        cellSize    = CellSize();
    float        originOffset = kGalaxySpan * 0.5f;

    auto ToCell = [&](float world) -> long long {
        return (long long)std::floor((world + originOffset) / cellSize);
    };

    long long cx0 = std::max(0LL, ToCell(worldRect.x));
    long long cx1 = std::min((long long)dim - 1, ToCell(worldRect.x + worldRect.width));
    long long cy0 = std::max(0LL, ToCell(worldRect.y));
    long long cy1 = std::min((long long)dim - 1, ToCell(worldRect.y + worldRect.height));

    // Home (id==1) is special-cased in Generate() to sit at the world origin
    // rather than in its "natural" lattice slot (idx 0 -> cell 0,0, a grid
    // corner far from world (0,0)) — see the comment there. That means the
    // cell-index walk below can never land on it even when worldRect covers
    // the origin, so it needs this explicit, separate check — EXCEPT when
    // cx0==0 && cy0==0 (worldRect is wide enough to reach the grid corner
    // itself), in which case the walk's very first iteration already lands
    // on cell (0,0) -> id 1 naturally, and adding it here too would duplicate it.
    if (worldRect.width > 0.0f && worldRect.height > 0.0f &&
        CheckCollisionPointRec({ 0.0f, 0.0f }, worldRect) &&
        !(cx0 == 0 && cy0 == 0)) {
        out.push_back(Generate(1));
    }

    if (cx1 < cx0 || cy1 < cy0) {
        if (outSampleSpacing) *outSampleSpacing = cellSize;
        return out;
    }

    long long cellsWide  = cx1 - cx0 + 1;
    long long cellsHigh  = cy1 - cy0 + 1;
    long long totalCells = cellsWide * cellsHigh;

    long long stride = 1;
    if (maxResults > 0 && totalCells > (long long)maxResults) {
        stride = (long long)std::ceil(std::sqrt((double)totalCells / (double)maxResults));
        stride = std::max(1LL, stride);
    }
    if (outSampleSpacing) *outSampleSpacing = cellSize * (float)stride;

    out.reserve((size_t)std::min<long long>((cellsWide / stride + 1) * (cellsHigh / stride + 1), 20000));
    for (long long cy = cy0; cy <= cy1; cy += stride) {
        for (long long cx = cx0; cx <= cx1; cx += stride) {
            unsigned int id = (unsigned int)(cy * (long long)dim + cx + 1);
            if (id == 0u || id > detail::CountRef()) continue;
            StarSystem sys = Generate(id);
            if (!sys.hasStar) continue; // thinned out by the density field
            out.push_back(sys);
        }
    }
    return out;
}

// Lazy name resolution — only call this for systems actually about to be
// labeled on screen (current/selected/discovered), never for the bulk
// decorative background returned by QueryRegion().
inline std::string NameOf(unsigned int seed) { return detail::GenerateName(seed); }

// Exposes the density field itself (not just existence rolls) for callers
// that want to shade rendered points/tiles by how "deep" into the galaxy's
// structure a position is — e.g. the galactic map's widest zoom tier.
inline float Density(Vector2 galacticPos) { return detail::Density(galacticPos); }

} // namespace StarSystemRegistry
