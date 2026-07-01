#include "items/Item.h"

nlohmann::json Item::Serialize() const {
    return {
        {"defId",             defId},
        {"grade",             static_cast<int>(grade)},
        {"isMerged",          isMerged},
        {"graftedAttributes", graftedAttributes},
        {"baseStatCap",       baseStatCap},
        {"lineage",           lineage}
    };
}

Item Item::Deserialize(const nlohmann::json& j) {
    Item item;
    item.defId             = j.value("defId",             std::string{});
    item.grade             = static_cast<ModuleGrade>(j.value("grade", 0));
    item.isMerged          = j.value("isMerged",          false);
    item.graftedAttributes = j.value("graftedAttributes", std::vector<std::string>{});
    item.baseStatCap       = j.value("baseStatCap",       1.0f);
    item.lineage           = j.value("lineage",           std::vector<std::string>{});
    return item;
}
