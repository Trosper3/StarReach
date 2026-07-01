#include "ui/controllers/StationMenuController.h"

bool StationMenuController::TryInstallModule(
        LoadoutComponent&                          loadout,
        InventoryComponent&                        inventory,
        int                                        slotIndex,
        const ModuleDef&                           mod,
        const std::unordered_map<std::string,int>& buildCost) {

    // Atomic check: all cost entries must be satisfied before any deduction.
    for (const auto& [id, qty] : buildCost)
        if (!inventory.HasMultiple(id, qty))
            return false;

    for (const auto& [id, qty] : buildCost)
        inventory.RemoveMultiple(id, qty);

    return loadout.Equip(slotIndex, mod);
}
