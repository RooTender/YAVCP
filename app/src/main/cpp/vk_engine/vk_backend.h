//
// Created by rootender on 1/7/24.
//

#ifndef YAVCP_VK_LOOP_H
#define YAVCP_VK_LOOP_H

#endif //YAVCP_VK_LOOP_H

#include "vk_core.h"

class VKBackend : protected VKCore
{
public:
    void initVulkan();
    void render();
    void cleanup();
    void reset(ANativeWindow *newWindow, AAssetManager *newManager);
    bool isInitialized();

private:
    std::unique_ptr<VKCore> core;
};

void VKBackend::initVulkan() {
    core = std::make_unique<VKCore>();
}

void VKBackend::cleanup() {
    core = nullptr;
}

void VKBackend::render() {
    if (isInitialized()) {
        core->render();
    }
}

void VKBackend::reset(ANativeWindow *newWindow, AAssetManager *newManager) {
    window.reset(newWindow);
    assetManager = newManager;

    if (core) {
        core->reset();
    }
}

bool VKBackend::isInitialized() {
    return core != nullptr;
}
