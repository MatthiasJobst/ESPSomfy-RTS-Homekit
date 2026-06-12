// SomfyStateMachine.h — Drives the Somfy system from the app_main task: boots it once
// (begin) and ticks its periodic work on every poll-loop iteration (loop). It owns
// no data — it sequences operations on the SomfyShadeController model.
#pragma once

class SomfyShadeController;

/**
 * @brief Boots and drives the periodic work of a SomfyShadeController.
 *
 * Holds no state of its own; it sequences the model's operations (radio poll,
 * command drain, shade tick, auto-commit) on each loop iteration.
 */
class SomfyStateMachine {
  public:
    /**
     * @brief Construct over the model this state machine drives.
     * @param somfy The shade controller model.
     */
    explicit SomfyStateMachine(SomfyShadeController &somfy);

    /**
     * @brief Boot the system: load config, start the transceiver, apply defaults.
     * @return Always true.
     */
    bool begin();

    /**
     * @brief One poll-loop iteration: poll the radio, drain queued commands,
     *        advance shade movement, and auto-commit if dirty.
     */
    void loop();

    /**
     * @brief Stop receiving (shutdown hook).
     */
    void end();

  private:
    SomfyShadeController &somfy;
};
