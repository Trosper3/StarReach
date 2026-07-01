#pragma once

#include "core/FactionEnum.h"
#include <string>
#include <unordered_map>

enum class Relation : uint8_t
{
    Friendly,
    Neutral,
    Hostile
};

// Runtime-mutable faction relationship store.
//
// String-keyed so any faction ID works — the 9 canon NPC factions plus
// player factions ("player" / "player_<uuid>") all participate identically.
// Unknown pairs default to Neutral; no recompile needed for new factions.
class DiplomaticRegistry
{
public:
    // Load initial NPC relations from JSON; falls back to embedded defaults
    // if the file is missing or malformed. Call once at startup, before
    // ai::FleetManager::Init().
    static void Init(const std::string& jsonPath = "config/faction_relations.json");

    // String-based API — the primary interface.
    // Get() returns Neutral for any pair that has never been Set().
    static Relation Get(const std::string& a, const std::string& b);
    static void     Set(const std::string& a, const std::string& b, Relation r);

    // Enum shim — converts canonical Faction values to their string IDs
    // and delegates to the string API. Keeps all existing callers unchanged.
    static Relation Get(Faction a, Faction b);

private:
    static std::unordered_map<std::string, Relation> s_relations;
};
