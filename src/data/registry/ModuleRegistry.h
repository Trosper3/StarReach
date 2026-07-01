#pragma once
#include "core/Module.h"
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class ModuleRegistry {
public:
    static void                           Init();
    static const std::vector<ModuleDef>&  All();
    static std::optional<ModuleDef>       ById(const std::string& id);
    static std::vector<ModuleDef>         ByType(ModuleType type);
    static std::vector<ModuleDef>         ByTypeAndGrade(ModuleType type, ModuleGrade grade);
    static ModuleDef                      Random(ModuleType type, ModuleGrade grade);
    static ModuleDef                      RandomDrop(ModuleGrade grade);
    static ModuleDef                      RandomDrop();
    static ModuleGrade                    RollGrade();

private:
    static std::vector<ModuleDef>                  s_all;
    static std::unordered_map<std::string, size_t> s_byId;
};
