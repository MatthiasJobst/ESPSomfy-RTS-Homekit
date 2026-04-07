// SomfyPersistence.h — Encapsulates NVS load/save and position-commit logic for
// a SomfyShade. Owns no state of its own; all reads and writes go through the
// back-pointer set by the SomfyShade() constructor.
#pragma once
#include "SomfyFrame.h"

class SomfyShade; // forward declaration

class SomfyPersistence {
public:
    SomfyShade *shade = nullptr; // set by SomfyShade() constructor

    // Marks somfy as dirty and (when USE_NVS && useNVS()) flushes individual
    // position fields to NVS without a full save().
    void commitShadePosition();
    void commitMyPosition();
    void commitTiltPosition();

    // Delegates to SomfyShadeController::commit() (marks dirty + schedules save).
    void commit();

    // Full NVS write of all shade fields, then commit + publish.
    bool save();

#ifdef USE_NVS
    // Reads all fields back from NVS into the shade (called at boot).
    void load();
#endif
};
