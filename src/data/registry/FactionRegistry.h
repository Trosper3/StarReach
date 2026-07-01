#pragma once
#include <string>
#include <unordered_map>
#include <vector>

struct FactionDef {
    std::string id;
    std::string displayName;
    std::string defaultRelation;  // "hostile" | "neutral" | "friendly"
    std::string loreText;
    std::vector<std::string> ranks;
};

class FactionRegistry {
public:
    static void                           Init();
    static const std::vector<FactionDef>& All();
    static const FactionDef*              ById(const std::string& id);

private:
    static std::vector<FactionDef>                  s_all;
    static std::unordered_map<std::string, size_t>  s_byId;
};
