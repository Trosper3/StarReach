#include "items/BlueprintManager.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

// ── Grade name helpers ────────────────────────────────────────────────────────

static const char* kGradeNames[7] = {
    "Common", "Uncommon", "Unique", "Remarkable", "Epic", "Legendary", "Mythic"
};

static std::string GradeToStr(ModuleGrade g) {
    int idx = static_cast<int>(g);
    return (idx >= 0 && idx < 7) ? kGradeNames[idx] : "Common";
}

static ModuleGrade StrToGrade(const std::string& s) {
    for (int i = 0; i < 7; ++i)
        if (s == kGradeNames[i]) return static_cast<ModuleGrade>(i);
    return ModuleGrade::Common;
}

// ── Extract ───────────────────────────────────────────────────────────────────

bool BlueprintManager::Extract(const Item& item, const std::string& name) {
    std::error_code ec;
    fs::create_directories(kBlueprintDir, ec);
    if (ec) return false;

    fs::path path = fs::path(kBlueprintDir) / (name + ".json");
    std::ofstream f(path);
    if (!f.is_open()) return false;

    nlohmann::json j = {
        {"name",              name},
        {"defId",             item.defId},
        {"grade",             GradeToStr(item.grade)},
        {"isMerged",          item.isMerged},
        {"baseStatCap",       item.baseStatCap},
        {"graftedAttributes", item.graftedAttributes},
        {"lineage",           item.lineage}
    };

    f << j.dump(2);
    return f.good();
}

// ── Load ──────────────────────────────────────────────────────────────────────

std::optional<Blueprint> BlueprintManager::Load(const std::string& name) {
    fs::path path = fs::path(kBlueprintDir) / (name + ".json");
    std::ifstream f(path);
    if (!f.is_open()) return std::nullopt;

    auto j = nlohmann::json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded()) return std::nullopt;

    Blueprint bp;
    bp.name              = j.value("name",      name);
    bp.defId             = j.value("defId",      std::string{});
    bp.grade             = j.value("grade",      std::string{"Common"});
    bp.isMerged          = j.value("isMerged",   false);
    bp.baseStatCap       = j.value("baseStatCap", 1.0f);
    bp.graftedAttributes = j.value("graftedAttributes", std::vector<std::string>{});
    bp.lineage           = j.value("lineage",            std::vector<std::string>{});
    return bp;
}

// ── ListAll ───────────────────────────────────────────────────────────────────

std::vector<std::string> BlueprintManager::ListAll() {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(kBlueprintDir, ec)) {
        if (ec) break;
        if (entry.path().extension() == ".json")
            names.push_back(entry.path().stem().string());
    }
    return names;
}

// ── Apply ─────────────────────────────────────────────────────────────────────

int BlueprintManager::Apply(const Blueprint& bp, Item& item) {
    item.defId    = bp.defId;
    item.grade    = StrToGrade(bp.grade);
    item.isMerged = bp.isMerged;
    item.baseStatCap = bp.baseStatCap;

    int applied = 0;
    for (const auto& attrId : bp.graftedAttributes) {
        item.graftedAttributes.push_back(attrId);
        item.lineage.push_back("blueprint:" + bp.name + ",attr:" + attrId);
        ++applied;
    }
    return applied;
}
