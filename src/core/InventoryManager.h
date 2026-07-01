#pragma once
#include <string>
#include <unordered_map>

/// Owns all player resources: items, materials, and credits.
/// Always mutate through these methods so EventBus signals fire correctly.
class InventoryManager {
public:
    static InventoryManager& Get() {
        static InventoryManager instance;
        return instance;
    }

    int Credits = 0;

    void AddItem(const std::string& itemId, int amount = 1);
    bool RemoveItem(const std::string& itemId, int amount = 1);
    bool HasItem(const std::string& itemId, int amount = 1) const;
    int  GetCount(const std::string& itemId) const;
    void Reset();

    void AddCredits(int amount);
    bool SpendCredits(int amount);

    const std::unordered_map<std::string, int>& Items() const { return _items; }
          std::unordered_map<std::string, int>& Items()       { return _items; }

private:
    InventoryManager() = default;
    std::unordered_map<std::string, int> _items;
};
