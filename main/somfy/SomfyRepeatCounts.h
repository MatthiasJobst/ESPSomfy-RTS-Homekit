// SomfyRepeatCounts.h — Frame repeat counts per command intent: how many times a
// frame is retransmitted to express a given action ("how long the button is held"
// over the air). Consumed by the shade command/movement layer and by the
// forced-move setting. Deliberately free of other project includes so any layer
// (including ConfigSettings) can use these without pulling in the transceiver or
// frame headers.
#pragma once
#include <cstdint>

constexpr uint8_t SETMY_REPEATS = 35; // program the "My" favorite position (long hold)
constexpr uint8_t TILT_REPEATS = 15;  // tilt-step burst

// MOVE_REPEATS — initial burst length for programmatic lift moves. ~1 s of
// continuous air time (8 frames × ~116 ms) is long enough for the motor to
// register a sustained press and start moving on its own; the transmitter is then
// freed. Intermediate targets are halted by a My frame from handlePosTargetReached.
constexpr uint8_t MOVE_REPEATS = 8;
