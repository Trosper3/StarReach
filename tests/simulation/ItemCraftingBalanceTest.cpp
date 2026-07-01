// ItemCraftingBalanceTest — validates crafting balance invariants across all 7 grades.
// Standalone test with its own main() — no external test framework required.
//
// Compile: add this file + EngineerService.cpp, GradeRegistry.cpp, AttributeRegistry.cpp,
//          ProgressionRegistry.cpp, ModuleRegistry.cpp, Item.cpp to a test target.
//          Requires raylib in the link line (pulled in by Module.h).
// Run: ./ItemCraftingBalanceTest  →  prints FAIL lines then a summary.
//
// Sections:
//   §1  GradeRegistry cap assignment          (14 checks)
//   §2  GradeRegistry AllowsPrimaryGraft      ( 7 checks)
//   §3  GradeRegistry Apply() idempotency     ( 7 checks)
//   §4  ProgressionRegistry monotonicity      ( 6 checks)
//   §5  ScaledCost at Common == base cost     (18 checks)
//   §6  ScaledCost Mythic > Common            ( 9 checks)
//   §7  Validate — success path               ( 9 checks)
//   §8  Validate — missing credits            ( 9 checks)
//   §9  Validate — missing materials          ( 9 checks)
//   §10 Validate — Mythic primary block       ( 4 checks)
//   §11 Dataset: 50 found + 50 merged × 7    (21 checks)
// Total: 113 checks

#include "items/Item.h"
#include "items/GradeRegistry.h"
#include "items/AttributeRegistry.h"
#include "services/EngineerService.h"
#include "progression/ProgressionRegistry.h"
#include "shared/entities/InventoryComponent.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>

static int s_checks   = 0;
static int s_failures = 0;

static void CHK(bool cond, const char* desc) {
    ++s_checks;
    if (!cond) {
        std::printf("FAIL  %s\n", desc);
        ++s_failures;
    }
}

static const char* kGradeNames[7] = {
    "Common", "Uncommon", "Unique", "Remarkable", "Epic", "Legendary", "Mythic"
};

// Returns an inventory stocked with the exact scaled cost for attr at grade, plus credits.
static InventoryComponent StockFor(const AttributeDef& attr, ModuleGrade grade) {
    InventoryComponent inv;
    for (const auto& ing : ProgressionRegistry::ScaledCost(attr.graftCost, grade))
        inv.AddItem(ing.materialId, ing.amount);
    inv.AddItem(EngineerService::kCreditKey, EngineerService::kBaseCreditCost);
    return inv;
}

int main() {
    AttributeRegistry::Init();
    ProgressionRegistry::Init(""); // empty path → built-in defaults

    const auto& attrs = AttributeRegistry::All();

    // ── §1: GradeRegistry cap assignment (14 checks) ─────────────────────────────
    for (int g = 0; g < 7; ++g) {
        ModuleGrade grade = static_cast<ModuleGrade>(g);

        Item found;
        found.grade = grade;
        // Found items: default baseStatCap=1.0f — Apply() not required.
        char buf[100];
        std::snprintf(buf, sizeof(buf), "§1 found at %-10s → baseStatCap=1.0", kGradeNames[g]);
        CHK(found.baseStatCap == 1.0f, buf);

        Item merged;
        merged.grade    = grade;
        merged.isMerged = true;
        GradeRegistry::Apply(merged);
        std::snprintf(buf, sizeof(buf),
            "§1 merged at %-10s → baseStatCap=%.1f (kMergedCap)",
            kGradeNames[g], GradeRegistry::kMergedCap);
        CHK(std::fabs(merged.baseStatCap - GradeRegistry::kMergedCap) < 0.001f, buf);
    }

    // ── §2: AllowsPrimaryGraft — true for Common–Legendary, false for Mythic (7 checks) ──
    for (int g = 0; g < 6; ++g) {
        char buf[80];
        std::snprintf(buf, sizeof(buf), "§2 AllowsPrimaryGraft(%s)=true", kGradeNames[g]);
        CHK(GradeRegistry::AllowsPrimaryGraft(static_cast<ModuleGrade>(g)), buf);
    }
    CHK(!GradeRegistry::AllowsPrimaryGraft(ModuleGrade::Mythic),
        "§2 AllowsPrimaryGraft(Mythic)=false");

    // ── §3: Apply() idempotency — double call does not change cap (7 checks) ──────
    for (int g = 0; g < 7; ++g) {
        Item item;
        item.grade    = static_cast<ModuleGrade>(g);
        item.isMerged = true;
        GradeRegistry::Apply(item);
        float first = item.baseStatCap;
        GradeRegistry::Apply(item);
        char buf[80];
        std::snprintf(buf, sizeof(buf),
            "§3 Apply() idempotent at %s (%.3f == %.3f)",
            kGradeNames[g], first, item.baseStatCap);
        CHK(std::fabs(item.baseStatCap - first) < 0.001f, buf);
    }

    // ── §4: ProgressionRegistry multiplier strictly monotone ascending (6 checks) ─
    for (int g = 0; g < 6; ++g) {
        float lo = ProgressionRegistry::GetMultiplier(static_cast<ModuleGrade>(g));
        float hi = ProgressionRegistry::GetMultiplier(static_cast<ModuleGrade>(g + 1));
        char buf[100];
        std::snprintf(buf, sizeof(buf),
            "§4 multiplier[%s]=%.2f < multiplier[%s]=%.2f",
            kGradeNames[g], lo, kGradeNames[g + 1], hi);
        CHK(lo < hi, buf);
    }

    // ── §5: ScaledCost at Common == raw base cost, per ingredient (18 checks) ─────
    for (const auto& attr : attrs) {
        auto scaled = ProgressionRegistry::ScaledCost(attr.graftCost, ModuleGrade::Common);
        for (size_t i = 0; i < attr.graftCost.size(); ++i) {
            char buf[120];
            std::snprintf(buf, sizeof(buf),
                "§5 ScaledCost Common: %s[%s] scaled=%d == base=%d",
                attr.id.c_str(), attr.graftCost[i].materialId.c_str(),
                scaled[i].amount, attr.graftCost[i].amount);
            CHK(scaled[i].amount == attr.graftCost[i].amount, buf);
        }
    }

    // ── §6: ScaledCost Mythic > Common for first ingredient of each attr (9 checks) ─
    for (const auto& attr : attrs) {
        if (attr.graftCost.empty()) continue;
        auto common = ProgressionRegistry::ScaledCost(attr.graftCost, ModuleGrade::Common);
        auto mythic = ProgressionRegistry::ScaledCost(attr.graftCost, ModuleGrade::Mythic);
        char buf[120];
        std::snprintf(buf, sizeof(buf),
            "§6 ScaledCost Mythic>Common for %s[%s]: %d > %d",
            attr.id.c_str(), attr.graftCost[0].materialId.c_str(),
            mythic[0].amount, common[0].amount);
        CHK(mythic[0].amount > common[0].amount, buf);
    }

    // ── §7: Validate — success with exact inventory at Common (9 checks) ──────────
    {
        Item tgt;
        tgt.grade = ModuleGrade::Common;
        std::vector<Item> cargo;
        for (const auto& attr : attrs) {
            InventoryComponent inv = StockFor(attr, ModuleGrade::Common);
            GraftRequest req{ tgt, attr.id, cargo, 0, inv };
            char buf[120];
            std::snprintf(buf, sizeof(buf),
                "§7 Validate(%s, Common, full inventory) == Success", attr.id.c_str());
            CHK(EngineerService::Validate(req) == GraftResult::Success, buf);
        }
    }

    // ── §8: Validate — missing credits (9 checks) ────────────────────────────────
    {
        Item tgt;
        tgt.grade = ModuleGrade::Common;
        std::vector<Item> cargo;
        for (const auto& attr : attrs) {
            InventoryComponent inv = StockFor(attr, ModuleGrade::Common);
            inv.RemoveMultiple(EngineerService::kCreditKey, EngineerService::kBaseCreditCost);
            GraftRequest req{ tgt, attr.id, cargo, 0, inv };
            char buf[120];
            std::snprintf(buf, sizeof(buf),
                "§8 Validate(%s) == ErrorInsufficientCredits (credits removed)", attr.id.c_str());
            CHK(EngineerService::Validate(req) == GraftResult::ErrorInsufficientCredits, buf);
        }
    }

    // ── §9: Validate — missing materials (9 checks) ──────────────────────────────
    {
        Item tgt;
        tgt.grade = ModuleGrade::Common;
        std::vector<Item> cargo;
        for (const auto& attr : attrs) {
            InventoryComponent inv;
            inv.AddItem(EngineerService::kCreditKey, EngineerService::kBaseCreditCost);
            // No materials → fails on first ingredient check.
            GraftRequest req{ tgt, attr.id, cargo, 0, inv };
            char buf[120];
            std::snprintf(buf, sizeof(buf),
                "§9 Validate(%s) == ErrorInsufficientMaterials (no materials)", attr.id.c_str());
            CHK(EngineerService::Validate(req) == GraftResult::ErrorInsufficientMaterials, buf);
        }
    }

    // ── §10: Validate — Mythic primary block (4 checks, one per primary attr) ─────
    {
        Item mythicTgt;
        mythicTgt.grade = ModuleGrade::Mythic;
        std::vector<Item> cargo;
        for (const auto& attr : attrs) {
            if (!attr.isPrimary) continue;
            // Stock generously — the grade block fires before material checks.
            InventoryComponent inv = StockFor(attr, ModuleGrade::Mythic);
            GraftRequest req{ mythicTgt, attr.id, cargo, 0, inv };
            char buf[120];
            std::snprintf(buf, sizeof(buf),
                "§10 Validate(%s, Mythic) == ErrorMythicPrimaryBlocked", attr.id.c_str());
            CHK(EngineerService::Validate(req) == GraftResult::ErrorMythicPrimaryBlocked, buf);
        }
    }

    // ── §11: Dataset — 50 found + 50 merged per grade, verify caps (21 checks) ────
    {
        static const int kPerGrade = 50;
        int foundCapFail  [7] = {};
        int mergedCapFail [7] = {};
        int mergedLtFail  [7] = {};

        for (int g = 0; g < 7; ++g) {
            ModuleGrade grade = static_cast<ModuleGrade>(g);
            for (int n = 0; n < kPerGrade; ++n) {
                Item found;
                found.grade = grade;
                if (std::fabs(found.baseStatCap - 1.0f) > 0.001f) ++foundCapFail[g];

                Item merged;
                merged.grade    = grade;
                merged.isMerged = true;
                GradeRegistry::Apply(merged);
                if (std::fabs(merged.baseStatCap - GradeRegistry::kMergedCap) > 0.001f) ++mergedCapFail[g];
                if (merged.baseStatCap >= found.baseStatCap) ++mergedLtFail[g];
            }
        }

        for (int g = 0; g < 7; ++g) {
            char buf[120];
            std::snprintf(buf, sizeof(buf),
                "§11 dataset: found caps=1.0 at %-10s (%d/%d fail)",
                kGradeNames[g], foundCapFail[g], kPerGrade);
            CHK(foundCapFail[g] == 0, buf);

            std::snprintf(buf, sizeof(buf),
                "§11 dataset: merged caps=%.1f at %-10s (%d/%d fail)",
                GradeRegistry::kMergedCap, kGradeNames[g], mergedCapFail[g], kPerGrade);
            CHK(mergedCapFail[g] == 0, buf);

            std::snprintf(buf, sizeof(buf),
                "§11 dataset: merged<found at %-10s (%d/%d fail)",
                kGradeNames[g], mergedLtFail[g], kPerGrade);
            CHK(mergedLtFail[g] == 0, buf);
        }
    }

    // ── Result ────────────────────────────────────────────────────────────────────
    std::printf("\n%d checks run — ", s_checks);
    if (s_failures == 0)
        std::printf("PASS  all %d crafting balance checks correct\n", s_checks);
    else
        std::printf("FAIL  %d check(s) failed\n", s_failures);

    return s_failures == 0 ? 0 : 1;
}
