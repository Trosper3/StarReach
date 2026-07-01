#pragma once
#include "shared/entities/LoadoutComponent.h"
#include "shared/entities/InventoryComponent.h"
#include "core/Module.h"
#include <string>
#include <unordered_map>

// Controller for the ECS station module menu.
// Intercepts install requests, validates build costs against inventory,
// deducts on success, and delegates equip logic to LoadoutComponent.
class StationMenuController {
public:
    // Returns true if the module was successfully installed.
    // Checks ALL cost entries before deducting anything (atomic check-then-apply).
    static bool TryInstallModule(LoadoutComponent&                          loadout,
                                 InventoryComponent&                        inventory,
                                 int                                        slotIndex,
                                 const ModuleDef&                           mod,
                                 const std::unordered_map<std::string,int>& buildCost);
};
