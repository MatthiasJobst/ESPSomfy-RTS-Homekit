#include "SomfyStateMachine.h"
#include <esp_log.h>
#include "SomfyShadeController.h"
#include "ShadeConfigFile.h"
#include "ConfigSettings.h"

extern ConfigSettings settings;

static const char *s_TAG = "SomfyStateMachine";

SomfyStateMachine::SomfyStateMachine(SomfyShadeController &somfy) : somfy(somfy)
{
}

bool SomfyStateMachine::begin()
{
    ESP_LOGI(s_TAG, "App Version:%u.%u.%u", settings.appVersion.major, settings.appVersion.minor,
             settings.appVersion.build);
    if (ShadeConfigFile::exists()) {
        ESP_LOGI(s_TAG, "shades.cfg exists so we are using that");
        ShadeConfigFile::load(&this->somfy);
    } else {
        ESP_LOGI(s_TAG, "Starting clean");
    }
    this->somfy.transceiver.begin();
    this->somfy.applyDefaultBitLengths();
    return true;
}

void SomfyStateMachine::loop()
{
    this->somfy.transceiver.loop();
    this->somfy.commandDispatcher.drainCommandQueue();
    this->somfy.tickShades();
    this->somfy.autoCommit();
}

void SomfyStateMachine::end()
{
    this->somfy.transceiver.disableReceive();
}
