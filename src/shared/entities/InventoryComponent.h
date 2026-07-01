#pragma once
#include <string>
#include <unordered_map>

class InventoryComponent {
public:
    std::unordered_map<std::string, int> items;

    void AddItem(const std::string& id, int qty) {
        items[id] += qty;
    }

    bool HasMultiple(const std::string& id, int qty) const {
        auto it = items.find(id);
        return it != items.end() && it->second >= qty;
    }

    void RemoveMultiple(const std::string& id, int qty) {
        auto it = items.find(id);
        if (it == items.end()) return;
        it->second -= qty;
        if (it->second <= 0)
            items.erase(it);
    }
};
