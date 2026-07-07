#pragma once
#include "data/registry/StarSystemRegistry.h"
#include "raylib.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

// Universe tier, one level above StarSystemRegistry: a hash-lattice,
// generate-on-demand field of galaxies, exactly the same architecture used
// for stars within a galaxy (no bulk storage, O(1) per-galaxy generation,
// budget-capped QueryRegion for the map). Only ever used for cheap per-galaxy
// metadata (position + shape, for drawing an icon) — a galaxy's actual star
// field is never generated here. That only happens when the player warps
// into it and StarSystemRegistry::Init() is called with that galaxy's seed,
// so exactly one galaxy's worth of star data ever exists in memory at a time.
namespace UniverseRegistry {

inline constexpr unsigned int kDefaultGalaxyCount = 10'000u;      // adjustable via Init()
inline constexpr float        kUniverseSpan       = 4'000'000'000.0f; // world spans [-span/2, +span/2]

struct GalaxyInfo {
    unsigned int id       = 0;
    std::string  name;
    // Passed to StarSystemRegistry::Init() when the player warps into this
    // galaxy. Derived from the universe seed, except id==1 (home galaxy),
    // which reuses the raw universe seed directly — see Generate() below.
    unsigned int seed      = 0;
    Vector2      position   = {}; // universe-space position (icon placement only)
    Vector2      cellCenter = {}; // un-jittered — see StarSystem::cellCenter for why
    // Cached from the existence-roll evaluation in Generate() — lets the
    // UniverseShape tier shade its density dots without a second Density() call,
    // same optimization StarSystem::density already does for the galaxy-shape tier.
    float density = 0.0f;
    StarSystemRegistry::GalaxyShape       shape = StarSystemRegistry::GalaxyShape::Spiral;
    StarSystemRegistry::GalaxyShapeParams shapeParams; // reused for icon rendering (aspect/rotation/arms)
    // False for lattice cells thinned out by the universe's density field
    // (see Density() below) — most callers only ever see true, since
    // QueryRegion() already filters these out before returning.
    bool exists = true;
};

// Tunable shape of the "supercluster" galaxies actually populate — without
// this, every lattice cell has a galaxy and zooming out shows a perfectly
// filled square (the lattice's own bounding box) instead of anything organic.
// A handful of overlapping soft-edged lobes combined with max() (same trick
// StarSystemRegistry uses for spiral arms) gives a lumpy, irregular blob
// instead of a single perfect circle, derived once from the universe seed.
struct UniverseShapeParams {
    static constexpr int kLobeCount = 4;
    Vector2 lobeCenter[kLobeCount] = {}; // fraction of half-span, [-1,1]
    float   lobeRadius[kLobeCount] = {}; // fraction of half-span
};

namespace detail {
inline unsigned int&        UniverseSeedRef() { static unsigned int        s = 1u; return s; }
inline unsigned int&        CountRef()        { static unsigned int        s = kDefaultGalaxyCount; return s; }
inline UniverseShapeParams& ShapeParamsRef()  { static UniverseShapeParams s{}; return s; }

inline void ComputeShapeParams(unsigned int universeSeed, UniverseShapeParams& p) {
    for (int i = 0; i < UniverseShapeParams::kLobeCount; ++i) {
        if (i == 0) {
            // Lobe 0 is always anchored at the universe origin, where the
            // home galaxy (id==1) always sits — otherwise, for most seeds,
            // the player would start out isolated in an empty void with no
            // other galaxies nearby, since the remaining lobes land wherever
            // the seed happens to roll them. The other lobes still roll
            // freely, extending the supercluster outward asymmetrically.
            p.lobeCenter[i] = { 0.0f, 0.0f };
            p.lobeRadius[i] = StarSystemRegistry::detail::Lerp(0.28f, 0.38f,
                StarSystemRegistry::detail::HashToUnit(
                    StarSystemRegistry::detail::Hash32(universeSeed, (uint32_t)i, 0x5003u)));
            continue;
        }
        float ux = StarSystemRegistry::detail::HashToUnit(
            StarSystemRegistry::detail::Hash32(universeSeed, (uint32_t)i, 0x5001u)) * 2.0f - 1.0f;
        float uy = StarSystemRegistry::detail::HashToUnit(
            StarSystemRegistry::detail::Hash32(universeSeed, (uint32_t)i, 0x5002u)) * 2.0f - 1.0f;
        // Keep lobe centers within the inner 40% of the span so their
        // falloff (below) has room to taper to 0 well before the lattice
        // edge, the same reasoning as StarSystemRegistry's edge fade.
        p.lobeCenter[i] = { ux * 0.4f, uy * 0.4f };
        p.lobeRadius[i] = StarSystemRegistry::detail::Lerp(0.20f, 0.38f,
            StarSystemRegistry::detail::HashToUnit(
                StarSystemRegistry::detail::Hash32(universeSeed, (uint32_t)i, 0x5003u)));
    }
}

// Closed-form density in [0,1], pure function of position — same
// architecture as StarSystemRegistry::Density, one level up. max() across
// lobes (rather than summing) keeps each lobe's own soft edge instead of
// blowing out to full density wherever two lobes overlap.
inline float Density(Vector2 pos) {
    const UniverseShapeParams& p = ShapeParamsRef();
    float halfSpan = kUniverseSpan * 0.5f;
    float density  = 0.0f;
    for (int i = 0; i < UniverseShapeParams::kLobeCount; ++i) {
        Vector2 lobeWorldPos = { p.lobeCenter[i].x * halfSpan, p.lobeCenter[i].y * halfSpan };
        float   dx     = pos.x - lobeWorldPos.x;
        float   dy     = pos.y - lobeWorldPos.y;
        float   rNorm  = sqrtf(dx * dx + dy * dy) / halfSpan;
        float   lobe   = 1.0f - StarSystemRegistry::detail::SmoothstepLocal(
                                     p.lobeRadius[i] * 0.6f, p.lobeRadius[i], rNorm);
        density = std::max(density, lobe);
    }
    // Forces density to exactly 0 by the time any single lobe could reach
    // the lattice's true edge, regardless of how the seed rolled lobe
    // centers/radii — same principled fix as StarSystemRegistry's edge fade.
    float rNormOrigin = sqrtf(pos.x * pos.x + pos.y * pos.y) / halfSpan;
    float edgeFade = 1.0f - StarSystemRegistry::detail::SmoothstepLocal(0.85f, 1.0f, rNormOrigin);
    return std::clamp(density * edgeFade, 0.0f, 1.0f);
}
} // namespace detail

// Must be called once per game session before any other function in this
// namespace is used. universeSeed doubles as the existing save format's
// `gameSeed` field — see Generate()'s id==1 special case for why that keeps
// existing single-galaxy saves loading identically.
inline void Init(unsigned int universeSeed, unsigned int galaxyCount = kDefaultGalaxyCount) {
    detail::UniverseSeedRef() = universeSeed == 0u ? 1u : universeSeed;
    detail::CountRef()        = galaxyCount == 0u ? 1u : galaxyCount;
    detail::ComputeShapeParams(detail::UniverseSeedRef(), detail::ShapeParamsRef());
}

inline unsigned int Count() { return detail::CountRef(); }

inline unsigned int GridDim() {
    return (unsigned int)std::ceil(std::sqrt((double)detail::CountRef()));
}

inline float CellSize() { return kUniverseSpan / (float)GridDim(); }

// O(1), no storage — computes a galaxy's seed/position/shape purely from its
// id and the current universe seed. id == 1 is special-cased as the home
// galaxy: anchored at the universe origin, and using the raw universe seed
// directly (not re-hashed) so that a save file's single `gameSeed` — which,
// before this feature existed, WAS the home galaxy's master seed — still
// reproduces bit-identical content once `currentGalaxyId` defaults to 1.
inline GalaxyInfo Generate(unsigned int id) {
    GalaxyInfo g;
    g.id = id;

    if (id == 1) {
        g.seed       = detail::UniverseSeedRef();
        g.position   = { 0.0f, 0.0f };
        g.cellCenter = { 0.0f, 0.0f };
        g.density    = detail::Density(g.position);
    } else {
        unsigned int dim = GridDim();
        unsigned int idx = id - 1;
        unsigned int cx  = idx % dim;
        unsigned int cy  = idx / dim;

        float cellSize     = CellSize();
        float originOffset = kUniverseSpan * 0.5f;
        float cellCX = (cx + 0.5f) * cellSize - originOffset;
        float cellCY = (cy + 0.5f) * cellSize - originOffset;
        g.cellCenter = { cellCX, cellCY };

        // Jitter within the cell so galaxies don't sit on a visibly perfect
        // grid — same trick as StarSystemRegistry::Generate's star jitter.
        uint32_t jh = StarSystemRegistry::detail::Hash32(detail::UniverseSeedRef(), id, 0x63A11u);
        float jx = ((jh & 0xFFFFu) / 65535.0f - 0.5f) * cellSize * 0.5f;
        float jy = (((jh >> 16) & 0xFFFFu) / 65535.0f - 0.5f) * cellSize * 0.5f;
        g.position = { cellCX + jx, cellCY + jy };

        g.seed = StarSystemRegistry::detail::Hash32(detail::UniverseSeedRef(), id, 0xF00Du);

        // Existence roll against the universe's density field — most cells
        // outside the populated lobes come up empty, which is what actually
        // produces an organic supercluster shape instead of every lattice
        // cell (i.e. the whole square) having a galaxy. id==1 (home galaxy,
        // handled in the branch above) always exists.
        uint32_t eh   = StarSystemRegistry::detail::Hash32(detail::UniverseSeedRef(), id, 0x9A11u);
        float    roll = StarSystemRegistry::detail::HashToUnit(eh);
        g.density     = detail::Density(g.position);
        g.exists      = roll < g.density;
    }

    float shapeRoll = StarSystemRegistry::detail::HashToUnit(
        StarSystemRegistry::detail::Hash32(g.seed, 0u, 0x2A2Au));
    g.shape = (shapeRoll < 0.5f) ? StarSystemRegistry::GalaxyShape::Spiral
                                 : StarSystemRegistry::GalaxyShape::Elliptical;
    StarSystemRegistry::detail::ComputeShapeParams(g.seed, g.shape, g.shapeParams);
    g.name = StarSystemRegistry::detail::GenerateName(g.seed);

    return g;
}

// Same budget-capped, strided lattice walk as StarSystemRegistry::QueryRegion
// (see its comment) — bounds the number of galaxies returned regardless of
// how large universeRect is, so this stays safe to call every frame the
// universe tier is open, at any zoom level.
// outSampleSpacing (optional): mirrors StarSystemRegistry::QueryRegion's
// param of the same name — the actual world-space distance between adjacent
// samples (stride * cellSize) for this call, letting a caller judge how
// tightly packed the returned galaxies are (e.g. to decide whether there's
// enough on-screen room to draw names without them overlapping).
inline std::vector<GalaxyInfo> QueryRegion(Rectangle universeRect, int maxResults,
                                            float* outSampleSpacing = nullptr) {
    std::vector<GalaxyInfo> out;

    unsigned int dim          = GridDim();
    float        cellSize     = CellSize();
    float        originOffset = kUniverseSpan * 0.5f;

    auto ToCell = [&](float world) -> long long {
        return (long long)std::floor((world + originOffset) / cellSize);
    };

    long long cx0 = std::max(0LL, ToCell(universeRect.x));
    long long cx1 = std::min((long long)dim - 1, ToCell(universeRect.x + universeRect.width));
    long long cy0 = std::max(0LL, ToCell(universeRect.y));
    long long cy1 = std::min((long long)dim - 1, ToCell(universeRect.y + universeRect.height));

    // Home galaxy (id==1) is special-cased in Generate() to sit at the
    // universe origin rather than in its "natural" lattice slot (idx 0 ->
    // cell 0,0, a grid corner far from the origin) — same reasoning as
    // StarSystemRegistry::QueryRegion's identical fix. The cell-index walk
    // below can never land on it, so it needs this explicit check — EXCEPT
    // when cx0==0 && cy0==0 (universeRect reaches the grid corner itself),
    // in which case the walk's first iteration already lands on cell (0,0)
    // -> id 1 naturally, and adding it here too would duplicate it.
    if (universeRect.width > 0.0f && universeRect.height > 0.0f &&
        CheckCollisionPointRec({ 0.0f, 0.0f }, universeRect) &&
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
            GalaxyInfo g = Generate(id);
            if (!g.exists) continue; // thinned out by the density field
            out.push_back(std::move(g));
        }
    }
    return out;
}

// Exposes the density field itself (not just existence rolls) for callers
// that want to shade rendered dots/tiles by how "deep" into the supercluster
// structure a position is — e.g. the Universe map's widest zoom tier. Mirrors
// StarSystemRegistry::Density.
inline float Density(Vector2 pos) { return detail::Density(pos); }

} // namespace UniverseRegistry
