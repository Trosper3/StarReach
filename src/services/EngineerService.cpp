#include "services/EngineerService.h"
#include "data/registry/ModuleRegistry.h"
#include "progression/ProgressionRegistry.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

// ── CheckAccess ───────────────────────────────────────────────────────────────
// Hostile standing → service locked entirely.
// Legendary and Mythic require Friendly ("Experimental Standing");
// Neutral players are blocked from those tiers.

GraftResult EngineerService::CheckAccess(Relation standing, ModuleGrade grade) {
    if (standing == Relation::Hostile)
        return GraftResult::ErrorHostileFaction;

    bool isHighEnd = (grade == ModuleGrade::Legendary || grade == ModuleGrade::Mythic);
    if (isHighEnd && standing != Relation::Friendly)
        return GraftResult::ErrorRequiresExperimentalStanding;

    return GraftResult::Success;
}

// ── FailureChance ─────────────────────────────────────────────────────────────
// Base failure rates per grade (insufficient mastery = full risk):
//   Common=0%, Uncommon=5%, Unique=10%, Remarkable=20%, Epic=35%, Legendary=55%, Mythic=80%
// skillFactor = clamp(engineerSkill * stationTier / kMaxTier, 0, 1)
// effectiveChance = baseChance * (1 − skillFactor)
// At max mastery (skill=1.0, tier=5) every grade rolls 0% — no failure.

float EngineerService::FailureChance(ModuleGrade grade, const MasteryParams& m) {
    static const float kBase[7] = { 0.00f, 0.05f, 0.10f, 0.20f, 0.35f, 0.55f, 0.80f };
    int idx = static_cast<int>(grade);
    if (idx < 0 || idx >= 7) idx = 0;

    constexpr float kMaxTier  = 5.0f;
    float skillFactor = std::min(1.0f, (m.engineerSkill * static_cast<float>(m.stationTier)) / kMaxTier);
    return kBase[idx] * (1.0f - skillFactor);
}

// ── Internal graft application ────────────────────────────────────────────────
// Applies the attribute, sets isMerged, writes appropriate baseStatCap, records lineage.

static void ApplyGraft(Item& target, const std::string& attrId,
                       const std::string& sourceDefId, bool suboptimal)
{
    target.isMerged = true;
    target.graftedAttributes.push_back(attrId);

    if (suboptimal) {
        target.baseStatCap = EngineerService::kSuboptimalCap;
        target.lineage.push_back(
            "source:" + sourceDefId + ",attr:" + attrId + ",quality:suboptimal");
    } else {
        GradeRegistry::Apply(target); // sets baseStatCap = kMergedCap (0.9)
        target.lineage.push_back("source:" + sourceDefId + ",attr:" + attrId);
    }
}

// ── Validate ──────────────────────────────────────────────────────────────────

GraftResult EngineerService::Validate(const GraftRequest& req) {
    const AttributeDef* attr = AttributeRegistry::ById(req.attributeId);
    if (!attr)
        return GraftResult::ErrorInvalidAttribute;

    if (attr->isPrimary && !GradeRegistry::AllowsPrimaryGraft(req.target.grade))
        return GraftResult::ErrorMythicPrimaryBlocked;

    for (const auto& ing : ProgressionRegistry::ScaledCost(attr->graftCost, req.target.grade))
        if (!req.inventory.HasMultiple(ing.materialId, ing.amount))
            return GraftResult::ErrorInsufficientMaterials;

    if (!req.inventory.HasMultiple(kCreditKey, kBaseCreditCost))
        return GraftResult::ErrorInsufficientCredits;

    return GraftResult::Success;
}

// ── Execute ───────────────────────────────────────────────────────────────────

GraftResult EngineerService::Execute(GraftRequest& req, const MasteryParams& mastery) {
    // Same-type check: source and target must share ModuleType.
    if (req.sourceIndex < req.cargo.size()) {
        auto srcDef = ModuleRegistry::ById(req.cargo[req.sourceIndex].defId);
        auto tgtDef = ModuleRegistry::ById(req.target.defId);
        if (srcDef && tgtDef && srcDef->type != tgtDef->type)
            return GraftResult::ErrorSourceTypeMismatch;
    }

    GraftResult valid = Validate(req);
    if (valid != GraftResult::Success)
        return valid;

    const AttributeDef* attr = AttributeRegistry::ById(req.attributeId);

    // Deduct materials + credits (consumed regardless of outcome — System Strain).
    for (const auto& ing : ProgressionRegistry::ScaledCost(attr->graftCost, req.target.grade))
        req.inventory.RemoveMultiple(ing.materialId, ing.amount);
    req.inventory.RemoveMultiple(kCreditKey, kBaseCreditCost);

    // Capture source defId before consuming.
    std::string sourceDefId;
    if (req.sourceIndex < req.cargo.size())
        sourceDefId = req.cargo[req.sourceIndex].defId;

    // Consume source item (swap-and-pop: O(1)).
    if (req.sourceIndex < req.cargo.size()) {
        std::swap(req.cargo[req.sourceIndex], req.cargo.back());
        req.cargo.pop_back();
    }

    // Failure roll: use mastery.roll override when >= 0, otherwise rand().
    float chance = FailureChance(req.target.grade, mastery);
    float roll   = mastery.roll >= 0.0f
                   ? mastery.roll
                   : static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    bool suboptimal = (roll < chance);

    ApplyGraft(req.target, req.attributeId, sourceDefId, suboptimal);
    return suboptimal ? GraftResult::SuboptimalGraft : GraftResult::Success;
}
