#pragma once
#include "data/registry/ModuleRegistry.h"

inline std::optional<ModuleDef> ModuleById(const std::string& id) {
    return ModuleRegistry::ById(id);
}
