// Standalone validation for DiplomaticRegistry.
// Compile: add this file + DiplomaticRegistry.cpp to a test executable target.
// Run: ./DiplomaticRegistryTests  →  prints PASS or each failing entry.

#include "systems/diplomacy/DiplomaticRegistry.h"
#include <cstdio>
#include <cstring>

static int s_failures = 0;

static void Check(Faction a, Faction b, Relation expected,
                  const char* aName, const char* bName) {
    Relation got = DiplomaticRegistry::Get(a, b);
    if (got != expected) {
        const char* relStr[] = { "Friendly", "Neutral", "Hostile" };
        std::printf("FAIL  [%s → %s]  expected %s  got %s\n",
                    aName, bName,
                    relStr[static_cast<int>(expected)],
                    relStr[static_cast<int>(got)]);
        ++s_failures;
    }
}

#define CHK(A, B, REL) Check(Faction::A, Faction::B, Relation::REL, #A, #B)

int main() {
    DiplomaticRegistry::Init();

    // ── All 81 entries (row = acting faction, col = target faction) ───────────
    //           Rep          Zen          Kor          Mer
    CHK(Republic, Republic, Friendly);  CHK(Republic, Zenith,   Neutral);
    CHK(Republic, Korrath,  Neutral);   CHK(Republic, Merchant, Neutral);
    //           Ede          Rea          For          Con          Voi
    CHK(Republic, Eden,     Friendly);  CHK(Republic, Reavers,  Hostile);
    CHK(Republic, Forge,    Hostile);   CHK(Republic, Conclave, Friendly);
    CHK(Republic, Void,     Neutral);

    CHK(Zenith, Republic,  Neutral);    CHK(Zenith, Zenith,    Friendly);
    CHK(Zenith, Korrath,   Hostile);    CHK(Zenith, Merchant,  Neutral);
    CHK(Zenith, Eden,      Friendly);   CHK(Zenith, Reavers,   Neutral);
    CHK(Zenith, Forge,     Neutral);    CHK(Zenith, Conclave,  Friendly);
    CHK(Zenith, Void,      Hostile);

    CHK(Korrath, Republic,  Neutral);   CHK(Korrath, Zenith,   Hostile);
    CHK(Korrath, Korrath,   Friendly);  CHK(Korrath, Merchant, Friendly);
    CHK(Korrath, Eden,      Neutral);   CHK(Korrath, Reavers,  Neutral);
    CHK(Korrath, Forge,     Hostile);   CHK(Korrath, Conclave, Neutral);
    CHK(Korrath, Void,      Friendly);

    CHK(Merchant, Republic,  Neutral);  CHK(Merchant, Zenith,   Neutral);
    CHK(Merchant, Korrath,   Friendly); CHK(Merchant, Merchant, Friendly);
    CHK(Merchant, Eden,      Neutral);  CHK(Merchant, Reavers,  Neutral);
    CHK(Merchant, Forge,     Hostile);  CHK(Merchant, Conclave, Hostile);
    CHK(Merchant, Void,      Friendly);

    CHK(Eden, Republic,  Friendly);     CHK(Eden, Zenith,   Friendly);
    CHK(Eden, Korrath,   Neutral);      CHK(Eden, Merchant, Neutral);
    CHK(Eden, Eden,      Friendly);     CHK(Eden, Reavers,  Hostile);
    CHK(Eden, Forge,     Neutral);      CHK(Eden, Conclave, Neutral);
    CHK(Eden, Void,      Hostile);

    CHK(Reavers, Republic,  Hostile);   CHK(Reavers, Zenith,   Neutral);
    CHK(Reavers, Korrath,   Neutral);   CHK(Reavers, Merchant, Neutral);
    CHK(Reavers, Eden,      Hostile);   CHK(Reavers, Reavers,  Friendly);
    CHK(Reavers, Forge,     Friendly);  CHK(Reavers, Conclave, Neutral);
    CHK(Reavers, Void,      Friendly);

    CHK(Forge, Republic,  Hostile);     CHK(Forge, Zenith,   Neutral);
    CHK(Forge, Korrath,   Hostile);     CHK(Forge, Merchant, Hostile);
    CHK(Forge, Eden,      Neutral);     CHK(Forge, Reavers,  Friendly);
    CHK(Forge, Forge,     Friendly);    CHK(Forge, Conclave, Neutral);
    CHK(Forge, Void,      Friendly);

    CHK(Conclave, Republic,  Friendly); CHK(Conclave, Zenith,   Friendly);
    CHK(Conclave, Korrath,   Neutral);  CHK(Conclave, Merchant, Hostile);
    CHK(Conclave, Eden,      Neutral);  CHK(Conclave, Reavers,  Neutral);
    CHK(Conclave, Forge,     Neutral);  CHK(Conclave, Conclave, Friendly);
    CHK(Conclave, Void,      Hostile);

    CHK(Void, Republic,  Neutral);      CHK(Void, Zenith,   Hostile);
    CHK(Void, Korrath,   Friendly);     CHK(Void, Merchant, Friendly);
    CHK(Void, Eden,      Hostile);      CHK(Void, Reavers,  Friendly);
    CHK(Void, Forge,     Friendly);     CHK(Void, Conclave, Hostile);
    CHK(Void, Void,      Friendly);

    // ── Verify all three relation types appear at least once ──────────────────
    bool sawF = false, sawN = false, sawH = false;
    for (int a = 0; a < 9; ++a)
        for (int b = 0; b < 9; ++b) {
            Relation r = DiplomaticRegistry::Get(static_cast<Faction>(a),
                                                  static_cast<Faction>(b));
            if (r == Relation::Friendly) sawF = true;
            if (r == Relation::Neutral)  sawN = true;
            if (r == Relation::Hostile)  sawH = true;
        }
    if (!sawF) { std::puts("FAIL  no Friendly entry found"); ++s_failures; }
    if (!sawN) { std::puts("FAIL  no Neutral entry found");  ++s_failures; }
    if (!sawH) { std::puts("FAIL  no Hostile entry found");  ++s_failures; }

    if (s_failures == 0)
        std::puts("PASS  all 81 entries correct");
    else
        std::printf("FAIL  %d check(s) failed\n", s_failures);

    return s_failures == 0 ? 0 : 1;
}
