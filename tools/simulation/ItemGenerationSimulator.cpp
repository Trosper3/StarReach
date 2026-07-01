// ItemGenerationSimulator — generates a large dataset of found/merged items
// and verifies the 90% performance target across all 7 grades.
//
// Compile: add this file + GradeRegistry.cpp, AttributeRegistry.cpp,
//          ProgressionRegistry.cpp, Item.cpp to a tool target (requires raylib link).
// Usage:  ItemGenerationSimulator [N] [--csv]
//   N      — items per grade to generate (default: 1000)
//   --csv  — print item-level CSV to stdout (stats go to stderr)
//
// Output:
//   Per-grade stats table showing found cap, merged cap, ratio vs 90% target.
//   Cost scaling table for hull_reinforced across all grades.
//   CSV (if --csv): grade,status,baseStatCap,graftedCount

#include "items/Item.h"
#include "items/GradeRegistry.h"
#include "items/AttributeRegistry.h"
#include "progression/ProgressionRegistry.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

static const char* kGradeNames[7] = {
    "Common", "Uncommon", "Unique", "Remarkable", "Epic", "Legendary", "Mythic"
};

struct GradeStats {
    int    total      = 0;
    int    foundCount = 0;
    int    mergCount  = 0;
    double foundSum   = 0.0;
    double mergSum    = 0.0;
};

int main(int argc, char* argv[]) {
    AttributeRegistry::Init();
    ProgressionRegistry::Init(""); // empty path → built-in defaults

    int  nPerGrade = 1000;
    bool csvMode   = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--csv") == 0) csvMode = true;
        else {
            int n = std::atoi(argv[i]);
            if (n > 0) nPerGrade = n;
        }
    }

    const auto& attrs = AttributeRegistry::All();
    if (attrs.empty()) {
        std::fprintf(stderr, "ERROR: AttributeRegistry empty — Init() failed\n");
        return 1;
    }

    GradeStats stats[7] = {};

    if (csvMode)
        std::printf("grade,status,baseStatCap,graftedCount\n");

    // Generate nPerGrade items per grade: half found, half merged.
    // Merged items cycle through attributes round-robin for variety.
    for (int g = 0; g < 7; ++g) {
        ModuleGrade grade = static_cast<ModuleGrade>(g);
        const int half = nPerGrade / 2;

        // Found items
        for (int n = 0; n < half; ++n) {
            Item item;
            item.grade = grade;
            // Found items start at baseStatCap = 1.0f (no Apply needed).

            stats[g].foundCount++;
            stats[g].foundSum += item.baseStatCap;
            stats[g].total++;

            if (csvMode)
                std::printf("%s,found,%.6f,%d\n",
                    kGradeNames[g], item.baseStatCap,
                    static_cast<int>(item.graftedAttributes.size()));
        }

        // Merged items (one attribute grafted per item, cycling attrs)
        for (int n = 0; n < half; ++n) {
            Item item;
            item.grade    = grade;
            item.isMerged = true;
            item.graftedAttributes.push_back(attrs[n % attrs.size()].id);
            GradeRegistry::Apply(item);

            stats[g].mergCount++;
            stats[g].mergSum += item.baseStatCap;
            stats[g].total++;

            if (csvMode)
                std::printf("%s,merged,%.6f,%d\n",
                    kGradeNames[g], item.baseStatCap,
                    static_cast<int>(item.graftedAttributes.size()));
        }
    }

    // Stats table
    FILE* out = csvMode ? stderr : stdout;

    std::fprintf(out, "\n=== Item Generation Stats (N=%d per grade) ===\n\n", nPerGrade);
    std::fprintf(out, "%-12s  %7s  %10s  %10s  %8s  %-8s\n",
        "Grade", "Items", "MergedCap", "FoundCap", "Ratio", "Status");
    std::fprintf(out, "%-12s  %7s  %10s  %10s  %8s  %-8s\n",
        "-----", "-----", "---------", "--------", "-----", "------");

    int failures = 0;
    for (int g = 0; g < 7; ++g) {
        double foundAvg  = stats[g].foundCount > 0 ? stats[g].foundSum / stats[g].foundCount : 0.0;
        double mergAvg   = stats[g].mergCount  > 0 ? stats[g].mergSum  / stats[g].mergCount  : 0.0;
        double ratio     = foundAvg > 0.0 ? mergAvg / foundAvg * 100.0 : 0.0;
        bool   ok        = std::fabs(ratio - 90.0) < 0.01;
        if (!ok) ++failures;

        std::fprintf(out, "%-12s  %7d  %10.4f  %10.4f  %7.2f%%  %s\n",
            kGradeNames[g], stats[g].total,
            mergAvg, foundAvg, ratio,
            ok ? "OK" : "FAIL");
    }

    // Cost scaling reference table (hull_reinforced)
    const AttributeDef* hull = AttributeRegistry::ById("hull_reinforced");
    if (hull) {
        std::fprintf(out, "\n=== Graft Cost Scaling: %s (base", hull->displayName.c_str());
        for (const auto& ing : hull->graftCost)
            std::fprintf(out, " %s×%d", ing.materialId.c_str(), ing.amount);
        std::fprintf(out, ") ===\n\n");
        std::fprintf(out, "%-12s  %8s  %s\n", "Grade", "Mult", "Scaled Cost");
        std::fprintf(out, "%-12s  %8s  %s\n", "-----", "----", "-----------");
        for (int g = 0; g < 7; ++g) {
            ModuleGrade grade = static_cast<ModuleGrade>(g);
            float mult = ProgressionRegistry::GetMultiplier(grade);
            auto  costs = ProgressionRegistry::ScaledCost(hull->graftCost, grade);
            std::fprintf(out, "%-12s  %7.2fx  ", kGradeNames[g], mult);
            for (const auto& c : costs)
                std::fprintf(out, "%s×%d  ", c.materialId.c_str(), c.amount);
            std::fprintf(out, "\n");
        }
    }

    std::fprintf(out, "\n%d grade(s) outside 90%% target.\n", failures);
    std::fprintf(out, failures == 0
        ? "PASS  90%% performance target verified across all grades.\n"
        : "FAIL  one or more grades did not meet the 90%% target.\n");

    return failures == 0 ? 0 : 1;
}
