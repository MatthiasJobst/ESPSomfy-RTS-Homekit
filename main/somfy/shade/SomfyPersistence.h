// SomfyPersistence.h — Encapsulates NVS load/save and position-commit logic for
// a SomfyShade. Owns no state of its own; all reads and writes go through the
// back-pointer set by the SomfyShade() constructor.
#pragma once
#include "SomfyFrame.h"

class SomfyShade; // forward declaration

class SomfyPersistence {
public:
    SomfyShade *shade = nullptr; // set by SomfyShade() constructor

    // Marks somfy as dirty.
    void commitShadePosition();
    void commitMyPosition();
    void commitTiltPosition();

    // Delegates to SomfyShadeController::commit() (marks dirty + schedules save).
    void commit();

    // Commit + publish (config file write is scheduled separately via commit()).
    bool save();
};
