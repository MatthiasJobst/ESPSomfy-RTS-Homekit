// test_controller_access.h — convenience helpers for SomfyShadeController tests.
// State under test (commandDispatcher.cmdQueue, lastCommit, the sub-controllers)
// is public, so no subclass tricks are needed.
#pragma once
#include "SomfyShadeController.h"
#include "SomfyStateMachine.h"
#include "SomfyCommandQueue.h"

extern SomfyShadeController somfy;
extern SomfyStateMachine stateMachine;
