#include "InventoryManager.h"
#include "EventBus.h"

void InventoryManager::AddItem(const std::string& itemId, int amount) {
    _items[itemId] += amount;
    EventBus::Get().Emit("InventoryChanged");
}

bool InventoryManager::RemoveItem(const std::string& itemId, int amount) {
    if (!HasItem(itemId, amount)) return false;
    _items[itemId] -= amount;
    if (_items[itemId] <= 0) _items.erase(itemId);
    EventBus::Get().Emit("InventoryChanged");
    return true;
}

bool InventoryManager::HasItem(const std::string& itemId, int amount) const {
    auto it = _items.find(itemId);
    return it != _items.end() && it->second >= amount;
}

int InventoryManager::GetCount(const std::string& itemId) const {
    auto it = _items.find(itemId);
    return it != _items.end() ? it->second : 0;
}

void InventoryManager::AddCredits(int amount) {
    Credits += amount;
    EventBus::Get().Emit("InventoryChanged");
}

bool InventoryManager::SpendCredits(int amount) {
    if (Credits < amount) return false;
    Credits -= amount;
    EventBus::Get().Emit("InventoryChanged");
    return true;
}

void InventoryManager::Reset() {
    Credits = 0;
}
