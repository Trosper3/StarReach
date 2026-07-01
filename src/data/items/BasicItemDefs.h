#pragma once
#include "data/ItemDef.h"
#include <vector>

inline std::vector<ItemDef> AllBasicItems() {
    return {
        { "steel_plate",    "Steel Plate",    "Forged iron plate for structural assembly",
          { {"iron",5}, {"carbon",2} } },
        { "titanium_alloy", "Titanium Alloy", "High-strength alloy for advanced construction",
          { {"titanium",4}, {"iron",2} } },
        { "circuit_board",  "Circuit Board",  "Electronic control substrate",
          { {"silica",3}, {"cobalt",2} } },
        { "crystal_lens",   "Crystal Lens",   "Precision focusing element",
          { {"crystite",3}, {"silica",1} } },
        { "power_cell",     "Power Cell",     "Dense energy storage unit",
          { {"cobalt",4}, {"titanium",2} } },
        { "hull_frame",     "Hull Frame",     "Heavy-duty structural hull component",
          { {"iron",8}, {"titanium",4}, {"carbon",3} } },
        { "sensor_array",   "Sensor Array",   "Multi-spectrum detection package",
          { {"silica",4}, {"cobalt",3}, {"crystite",2} } },
        { "thruster_core",  "Thruster Core",  "Precision propulsion assembly",
          { {"titanium",6}, {"cobalt",3} } },
        { "weapons_rack",   "Weapons Rack",   "Reinforced weapon mounting frame",
          { {"iron",4}, {"cobalt",2}, {"tungsten",3} } },
        { "void_shard",     "Void Shard",     "Crystallized dark energy fragment",
          { {"voidstone",3}, {"crystite",2}, {"xenonite",1} } },
    };
}
