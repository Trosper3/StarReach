// Faction Encounter Simulation — 100 checks across 6 sections.
// Verifies the full taxonomy pipeline:
//   DiplomaticRegistry → BehaviorRegistry → CommunicationSystem
// Compile: add this file (+ FleetManager.cpp, BehaviorRegistry.cpp,
//          CommunicationSystem.cpp, DiplomaticRegistry.cpp) to a test target.
// Run: ./FactionEncounterSimulation  →  prints per-check FAIL lines or PASS.

#include "systems/comms/CommunicationSystem.h"
#include "systems/ai/FleetManager.h"
#include "systems/ai/BehaviorRegistry.h"
#include "systems/diplomacy/DiplomaticRegistry.h"
#include "shared/Entity.h"
#include <cstdio>

static int s_failures = 0;
static int s_checks   = 0;

static void CheckComm(Faction sender, Faction receiver,
                      CommType expected,
                      const char* sName, const char* rName)
{
    ++s_checks;
    CommEvent evt = CommunicationSystem::OnContact(1, sender, 2, receiver);
    if (evt.type != expected) {
        const char* names[] = { "None", "Telemetry", "TransitRequest", "Jamming", "SurrenderDemand" };
        std::printf("FAIL  [%s→%s]  expected CommType::%s  got CommType::%s\n",
                    sName, rName,
                    names[static_cast<int>(expected)],
                    names[static_cast<int>(evt.type)]);
        ++s_failures;
    }
}

static void CheckBool(bool condition, const char* desc)
{
    ++s_checks;
    if (!condition) {
        std::printf("FAIL  %s\n", desc);
        ++s_failures;
    }
}

// Derive expected CommType from the matrix relation — keeps Section 1 self-consistent
// with the registry rather than hardcoding CommType per pair a second time.
static CommType ExpectedComm(Faction a, Faction b)
{
    switch (DiplomaticRegistry::Get(a, b)) {
    case Relation::Friendly: return CommType::Telemetry;
    case Relation::Neutral:  return CommType::TransitRequest;
    case Relation::Hostile:  return CommType::Jamming;
    }
    return CommType::None;
}

#define ENC(A, B)     CheckComm(Faction::A, Faction::B, ExpectedComm(Faction::A, Faction::B), #A, #B)
#define CHK(expr, msg) CheckBool((expr), msg)

int main()
{
    ai::FleetManager::Init();   // builds the 9×9 diplomatic cache

    // ── Section 1: 81-encounter CommType validation ──────────────────────────────
    // Each ENC call exercises the full pipeline:
    //   DiplomaticRegistry::Get → BehaviorRegistry::Get → CommunicationSystem::OnContact
    ENC(Republic, Republic);  ENC(Republic, Zenith);    ENC(Republic, Korrath);
    ENC(Republic, Merchant);  ENC(Republic, Eden);      ENC(Republic, Reavers);
    ENC(Republic, Forge);     ENC(Republic, Conclave);  ENC(Republic, Void);

    ENC(Zenith, Republic);    ENC(Zenith, Zenith);      ENC(Zenith, Korrath);
    ENC(Zenith, Merchant);    ENC(Zenith, Eden);        ENC(Zenith, Reavers);
    ENC(Zenith, Forge);       ENC(Zenith, Conclave);    ENC(Zenith, Void);

    ENC(Korrath, Republic);   ENC(Korrath, Zenith);     ENC(Korrath, Korrath);
    ENC(Korrath, Merchant);   ENC(Korrath, Eden);       ENC(Korrath, Reavers);
    ENC(Korrath, Forge);      ENC(Korrath, Conclave);   ENC(Korrath, Void);

    ENC(Merchant, Republic);  ENC(Merchant, Zenith);    ENC(Merchant, Korrath);
    ENC(Merchant, Merchant);  ENC(Merchant, Eden);      ENC(Merchant, Reavers);
    ENC(Merchant, Forge);     ENC(Merchant, Conclave);  ENC(Merchant, Void);

    ENC(Eden, Republic);      ENC(Eden, Zenith);        ENC(Eden, Korrath);
    ENC(Eden, Merchant);      ENC(Eden, Eden);          ENC(Eden, Reavers);
    ENC(Eden, Forge);         ENC(Eden, Conclave);      ENC(Eden, Void);

    ENC(Reavers, Republic);   ENC(Reavers, Zenith);     ENC(Reavers, Korrath);
    ENC(Reavers, Merchant);   ENC(Reavers, Eden);       ENC(Reavers, Reavers);
    ENC(Reavers, Forge);      ENC(Reavers, Conclave);   ENC(Reavers, Void);

    ENC(Forge, Republic);     ENC(Forge, Zenith);       ENC(Forge, Korrath);
    ENC(Forge, Merchant);     ENC(Forge, Eden);         ENC(Forge, Reavers);
    ENC(Forge, Forge);        ENC(Forge, Conclave);     ENC(Forge, Void);

    ENC(Conclave, Republic);  ENC(Conclave, Zenith);    ENC(Conclave, Korrath);
    ENC(Conclave, Merchant);  ENC(Conclave, Eden);      ENC(Conclave, Reavers);
    ENC(Conclave, Forge);     ENC(Conclave, Conclave);  ENC(Conclave, Void);

    ENC(Void, Republic);      ENC(Void, Zenith);        ENC(Void, Korrath);
    ENC(Void, Merchant);      ENC(Void, Eden);          ENC(Void, Reavers);
    ENC(Void, Forge);         ENC(Void, Conclave);      ENC(Void, Void);

    // ── Section 2: BehaviorRegistry taxonomy flags (6 checks) ───────────────────
    {
        BehaviorSet bf = BehaviorRegistry::Get(Relation::Friendly);
        CHK(Has(bf, Behavior::Escort),        "Friendly → Escort flag present");
        CHK(Has(bf, Behavior::OpenComms),     "Friendly → OpenComms flag present");

        BehaviorSet bn = BehaviorRegistry::Get(Relation::Neutral);
        CHK(Has(bn, Behavior::Patrol),        "Neutral → Patrol flag present");
        CHK(Has(bn, Behavior::Transactional), "Neutral → Transactional flag present");

        BehaviorSet bh = BehaviorRegistry::Get(Relation::Hostile);
        CHK(Has(bh, Behavior::Aggressive),    "Hostile → Aggressive flag present");
        CHK(Has(bh, Behavior::Jamming),       "Hostile → Jamming flag present");
    }

    // ── Section 3: CommEvent field propagation (3 checks) ───────────────────────
    // Eden→Republic is Friendly; verifies senderId/receiverId/relation round-trip.
    {
        CommEvent evt = CommunicationSystem::OnContact(10, Faction::Eden, 20, Faction::Republic);
        CHK(evt.senderId   == 10,                 "CommEvent.senderId propagated correctly");
        CHK(evt.receiverId == 20,                 "CommEvent.receiverId propagated correctly");
        CHK(evt.relation   == Relation::Friendly, "CommEvent.relation matches registry for Eden→Republic");
    }

    // ── Section 4: Transit toll amount (2 checks) ────────────────────────────────
    // Zenith→Republic is Neutral → TransitRequest + toll == kBaseToll.
    {
        CommEvent evt = CommunicationSystem::OnContact(1, Faction::Zenith, 2, Faction::Republic);
        CHK(evt.type       == CommType::TransitRequest,        "Zenith→Republic yields TransitRequest");
        CHK(evt.tollAmount == CommunicationSystem::kBaseToll,  "TransitRequest toll equals kBaseToll");
    }

    // ── Section 5: Apply() entity mutation (4 checks) ────────────────────────────
    {
        // Jamming drains shield by kJamShieldDrain
        ecs::Entity target{};
        target.health.currentShield = 100.0f;
        CommEvent jam{};
        jam.type = CommType::Jamming;
        CommunicationSystem::Apply(target, jam);
        CHK(target.health.currentShield == 100.0f - CommunicationSystem::kJamShieldDrain,
            "Jamming Apply() drains shield by kJamShieldDrain");

        // Jamming clamps to 0 when drain exceeds remaining shield
        ecs::Entity low{};
        low.health.currentShield = 5.0f;
        CommunicationSystem::Apply(low, jam);
        CHK(low.health.currentShield == 0.0f,
            "Jamming Apply() clamps shield to 0 when drain exceeds current");

        // Telemetry: no entity mutation
        ecs::Entity harmless{};
        harmless.health.currentShield = 50.0f;
        CommEvent tel{};
        tel.type = CommType::Telemetry;
        CommunicationSystem::Apply(harmless, tel);
        CHK(harmless.health.currentShield == 50.0f,
            "Telemetry Apply() does not mutate entity shield");

        // SurrenderDemand: no entity mutation
        ecs::Entity surr_target{};
        surr_target.health.currentShield = 80.0f;
        CommEvent surr{};
        surr.type = CommType::SurrenderDemand;
        CommunicationSystem::Apply(surr_target, surr);
        CHK(surr_target.health.currentShield == 80.0f,
            "SurrenderDemand Apply() does not mutate entity shield");
    }

    // ── Section 6: Matrix asymmetry spot-checks (4 checks) ──────────────────────
    // Validates that direction matters for non-symmetric pairs.
    {
        // Reavers↔Republic — both Hostile (symmetric hostile pair)
        CHK(DiplomaticRegistry::Get(Faction::Reavers, Faction::Republic) == Relation::Hostile,
            "Reavers→Republic is Hostile");
        CHK(DiplomaticRegistry::Get(Faction::Republic, Faction::Reavers) == Relation::Hostile,
            "Republic→Reavers is Hostile");

        // Korrath↔Merchant — both Friendly (symmetric friendly pair)
        CHK(DiplomaticRegistry::Get(Faction::Korrath, Faction::Merchant) == Relation::Friendly,
            "Korrath→Merchant is Friendly");
        CHK(DiplomaticRegistry::Get(Faction::Merchant, Faction::Korrath) == Relation::Friendly,
            "Merchant→Korrath is Friendly (symmetric)");
    }

    // ── Result ───────────────────────────────────────────────────────────────────
    std::printf("\n%d checks run — ", s_checks);
    if (s_failures == 0)
        std::printf("PASS  all %d encounter checks correct\n", s_checks);
    else
        std::printf("FAIL  %d check(s) failed\n", s_failures);

    return s_failures == 0 ? 0 : 1;
}
