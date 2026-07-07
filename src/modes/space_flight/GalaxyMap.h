#pragma once
#include "core/StarSystem.h"
#include "data/registry/UniverseRegistry.h"
#include "ui/SavePicker.h"
#include "ui/SaveWriter.h"
#include "raylib.h"
#include <string>
#include <vector>

// Continuous-zoom navigation map: one camera (right-drag pan, wheel zoom)
// spans everything from in-system planet/station detail out to the whole
// galaxy. Replaces the old SystemMap + GalacticMap pair, which were two
// separate screens with a button hop between them — here it's just the
// same map at a different zoom level, so scrolling out from your system
// smoothly carries you into the galaxy view instead of switching screens.
enum class MapAction { None, Close, GoMainMenu, SaveToFile, LoadGame,
    WarpTo, WarpToSystem, WarpToGalaxy, OpenModules, OpenStorage, OpenEscorts, OpenRanks };

// A planet/station in the current system (System tier).
struct MapBlip {
    unsigned int id;
    Vector2      worldPos;
    float        radius;
    bool         isPlanet;
    bool         discovered;
};

// A star system eligible for interaction at Galaxy tier: the current
// system, a discovered system in view, or an undiscovered system inside
// hyperdrive range. The much larger decorative background field (Galaxy
// Shape tier) is never materialized as individual interactive entries.
struct GalaxyBlip {
    unsigned int id;
    std::string  name; // empty for undiscovered blips (never displayed)
    Vector2      galacticPos;
    Vector2      cellCenter; // un-jittered — see StarSystem::cellCenter
    bool         discovered;
    bool         isCurrent;
};

struct SystemMapData {
    // System tier
    Vector2              playerPos       = {};
    float                hyperdriveRange = 0.0f;
    int                  wingmanCount    = 0;
    std::vector<MapBlip> blips;
    bool                 hasSun   = false; // sun always sits at local (0,0)
    Color                sunCore  = WHITE;
    Color                sunGlow  = WHITE;

    // Galaxy / Galaxy-shape tiers
    unsigned int              currentSystemId  = 0;
    Vector2                   currentSystemPos = {};
    std::vector<unsigned int> discoveredSystemIds;
    // Fog-of-war reveal radius for undiscovered systems at the Galaxy tier —
    // independent of hyperdriveRange (which only gates whether a visible
    // target is warpable, not whether it's visible at all). 0 = no Sensor
    // Array-line aux module equipped, so nothing undiscovered is shown at all
    // beyond the current system. See GalaxyMap.cpp's BuildInteractiveBlips/
    // BuildBackgroundDots.
    float mapSensorRange = 0.0f;
    // ModuleGrade+1 (Common=1..Mythic=7) of the aux module providing
    // mapSensorRange; 0 with none equipped. Gates how much intel beyond
    // existence/distance a selected undiscovered system's popup reveals —
    // see GalaxyMap.cpp's PreviewSensorIntel.
    int mapSensorTier = 0;
    // The player's own faction — needed to resolve DiplomaticRegistry::Get()
    // for the faction/relation overlay on controlled discovered systems (see
    // GalaxyMap.cpp's Draw()). Defaults to Republic, matching kPlayerFaction's
    // own default in SpaceFlight.cpp.
    Faction playerFaction = Faction::Republic;

    // Universe tier. Defaults to 1 (the home galaxy) — callers that don't
    // yet track multiple galaxies get correct behavior for free, since
    // UniverseRegistry::Generate(1) always resolves to the same galaxy
    // StarSystemRegistry already generates from the plain master seed.
    unsigned int              currentGalaxyId = 1;
    std::vector<unsigned int> visitedGalaxyIds; // every galaxy ever warped into
};

class GalaxyMap {
public:
    bool isOpen = false;

    void Open();
    void Close();
    void Toggle() { isOpen ? Close() : Open(); }

    void AckSave() { _saveFeedbackTimer = 2.8f; }
    const std::string& LoadFilename()    const { return _savePicker.SelectedFile(); }
    const std::string& SavePath()        const { return _saveWriter.TargetPath();   }
    const std::string& SaveDisplayName() const { return _saveWriter.DisplayName();  }
    bool IsPickerOpen() const { return _savePicker.IsOpen() || _saveWriter.IsOpen(); }

    void         SetMapData(const SystemMapData& data) { _mapData = data; }
    Vector2      WarpTarget()        const { return _warpTarget; }        // System tier: in-system position
    unsigned int WarpTargetId()      const { return _warpTargetId; }      // Galaxy tier: target system id
    unsigned int WarpTargetGalaxyId() const { return _warpTargetGalaxyId; } // Universe tier: target galaxy id
    // Galaxy tier: ordered hop ids from the current system to WarpTargetId(),
    // excluding the current system itself (so chain.back() == WarpTargetId()).
    // Size 1 = an ordinary direct warp; size > 1 = a beacon chain through
    // already-discovered waypoints (see ComputeChainPath).
    const std::vector<unsigned int>& WarpChain() const { return _warpChain; }

    MapAction Update(float dt);
    void      Draw() const;

private:
    // Universe sits one level above GalaxyShape: zooming out past the point
    // where the current galaxy fills the screen shrinks it to a single icon
    // among a field of other galaxies (see _atUniverse below for why this
    // uses an independent camera rather than extending the existing one).
    // UniverseShape is the universe-camera's own outermost tier, mirroring
    // GalaxyShape: zooming out past the point where individual galaxy icons
    // would be smaller than a pixel apart, they thin into a density-dot field
    // showing the supercluster's overall shape instead.
    enum class Tier { System, Galaxy, GalaxyShape, Universe, UniverseShape };

    struct MapProjection {
        float cx, cy;       // world-space camera center
        float mapCX, mapCY; // screen-space center of the map panel
        float scale;        // screen px per world unit

        Vector2 Project(Vector2 wp) const {
            return { mapCX + (wp.x - cx) * scale,
                     mapCY + (wp.y - cy) * scale };
        }
        Vector2 Unproject(Vector2 sp) const {
            return { cx + (sp.x - mapCX) / scale,
                     cy + (sp.y - mapCY) / scale };
        }
    };

    // ── Camera (pan/zoom) — spans the full System -> Galaxy Shape range ──────
    Vector2 _camCenter      = {};
    float   _camScale       = 1.0f;
    bool    _dragging       = false;
    Vector2 _dragStartMouse = {};
    Vector2 _dragStartCam   = {};
    // Open() can't compute _camCenter itself: the caller feeds fresh
    // SystemMapData via SetMapData() *after* calling Open() each frame (see
    // SpaceFlight.cpp), so _mapData is still whatever was left over from the
    // last time the map was open at that point. Centering there used the
    // stale currentSystemPos/playerPos — invisible while the player could
    // only ever open the map inside the home system (which sits at the same
    // {0,0} either way), but after warping to any other system the camera
    // would center on the system just left, off in a completely different
    // part of the galaxy from where the fresh blips actually render, making
    // the System tier look empty. Deferred: Open() just raises this flag,
    // and Update() does the actual centering on its first call afterward,
    // once that frame's SetMapData() has already run.
    bool    _needsRecenter  = false;

    // ── Universe-tier camera — independent coordinate space ──────────────────
    // Additively combining in-galaxy coordinates (need ~1-unit precision,
    // already spans +/-40,000,000) with universe-scale offsets in one float32
    // would blow out precision, so the Universe tier gets its own camera
    // instead of extending _camCenter/_camScale. Crossing the boundary is a
    // discrete flip (see HandleCameraInput/HandleUniverseCameraInput), not a
    // continuous transform — but since it happens exactly when the current
    // galaxy has shrunk to a single icon, and _camCenter/_camScale are left
    // untouched while _atUniverse is true, zooming back in snaps to exactly
    // where the in-galaxy view was left, reading as seamless to the player.
    bool    _atUniverse       = false;
    Vector2 _universeCamCenter = {};
    float   _universeCamScale  = 1.0f;

    void HandleUniverseCameraInput(float dt, const Rectangle& mapRect);
    void EnterUniverseTier(const Rectangle& mapRect);

    float         _time              = 0.0f;
    float         _saveFeedbackTimer = 0.0f;
    int           _selectedBlip      = -1; // System tier: index into _mapData.blips
    unsigned int  _selectedSystemId  = 0;  // Galaxy tier: selected star system id
    unsigned int  _selectedGalaxyId  = 0;  // Universe tier: selected galaxy id
    Vector2       _warpTarget        = {};
    unsigned int  _warpTargetId      = 0;
    unsigned int  _warpTargetGalaxyId = 0;
    std::vector<unsigned int> _warpChain; // see WarpChain()
    SystemMapData _mapData;
    SavePicker    _savePicker;
    SaveWriter    _saveWriter;

    MapProjection ComputeProjection(const Rectangle& mapRect) const;
    MapProjection ComputeUniverseProjection(const Rectangle& mapRect) const;
    Rectangle     VisibleWorldRect(const Rectangle& mapRect) const;
    Rectangle     VisibleUniverseRect(const Rectangle& mapRect) const;
    Tier          CurrentTier(const Rectangle& mapRect) const;
    void          HandleCameraInput(float dt, const Rectangle& mapRect);

    std::vector<GalaxyBlip> BuildInteractiveBlips(const Rectangle& mapRect) const;

    // Beacon-chaining: is targetId reachable at all — directly, or by hopping
    // through a chain of already-discovered systems each within hyperdrive
    // range of the next? Graph nodes are the current system plus every
    // discovered system; targetId itself only ever appears as the final hop
    // (it need not be discovered — same rule the direct-warp button already
    // applies to newly-discovered systems). BFS minimizes hop count, not
    // total distance. Returns the ordered hop ids (current system excluded,
    // targetId included as the last element), or empty if unreachable even
    // via chaining. A direct in-range target yields a 1-element result.
    std::vector<unsigned int> ComputeChainPath(unsigned int targetId) const;

    // Draws whatever sensor-tier intel _mapData.mapSensorTier unlocks for an
    // undiscovered selected system (star class, planet estimate, occupancy,
    // controlling faction — see GalaxyMap.cpp's PreviewSensorIntel), centered
    // on centerX starting at startY, each line stacking downward. Returns the
    // Y just past the last line drawn (== startY if tier 1 reveals nothing
    // beyond what's already shown). draw=false measures without drawing —
    // see ComputeInfoBlockEnd for why that matters.
    float DrawSensorIntel(const StarSystem& sys, float centerX, float startY, bool draw = true) const;

    // Single source of truth for how far down a selected system's info block
    // (sensor intel if undiscovered, faction/relation line if discovered and
    // controlled, nothing otherwise) extends — used both to actually draw it
    // (Draw(), draw=true) and to hit-test the warp/chain button that gets
    // pushed below it (Update(), draw=false). These two call sites used to
    // independently duplicate this math; they drifted apart once sensor tiers
    // started reliably revealing multiple intel lines, leaving the visible
    // warp button unclickable (it had moved down; the hit-test rect hadn't).
    // Route both through here so that can't happen again.
    float ComputeInfoBlockEnd(const StarSystem& sys, bool discovered, float centerX, float startY, bool draw) const;
    // outWorldTileSize: actual world-space spacing between adjacent samples
    // (see StarSystemRegistry::QueryRegion) — lets the caller size a tile to
    // match, so surviving cells tile edge-to-edge instead of leaving gaps.
    // Returns full StarSystem records (not just positions) so callers can
    // choose cellCenter over galacticPos when tiling — see StarSystem.h.
    std::vector<StarSystem> BuildBackgroundDots(const Rectangle& mapRect,
                                                 const std::vector<GalaxyBlip>& interactive,
                                                 float* outWorldTileSize = nullptr) const;

    // Galaxy/Galaxy-Shape tiers re-walk up to kShapeDrawBudget grid cells
    // (each costing a density evaluation) inside Build{Interactive,Background}
    // above. Recomputing that every single frame — including frames where the
    // player hasn't touched pan/zoom at all — is pure waste, so Draw() calls
    // this first and it only redoes the work when camera/viewport actually
    // changed since the last call.
    void RefreshMapCache(const Rectangle& mapRect, Tier tier) const;

    mutable bool                   _cacheValid     = false;
    mutable Tier                   _cacheTier      = Tier::System;
    mutable Vector2                _cacheCamCenter = {};
    mutable float                  _cacheCamScale  = 0.0f;
    mutable Rectangle              _cacheMapRect   = {};
    mutable std::vector<GalaxyBlip>  _cacheInteractive;
    mutable std::vector<StarSystem>  _cacheBackground;
    mutable float                    _cacheWorldTileSize = 0.0f;
    // Universe tier's cached page — same budget-capped QueryRegion pattern,
    // refreshed by RefreshMapCache only when the universe camera moves.
    mutable std::vector<UniverseRegistry::GalaxyInfo> _cacheGalaxies;
    mutable float                    _cacheUniverseTileSize = 0.0f;
};
