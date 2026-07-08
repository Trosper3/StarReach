#pragma once
#include "core/FactionEnum.h"
#include <cstdint>
#include <string>

// Epic 7 (bounty/contract board, tasks_spaceflight_dynamics.md #7): a single
// contract offer or in-progress job. One struct serves both roles — an
// "offer" (as returned by SpaceFlight::GenerateContractOffers) becomes the
// active contract verbatim once accepted, just copied into
// SpaceFlight::_activeContract. Locked decision: first pass covers all
// three types (Bounty/Courier/Escort), one active contract at a time.
enum class ContractType : uint8_t { Bounty, Courier, Escort };

struct Contract {
    unsigned int id            = 0;
    ContractType type          = ContractType::Bounty;
    Faction      issuerFaction = Faction::Republic;
    std::string  title;
    std::string  briefing;
    int          rewardCredits    = 0;
    float        rewardReputation = 0.0f;

    // Bounty: kill `killsRequired` ships of `targetFaction` (anywhere) while active.
    Faction targetFaction = Faction::Republic;
    int     killsRequired = 0;
    int     killsDone     = 0;

    // Courier: haul `amount` units of `goodId` from the issuing station to
    // (destStationId, destWorldKey) within timeLimit. Accepting the contract
    // immediately debits `amount` from the origin's live stock (the cargo is
    // "loaded" — no separate storage-item representation); docking at the
    // destination while this contract is active and its type is Courier
    // completes it, crediting the destination's stock.
    unsigned int originStationId = 0;
    unsigned int destStationId   = 0;
    uint64_t     destWorldKey    = 0;
    std::string  goodId;
    int          amount = 0;

    // Escort: stay near NPC `escortNpcId` and keep it alive for `timeLimit`
    // seconds. Fails immediately if that NPC dies first.
    unsigned int escortNpcId = 0;

    float timeLimit     = 0.0f; // Courier/Escort only; Bounty has no timer.
    float timeRemaining = 0.0f;
};
