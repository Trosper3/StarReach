#pragma once
#include "data/MaterialDefs.h"
#include <string>
#include <unordered_map>
#include <vector>

class MaterialRegistry {
public:
    static void                           Init();
    static const std::vector<MatDef>&     All();
    static const MatDef*                  ById(const std::string& id);

private:
    static std::vector<MatDef>                     s_all;
    static std::unordered_map<std::string, size_t> s_byId;
};
