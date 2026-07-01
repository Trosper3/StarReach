#pragma once
#include "items/Item.h"
#include "items/AttributeRegistry.h"
#include "items/GradeRegistry.h"
#include "shared/entities/InventoryComponent.h"
#include "systems/diplomacy/DiplomaticRegistry.h"
#include <cstddef>
#include <vector>

enum class GraftResult : uint8_t {
    Success,
    SuboptimalGraft,                    // graft completed but with stat degradation (kSuboptimalCap)
    ErrorInvalidAttribute,              // attributeId not found in AttributeRegistry
    ErrorMythicPrimaryBlocked,          // target is Mythic, attribute isPrimary
    ErrorInsufficientMaterials,         // inventory lacks required graftCost materials
    ErrorInsufficientCredits,           // inventory lacks kBaseCreditCost credits
    ErrorSourceTypeMismatch,            // source and target ModuleType differ
    ErrorHostileFaction,                // station owner is Hostile — service denied entirely
    ErrorRequiresExperimentalStanding,  // grade is Legendary/Mythic but relation is not Friendly
};

// Engineer and station context for failure-chance calculation.
// Default values (tier 5, skill 1.0) yield 0% failure — safe for callers that
// haven't wired up EngineerComponent yet (Task 16.02).
struct MasteryParams {
    int   stationTier   = 5;     // 1–5: higher tier reduces failure risk
    float engineerSkill = 1.0f;  // 0.0–1.0: engineer proficiency
    float roll          = -1.0f; // [0,1] to override rand() in tests; -1 = use rand()
};

// All context for one grafting operation passed as a single struct.
struct GraftRequest {
    Item&               target;       // item receiving the attribute
    std::string         attributeId;  // which AttributeDef to graft
    std::vector<Item>&  cargo;        // player's item cargo (source consumed from here)
    size_t              sourceIndex;  // index into cargo of the item being consumed
    InventoryComponent& inventory;    // material + credit wallet
};

// Validates and executes attribute graft transactions.
class EngineerService {
public:
    // Checks attribute validity, Mythic grade rule, material costs, and credit balance.
    // Pure — does not mutate any state.
    static GraftResult Validate(const GraftRequest& req);

    // Runs Validate() then, on success:
    //   deducts materials + credits, consumes source from cargo, grafts attribute.
    // Rolls failure chance from MasteryParams:
    //   Success       → baseStatCap = kMergedCap (0.9)
    //   SuboptimalGraft → baseStatCap = kSuboptimalCap (0.75), lineage marked
    static GraftResult Execute(GraftRequest& req,
                               const MasteryParams& mastery = {});

    // Returns failure probability [0.0, 1.0] for the given grade and mastery.
    // Common = 0%, Mythic base = 80%, reduced by stationTier × engineerSkill.
    static float FailureChance(ModuleGrade grade, const MasteryParams& mastery);

    // Gate check based on diplomatic standing.
    // Call BEFORE Validate/Execute. Hostile → ErrorHostileFaction.
    // Legendary/Mythic require Friendly ("Experimental Standing"); Neutral → ErrorRequiresExperimentalStanding.
    static GraftResult CheckAccess(Relation standing, ModuleGrade grade);

    static constexpr int         kBaseCreditCost = 100;
    static constexpr const char* kCreditKey      = "credits";
    static constexpr float       kSuboptimalCap  = 0.75f;
};
