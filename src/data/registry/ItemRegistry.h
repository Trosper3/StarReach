#pragma once
#include "data/ItemDef.h"
#include <string>
#include <unordered_map>
#include <vector>

class ItemRegistry {
public:
    static void                          Init();
    static const std::vector<ItemDef>&   All();
    static const ItemDef*                ById(const std::string& id);

private:
    static std::vector<ItemDef>                   s_all;
    static std::unordered_map<std::string,size_t> s_byId;
};
