// SomfyTargetSequencer.h — Programmatic position-seek logic for SomfyShade.
// Owns moveToTarget, moveToTiltTarget, moveToMyPosition, and setMyPosition.
// All four methods write the shared MotionState flags (settingPos / settingTiltPos /
// settingMyPos) to coordinate with MovementTracker (checkMovement) and CommandProcessor
// (processFrame external-command abort).
#pragma once

#include <stdint.h>        // uint8_t / int8_t
#include "../SomfyFrame.h" // somfy_commands

class SomfyShade; // forward declaration — full type in SomfyShade.h

/**
 * @brief Translates a requested lift/tilt position into the RF commands that drive
 *        the owning SomfyShade there, and owns its favorite ("My") position.
 *
 * Each move method issues one or more directional commands through the shade and
 * sets the shared MotionState flags (settingPos / settingTiltPos / settingMyPos) so
 * MovementTracker (checkMovement) and CommandProcessor (processFrame) can track the
 * resulting motion and arbitrate external commands. The favorite position
 * (myPos / myTiltPos) is owned here and read by MQTT, persistence, and JSON.
 */
class SomfyTargetSequencer {
  public:
    SomfyShade *shade = nullptr; /**< Owning shade; set by the SomfyShade constructor. Must not be NULL. */
    float myPos = -1.0f;         /**< Favorite ("My") lift position, 0-100, or -1 when unset. */
    float myTiltPos = -1.0f;     /**< Favorite ("My") tilt position, 0-100, or -1 when unset. */

    /**
     * @brief Move the shade toward a lift position (and optionally tilt) using the
     *        normal repeat count.
     *
     * Sends a single directional command sized by repeatCount(), records the target,
     * sets settingPos, and clears the boosted-stop flag.
     *
     * @param pos  Target lift position, 0-100.
     * @param tilt Target tilt position, 0-100; pass < 0 to leave tilt unchanged.
     */
    void moveToTarget(float pos, float tilt = -1.0f);

    /**
     * @brief Move the shade toward a lift position using the forced repeat count.
     *
     * Like moveToTarget() but uses forcedMoveRepeatCount() (configurable, defaults to
     * MOVE_REPEATS) to guarantee the motor registers a sustained press, and opts the
     * auto-stop into a boosted stop. Tilt is ignored.
     *
     * @param pos  Target lift position, 0-100.
     * @param tilt Ignored in a forced move (logged as a warning).
     */
    void moveToTargetForced(float pos, float tilt = -1.0f);

    /**
     * @brief Move the tilt toward a target position.
     *
     * Only sends a command when the lift is idle or the shade is a tilt-motor type;
     * always records the tilt target when it is in range.
     *
     * @param target Target tilt position, 0-100; values outside the range are ignored.
     */
    void moveToTiltTarget(float target);

    /**
     * @brief Send the shade to its stored favorite ("My") position.
     *
     * Uses a simulated move (moveToTarget) when the shade simulates My, otherwise
     * issues a My command.
     *
     * @note No-op unless the shade is idle, the favorite is set, and the shade is not
     *       already at it.
     */
    void moveToMyPosition();

    /**
     * @brief Program the favorite ("My") position from a target lift/tilt.
     *
     * Handles the three tiltType branches (tiltonly, tilt, none), moving the shade as
     * needed and committing the new favorite.
     *
     * @param pos  Lift position to store, 0-100.
     * @param tilt Tilt position to store, 0-100; pass < 0 when the shade has no tilt.
     * @note No-op unless the shade is idle.
     */
    void setMyPosition(int8_t pos, int8_t tilt = -1);

  private:
    /**
     * @brief Choose the command (Up/Down/My) needed to reach pos/tilt from the
     *        current position.
     *
     * @param pos  In/out target lift position; forced to 100 for tilt-only shades.
     * @param tilt Target tilt position; pass < 0 when tilt is not being set.
     * @return The directional command to send, or My when no move is needed.
     */
    somfy_commands moveDirection(float &pos, float tilt);

    /**
     * @brief Frame repeat count for a normal move, derived from tiltType and timings.
     * @return Number of times to repeat the frame.
     */
    uint8_t repeatCount() const;

    /**
     * @brief Frame repeat count for a forced move (ConfigSettings::forcedMoveRepeats).
     * @return Number of times to repeat the frame.
     */
    uint8_t forcedMoveRepeatCount() const;
};
