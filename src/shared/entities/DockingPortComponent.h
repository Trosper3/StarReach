#pragma once

// Marks an entity as having a dockable service port.
// dockRadius == 0.f means this component is inactive (entity has no dock).
struct DockingPortComponent {
    float dockRadius    = 0.f;   // interaction threshold in world units; 0 = inactive
    bool  isEngineering = false; // if true, docking triggers the engineering service
};
