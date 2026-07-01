#pragma once
#include "items/Item.h"
#include "services/EngineerService.h"
#include <string>
#include <vector>

// One recorded graft attempt: before/after item state + mastery context.
struct TelemetryEvent {
    std::string itemDefId;
    std::string grade;          // human-readable grade name
    std::string attributeId;
    std::string result;         // "Success", "SuboptimalGraft", "Error:..."
    float       capBefore       = 0.f;
    float       capAfter        = 0.f;
    float       capDelta        = 0.f;  // capAfter - capBefore
    int         attrsBefore     = 0;
    int         attrsAfter      = 0;
    float       failureChance   = 0.f;  // chance at time of graft
    int         stationTier     = 0;
    float       engineerSkill   = 0.f;
};

// Append-only log of graft events.  ExportCSV() writes the full log for offline analysis.
// Intended use:
//   Item snapshot = req.target;        // copy before-state
//   GraftResult r = EngineerService::Execute(req, mastery);
//   CraftingTelemetry::Record(snapshot, req.target, attrId, r, mastery);
class CraftingTelemetry {
public:
    // Records one graft event. `before` is a copy taken before Execute(); `after` is the
    // mutated target. Computes capDelta and failureChance internally.
    static void Record(const Item&         before,
                       const Item&         after,
                       const std::string&  attributeId,
                       GraftResult         result,
                       const MasteryParams& mastery = {});

    // Writes all recorded events to path as a CSV file.
    // Header row + one data row per event. Returns false if the write fails.
    static bool ExportCSV(const std::string& path);

    // Removes all recorded events.
    static void Clear();

    static int  EventCount();

private:
    static std::vector<TelemetryEvent> s_events;

    static const char* ResultStr(GraftResult r);
    static const char* GradeStr (ModuleGrade g);
};
