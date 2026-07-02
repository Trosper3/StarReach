# StarReach

A space game written in modern **C++20** with **raylib**, featuring open-world flight combat, planetary exploration, and ship/station construction — playable solo or with friends over LAN/direct-IP multiplayer.

StarReach began as a rewrite of a Windows Forms C# Asteroids clone, rebuilt from the ground up with a real entity-component architecture, a procedurally generated galaxy, and a listen-server netcode layer.

---

## Features

### Three game modes
- **Space Flight** — fly, fight, trade, and explore in a live star system: NPC combat, faction diplomacy, hardpoint-based capital ships, and player-built stations.
- **Planet** — surface exploration mode, entered from orbit.
- **Builder** — design ships and stations from modular hardpoints and components.

### Procedural galaxy & warp travel
- A galaxy of **1,000,000 star systems**, generated on demand from a single seed (grid-hash generator — no bulk precomputation, O(1) lookup per system).
- Pannable/zoomable **galactic map** with fog-of-war style discovery, and an in-system **local map** for short-range travel.
- Cinematic **galactic warp** (turn-to-face → burn out → fade → arrival) and **local warp** (fast in-system dash) sequences, gated by your equipped hyperdrive's jump range.
- Each system has a procedurally typed star (O/B/A/F/G/K/M class) with real gravity wells, orbiting planets, and space stations.

### Ships, modules & hardpoints
- Data-driven ship and module registries (weapons, armor, shields, engines, hyperdrives, auxiliary) loaded from JSON — no recompiling to add content.
- Rarity tiers from Common to Mythic, with equip slots and capital-ship **hardpoints** that snap in sub-modules.
- A composite visual pipeline: most ships/stations are baked procedurally from palette-driven pixel-art definitions; unique assets can be loaded from static textures.

### Crafting, building & economy
- Gather materials, craft items, and construct **player-owned stations** (mining stations, defense platforms, trade stations) directly in space.
- An **Engineer** system for grafting attributes onto gear, with risk/reward mechanics (failure chance, stat caps for "merged" vs. "found" items, blueprint extraction, and attribute synergies).
- Combat wreckage feeds a salvage pipeline that recycles destroyed ships' loadouts into crafting materials.

### Living factions & diplomacy
- A 9-faction relationship matrix (Friendly / Neutral / Hostile) drives NPC behavior — escort and open comms with friendlies, tolls and patrols from neutrals, jamming and aggression from hostiles.
- Relationships are dynamic at runtime, with temporary diplomatic overrides and faction-gated services.

### Multiplayer
- **Listen-server** co-op over **ENet**, with host-authoritative physics/NPCs and client-side prediction for your own ship.
- **Independent per-system worlds** — each player's system simulates and ticks on its own; background systems stay live while unoccupied.
- Host or join directly by IP from the main menu; galaxy seed is shared automatically so everyone explores the same universe.

---

## Tech stack

| Component | Library |
|---|---|
| Language | C++20 |
| Rendering / windowing / input | [raylib](https://github.com/raysan5/raylib) 5.5 |
| Debug UI | [Dear ImGui](https://github.com/ocornut/imgui) + [rlImGui](https://github.com/raylib-extras/rlImGui) |
| Physics | [Box2D](https://github.com/erincatto/box2d) 2.4.1 |
| Data serialization | [nlohmann/json](https://github.com/nlohmann/json) |
| Networking | [ENet](https://github.com/lsalzman/enet) 1.3.18 (UDP) |
| Build system | CMake (`FetchContent`, no vendored dependencies) |

**Architecture:** an Entity-Component design (Transform, Sprite, Health, Loadout, Inventory, AIController, Network, DockingPort components) driven by a fixed-order system pipeline (Input → AI → Movement → Combat → Damage → NetworkSync → Render), with JSON-driven registries acting as the data layer for ships, modules, items, factions, and stations.

---

## Building

Requires **Visual Studio 2022** (MSVC toolchain) and **CMake 3.20+**. All third-party dependencies are fetched automatically via CMake's `FetchContent` — no manual dependency installation needed.

> **Note:** Build from a VS2022 Developer Command Prompt (or the Visual Studio IDE / CMake integration). Running bare `cmake`/`ninja` from a plain PowerShell prompt will fail with missing MSVC standard library headers.

```powershell
# From a VS2022 Developer Command Prompt
cmake -B build
cmake --build build --config Debug
```

The build copies `assets/` and `config/` into the output directory automatically, and produces the `StarReach` executable.

---

## Controls

| Action | Key |
|---|---|
| Thrust / reverse | `W` / `S` (or `↑` / `↓`) |
| Turn | `A` / `D` (or `←` / `→`) |
| Fire / select | Mouse |
| Weapon groups | Number keys |
| Close menu / cancel | `Esc` |
| Build, map, storage, modules, etc. | On-screen HUD buttons |

---

## Multiplayer

StarReach uses a **listen-server** model — one player hosts, others join directly by IP (default port `7777`). From the main menu's **Multiplayer** screen:

- **Host Game** — starts a server and enters space flight as the host.
- **Join Game** — enter the host's IP address and connect.

The host simulates the shared galaxy authoritatively; each connected player's current system is simulated independently, so travel and combat scale per system rather than per lobby.

---

## Project layout

```
src/
  core/         — engine-wide managers, registries, factories
  data/         — JSON-backed data registries (ships, modules, items, factions, stars...)
  engine/       — ECS systems (movement, combat, damage, rendering, camera, AI, network sync)
  modes/        — the three game modes (space_flight, planet, builder) + main menu
  net/          — ENet transport layer, protocol, session management
  shared/       — ECS components and shared entity definitions
  systems/      — diplomacy, comms, and AI behavior systems
  items/        — crafting, grafting, blueprints, salvage
  services/      — engineer/service-layer logic
  ui/           — MVC-style menu controllers/views
config/         — JSON definitions for ships, weapons, factions, stations, planets, progression
assets/         — art, audio, fonts, shaders (baked into the binary at build time)
tools/          — standalone asset-prep utilities (image-to-pixel-art conversion, PNG cleanup)
tests/          — standalone diplomacy & simulation test suites
```

---

## Status

StarReach is in active development. Core systems (flight, combat, crafting, diplomacy, galaxy generation, and multiplayer) are implemented and playable; content and balancing are ongoing.
