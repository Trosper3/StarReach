#include "analytics/CraftingTelemetry.h"
#include <fstream>
#include <cstdio>

std::vector<TelemetryEvent> CraftingTelemetry::s_events;

// ── Helpers ───────────────────────────────────────────────────────────────────

const char* CraftingTelemetry::GradeStr(ModuleGrade g) {
    switch (g) {
    case ModuleGrade::Common:     return "Common";
    case ModuleGrade::Uncommon:   return "Uncommon";
    case ModuleGrade::Unique:     return "Unique";
    case ModuleGrade::Remarkable: return "Remarkable";
    case ModuleGrade::Epic:       return "Epic";
    case ModuleGrade::Legendary:  return "Legendary";
    case ModuleGrade::Mythic:     return "Mythic";
    default:                      return "Unknown";
    }
}

const char* CraftingTelemetry::ResultStr(GraftResult r) {
    switch (r) {
    case GraftResult::Success:                    return "Success";
    case GraftResult::SuboptimalGraft:            return "SuboptimalGraft";
    case GraftResult::ErrorInvalidAttribute:      return "Error:InvalidAttribute";
    case GraftResult::ErrorMythicPrimaryBlocked:  return "Error:MythicPrimaryBlocked";
    case GraftResult::ErrorInsufficientMaterials: return "Error:InsufficientMaterials";
    case GraftResult::ErrorInsufficientCredits:   return "Error:InsufficientCredits";
    case GraftResult::ErrorSourceTypeMismatch:    return "Error:SourceTypeMismatch";
    default:                                      return "Error:Unknown";
    }
}

// ── Record ────────────────────────────────────────────────────────────────────

void CraftingTelemetry::Record(const Item&          before,
                               const Item&          after,
                               const std::string&   attributeId,
                               GraftResult          result,
                               const MasteryParams& mastery)
{
    TelemetryEvent ev;
    ev.itemDefId      = before.defId;
    ev.grade          = GradeStr(before.grade);
    ev.attributeId    = attributeId;
    ev.result         = ResultStr(result);
    ev.capBefore      = before.baseStatCap;
    ev.capAfter       = after.baseStatCap;
    ev.capDelta       = after.baseStatCap - before.baseStatCap;
    ev.attrsBefore    = static_cast<int>(before.graftedAttributes.size());
    ev.attrsAfter     = static_cast<int>(after.graftedAttributes.size());
    ev.failureChance  = EngineerService::FailureChance(before.grade, mastery);
    ev.stationTier    = mastery.stationTier;
    ev.engineerSkill  = mastery.engineerSkill;
    s_events.push_back(std::move(ev));
}

// ── ExportCSV ─────────────────────────────────────────────────────────────────

bool CraftingTelemetry::ExportCSV(const std::string& path) {
    std::ofstream f(path);
    if (!f.is_open()) return false;

    // Header
    f << "itemDefId,grade,attributeId,result,"
         "capBefore,capAfter,capDelta,"
         "attrsBefore,attrsAfter,"
         "failureChance,stationTier,engineerSkill\n";

    char buf[256];
    for (const TelemetryEvent& ev : s_events) {
        std::snprintf(buf, sizeof(buf),
            "%s,%s,%s,%s,"
            "%.4f,%.4f,%.4f,"
            "%d,%d,"
            "%.4f,%d,%.4f\n",
            ev.itemDefId.c_str(), ev.grade.c_str(),
            ev.attributeId.c_str(), ev.result.c_str(),
            ev.capBefore, ev.capAfter, ev.capDelta,
            ev.attrsBefore, ev.attrsAfter,
            ev.failureChance, ev.stationTier, ev.engineerSkill);
        f << buf;
    }

    return f.good();
}

// ── Clear / Count ─────────────────────────────────────────────────────────────

void CraftingTelemetry::Clear()      { s_events.clear(); }
int  CraftingTelemetry::EventCount() { return static_cast<int>(s_events.size()); }
