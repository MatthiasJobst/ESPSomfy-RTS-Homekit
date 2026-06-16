// ShadeStore.cpp — ShadeStore implementation: dirty-flag/auto-commit throttle
// and the ShadeConfigFile load/save/backup path, all gated on git.lockFS.
#include "ShadeStore.h"
#include "SomfyShadeController.h"
#include "ShadeConfigFile.h"
#include "GitOTA.h"

extern GitUpdater git;

ShadeStore::ShadeStore(SomfyShadeController &controller) : controller(controller) {}

void ShadeStore::markDirty()
{
    this->dirty = true;
}

void ShadeStore::commit()
{
    if (git.lockFS) return;
    ShadeConfigFile file;
    file.begin();
    file.save(&this->controller);
    file.end();
    this->dirty = false;
    this->lastCommit = millis();
}

void ShadeStore::autoCommit()
{
    // Only commit the file once per second.
    if (this->dirty && millis() - this->lastCommit > 1000) {
        this->commit();
    }
}

void ShadeStore::writeBackup()
{
    if (git.lockFS) return;
    ShadeConfigFile file;
    file.begin("/controller.backup", false);
    file.backup(&this->controller);
    file.end();
}

bool ShadeStore::load(const char *filename)
{
    return ShadeConfigFile::load(&this->controller, filename);
}
