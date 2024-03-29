/*
 * Copyright (C) 2022 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "vk_core/vk_descriptor.h"

#include <array>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

/**
 * VKCore contains the core of Vulkan pipeline setup. It includes recording
 * draw commands as well as screen clearing during the render pass.
 *
 * Please refer to: https://vulkan-tutorial.com/ for a gentle Vulkan
 * introduction.
 */

using namespace vkt;

class VKCore {
public:
    void initVulkan();
    void render();
    void cleanup();
    void cleanupSwapChain();
    void reset(ANativeWindow *newWindow, AAssetManager *newManager);
    bool initialized = false;

private:
    std::unique_ptr<Device> device;
    std::unique_ptr<SwapChain> swapChain;
    std::unique_ptr<Descriptor> descriptor;

    void createInstance();
    void createSurface();
    void setupDebugMessenger();
    void createRenderPass();
    //void createDescriptorSetLayout();
    void setPipeline();
    void createFramebuffers();
    void createCommandPool();
    void createCommandBuffer();
    void createSyncObjects();
    bool checkValidationLayerSupport();
    std::vector<const char *> getRequiredExtensions(bool enableValidation);
    VkShaderModule createShaderModule(const std::vector<uint8_t> &code);
    void drawFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex);
    void recreateSwapChain();
    void onOrientationChange();
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties, VkBuffer &buffer,
                      VkDeviceMemory &bufferMemory);
    void createUniformBuffers();
    void updateUniformBuffers(uint32_t currentImage);

    /*
     * In order to enable validation layer toggle this to true and
     * follow the README.md instructions concerning the validation
     * layers. You will be required to add separate vulkan validation
     * '*.so' files in order to enable this.
     *
     * The validation layers are not shipped with the APK as they are sizeable.
     */
    bool enableValidationLayers = false;

    const std::vector<const char *> validationLayers = {
            "VK_LAYER_KHRONOS_validation"};
    const std::vector<const char *> deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    std::unique_ptr<ANativeWindow, ANativeWindowDeleter> window;
    AAssetManager *assetManager;

    VkInstance instance;
    VkSurfaceKHR surface;

    VkDebugUtilsMessengerEXT debugMessenger;

    std::vector<VkFramebuffer> swapChainFramebuffers;
    VkCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;

    VkRenderPass renderPass;
    VkPipelineLayout pipelineLayout;
    VkPipeline graphicsPipeline;

    std::vector<VkBuffer> uniformBuffers;
    std::vector<VkDeviceMemory> uniformBuffersMemory;

    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;

    uint32_t currentFrame = 0;
    bool orientationChanged = false;
};

void VKCore::initVulkan() {
    createInstance();
    createSurface();
    device = std::make_unique<Device>(instance, surface);

    setupDebugMessenger();

    swapChain = std::make_unique<SwapChain>(*device);
    createRenderPass();
    createUniformBuffers();

    descriptor = std::make_unique<Descriptor>(*device, uniformBuffers);
    setPipeline();
    createFramebuffers();
    createCommandPool();
    createCommandBuffer();
    createSyncObjects();
    initialized = true;
}

/*
 *	Create a buffer with specified usage and memory properties
 *	i.e a uniform buffer which uses HOST_COHERENT memory
 *  Upon creation, these buffers will list memory requirements which need to be
 *  satisfied by the device in use in order to be created.
 */
void VKCore::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags properties, VkBuffer &buffer,
                          VkDeviceMemory &bufferMemory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK(vkCreateBuffer(device->getDevice(), &bufferInfo, nullptr, &buffer));

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device->getDevice(), buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = device->findMemoryType(memRequirements.memoryTypeBits, properties);

    VK_CHECK(vkAllocateMemory(device->getDevice(), &allocInfo, nullptr, &bufferMemory));

    vkBindBufferMemory(device->getDevice(), buffer, bufferMemory, 0);
}

void VKCore::createUniformBuffers() {
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);

    uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    uniformBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     uniformBuffers[i], uniformBuffersMemory[i]);
    }
}

void VKCore::reset(ANativeWindow *newWindow, AAssetManager *newManager) {
    window.reset(newWindow);
    assetManager = newManager;
    if (initialized) {
        createSurface();
        recreateSwapChain();
    }
}

void VKCore::recreateSwapChain() {
    vkDeviceWaitIdle(device->getDevice());
    cleanupSwapChain();
    swapChain = std::make_unique<SwapChain>(*device);
    createFramebuffers();
}

void VKCore::render() {
    if (orientationChanged) {
        onOrientationChange();
    }

    vkWaitForFences(device->getDevice(), 1, &inFlightFences[currentFrame], VK_TRUE,
                    UINT64_MAX);
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(
            device->getDevice(), swapChain->getSwapChain(), UINT64_MAX, imageAvailableSemaphores[currentFrame],
            VK_NULL_HANDLE, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain();
        return;
    }
    assert(result == VK_SUCCESS ||
           result == VK_SUBOPTIMAL_KHR);  // failed to acquire swap chain image
    updateUniformBuffers(currentFrame);

    vkResetFences(device->getDevice(), 1, &inFlightFences[currentFrame]);
    vkResetCommandBuffer(commandBuffers[currentFrame], 0);

    drawFrame(commandBuffers[currentFrame], imageIndex);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
    VkPipelineStageFlags waitStages[] = {
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[currentFrame];
    VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    VK_CHECK(vkQueueSubmit(device->getGraphicsQueue(), 1, &submitInfo,
                           inFlightFences[currentFrame]));

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = {swapChain->getSwapChain()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;
    presentInfo.pResults = nullptr;

    result = vkQueuePresentKHR(device->getPresentQueue(), &presentInfo);
    if (result == VK_SUBOPTIMAL_KHR) {
        orientationChanged = true;
    } else if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain();
    } else {
        assert(result == VK_SUCCESS);  // failed to present swap chain image!
    }
    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VKCore::updateUniformBuffers(uint32_t currentImage) {
    static auto startTime = std::chrono::high_resolution_clock::now();

    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

    UniformBufferObject ubo{};

    auto radiansToRotate = 60.0f;
    ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(radiansToRotate), glm::vec3(1.0f, 0.0f, 0.0f)); // Rotate around x-axis
    ubo.model = glm::rotate(ubo.model, time * glm::radians(radiansToRotate), glm::vec3(0.0f, 1.0f, 0.0f)); // Rotate around y-axis
    ubo.model = glm::rotate(ubo.model, time * glm::radians(radiansToRotate), glm::vec3(0.0f, 0.0f, 1.0f)); // Rotate around z-axis
    ubo.view = glm::lookAt(glm::vec3(4.0f, 4.0f, 4.0f),
                           glm::vec3(0.0f, 0.0f, 0.0f),
                           glm::vec3(0.0f, 0.0f, 1.0f));
    ubo.proj = glm::perspective(glm::radians(45.0f), swapChain->getSwapChainExtent().width / (float) swapChain->getSwapChainExtent().height, 0.1f, 10.0f);

    void *data;
    vkMapMemory(device->getDevice(), uniformBuffersMemory[currentImage], 0, sizeof(ubo), 0, &data);
    memcpy(data, &ubo, sizeof(ubo));
    vkUnmapMemory(device->getDevice(), uniformBuffersMemory[currentImage]);
}

void VKCore::onOrientationChange() {
    recreateSwapChain();
    orientationChanged = false;
}

void VKCore::drawFrame(VkCommandBuffer commandBuffer,
                       uint32_t imageIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;
    beginInfo.pInheritanceInfo = nullptr;

    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapChainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapChain->getSwapChainExtent();

    VkViewport viewport{};
    viewport.width = (float)swapChain->getSwapChainExtent().width;
    viewport.height = (float)swapChain->getSwapChainExtent().height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = swapChain->getSwapChainExtent();
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    VkClearValue clearColor = {{{0.25f, 0.3f, 0.25f, 1.0f}}};

    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo,
                         VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      graphicsPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout, 0, 1, &descriptor->getDescriptorSets()[currentFrame],
                            0, nullptr);

    vkCmdDraw(commandBuffer, 36, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer);
    VK_CHECK(vkEndCommandBuffer(commandBuffer));
}

void VKCore::cleanupSwapChain() {
    for (size_t i = 0; i < swapChainFramebuffers.size(); i++) {
        vkDestroyFramebuffer(device->getDevice(), swapChainFramebuffers[i], nullptr);
    }
}

void VKCore::cleanup() {
    vkDeviceWaitIdle(device->getDevice());
    cleanupSwapChain();
    swapChain = nullptr;
    descriptor = nullptr;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroyBuffer(device->getDevice(), uniformBuffers[i], nullptr);
        vkFreeMemory(device->getDevice(), uniformBuffersMemory[i], nullptr);
    }

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(device->getDevice(), imageAvailableSemaphores[i], nullptr);
        vkDestroySemaphore(device->getDevice(), renderFinishedSemaphores[i], nullptr);
        vkDestroyFence(device->getDevice(), inFlightFences[i], nullptr);
    }
    vkDestroyCommandPool(device->getDevice(), commandPool, nullptr);
    vkDestroyPipeline(device->getDevice(), graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(device->getDevice(), pipelineLayout, nullptr);
    vkDestroyRenderPass(device->getDevice(), renderPass, nullptr);
    vkDestroyDevice(device->getDevice(), nullptr);
    if (enableValidationLayers) {
        DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
    }
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
    initialized = false;
}

void VKCore::setupDebugMessenger() {
    if (!enableValidationLayers) {
        return;
    }

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    populateDebugMessengerCreateInfo(createInfo);

    VK_CHECK(CreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr,
                                          &debugMessenger));
}

bool VKCore::checkValidationLayerSupport() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char *layerName : validationLayers) {
        bool layerFound = false;
        for (const auto &layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }

        if (!layerFound) {
            return false;
        }
    }
    return true;
}

std::vector<const char *> VKCore::getRequiredExtensions(
        bool enableValidation) {
    std::vector<const char *> extensions;
    extensions.push_back("VK_KHR_surface");
    extensions.push_back("VK_KHR_android_surface");
    if (enableValidation) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    return extensions;
}

void VKCore::createInstance() {
    assert(!enableValidationLayers ||
           checkValidationLayerSupport());  // validation layers requested, but
    // not available!
    auto requiredExtensions = getRequiredExtensions(enableValidationLayers);

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Hello Triangle";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = (uint32_t)requiredExtensions.size();
    createInfo.ppEnabledExtensionNames = requiredExtensions.data();
    createInfo.pApplicationInfo = &appInfo;

    if (enableValidationLayers) {
        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
        createInfo.enabledLayerCount =
                static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
        populateDebugMessengerCreateInfo(debugCreateInfo);
        createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT *)&debugCreateInfo;
    } else {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }
    VK_CHECK(vkCreateInstance(&createInfo, nullptr, &instance));

    uint32_t extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> extensions(extensionCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount,
                                           extensions.data());
    LOG_INFO("available extensions");
    for (const auto &extension : extensions) {
        LOG_INFO("\t %s", extension.extensionName);
    }
}

/*
 * createSurface can only be called after the android ecosystem has had the
 * chance to provide a native window. This happens after the APP_CMD_START event
 * has had a chance to be called.
 *
 * Notice the window.get() call which is only valid after window has been set to
 * a non null value
 */
void VKCore::createSurface() {
    assert(window != nullptr);  // window not initialized
    const VkAndroidSurfaceCreateInfoKHR create_info{
            .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
            .pNext = nullptr,
            .flags = 0,
            .window = window.get()};

    VK_CHECK(vkCreateAndroidSurfaceKHR(instance, &create_info,
                                       nullptr /* pAllocator */, &surface));
}

// BEGIN DEVICE SUITABILITY
// Functions to find a suitable physical device to execute Vulkan commands.

// END DEVICE SUITABILITY

void VKCore::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapChain->getSwapChainImageFormat();
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;

    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = device->findDepthFormat();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    VK_CHECK(vkCreateRenderPass(device->getDevice(), &renderPassInfo, nullptr, &renderPass));
}

/*
 * Creates a graphics pipeline loading a simple vertex and fragment shader, both
 * with 'main' set as entrypoint A list of standard parameters are provided:
 * 	- The vertex input coming from the application is set to empty - we are
 * hardcoding the triangle in the vertex shader.
 * 	- The input assembly is configured to draw triangle lists
 *  - We intend to draw onto the whole screen, so the scissoring extent is
 * specified as being the whole swapchain extent.
 * 	- The rasterizer is set to discard fragmets beyond the near and far
 * planes (depthClampEnable=false) as well as sending geometry to the frame
 * buffer and generate fragments for the whole area of the geometry. We consider
 * geometry in terms of the clockwise order of their respective vertex input.
 *  - Multisampling is disabled
 *  - Depth and stencil testing are disabled
 * 	- ColorBlending is set to opaque mode, meaning any new fragments will
 * overwrite the ones already existing in the framebuffer
 *  - We utilise Vulkan's concept of dynamic state for viewport and scissoring.
 * 		The other option is to hardcode the viewport/scissor options,
 * however this means needing to recreate the whole graphics pipeline object
 * when the screen is rotated.
 *  - The pipeline layout sends 1 uniform buffer object to the shader containing
 * a 4x4 rotation matrix specified by the descriptorSetLayout. This is required
 * in order to render a rotated scene when the device has been rotated.
 */
void VKCore::setPipeline() {
    auto vertShaderCode =
            LoadBinaryFileToVector("shaders/shader.vert.spv", assetManager);
    auto fragShaderCode =
            LoadBinaryFileToVector("shaders/shader.frag.spv", assetManager);

    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType =
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType =
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo,
                                                      fragShaderStageInfo};

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType =
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.pVertexBindingDescriptions = nullptr;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.pVertexAttributeDescriptions = nullptr;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType =
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;

    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0f;
    rasterizer.depthBiasClamp = 0.0f;
    rasterizer.depthBiasSlopeFactor = 0.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType =
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.minSampleShading = 1.0f;
    multisampling.pSampleMask = nullptr;
    multisampling.alphaToCoverageEnable = VK_FALSE;
    multisampling.alphaToOneEnable = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType =
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptor->getDescriptorSetLayout();
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = nullptr;

    VK_CHECK(vkCreatePipelineLayout(device->getDevice(), &pipelineLayoutInfo, nullptr,
                                    &pipelineLayout));
    std::vector<VkDynamicState> dynamicStateEnables = {VK_DYNAMIC_STATE_VIEWPORT,
                                                       VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicStateCI{};
    dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicStateCI.pDynamicStates = dynamicStateEnables.data();
    dynamicStateCI.dynamicStateCount =
            static_cast<uint32_t>(dynamicStateEnables.size());

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = nullptr;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicStateCI;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;
    pipelineInfo.pDepthStencilState = &depthStencil;

    VK_CHECK(vkCreateGraphicsPipelines(device->getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo,
                                       nullptr, &graphicsPipeline));
    vkDestroyShaderModule(device->getDevice(), fragShaderModule, nullptr);
    vkDestroyShaderModule(device->getDevice(), vertShaderModule, nullptr);
}

VkShaderModule VKCore::createShaderModule(const std::vector<uint8_t> &code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();

    // Satisfies alignment requirements since the allocator
    // in vector ensures worst case requirements
    createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());
    VkShaderModule shaderModule;
    VK_CHECK(vkCreateShaderModule(device->getDevice(), &createInfo, nullptr, &shaderModule));

    return shaderModule;
}

void VKCore::createFramebuffers() {
    swapChainFramebuffers.resize(swapChain->getSwapChainImageViews().size());
    for (size_t i = 0; i < swapChain->getSwapChainImageViews().size(); i++) {
        VkImageView attachments[] = {
            swapChain->getSwapChainImageViews()[i],
            swapChain->getDepthImageView()
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = swapChain->getSwapChainExtent().width;
        framebufferInfo.height = swapChain->getSwapChainExtent().height;
        framebufferInfo.layers = 1;

        VK_CHECK(vkCreateFramebuffer(device->getDevice(), &framebufferInfo, nullptr,
                                     &swapChainFramebuffers[i]));
    }
}

void VKCore::createCommandPool() {
    QueueFamilyIndices queueFamilyIndices = device->findQueueFamilies(device->getPhysicalDevice());
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();
    VK_CHECK(vkCreateCommandPool(device->getDevice(), &poolInfo, nullptr, &commandPool));
}

void VKCore::createCommandBuffer() {
    commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = commandBuffers.size();

    VK_CHECK(vkAllocateCommandBuffers(device->getDevice(), &allocInfo, commandBuffers.data()));
}

void VKCore::createSyncObjects() {
    imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VK_CHECK(vkCreateSemaphore(device->getDevice(), &semaphoreInfo, nullptr,
                                   &imageAvailableSemaphores[i]));

        VK_CHECK(vkCreateSemaphore(device->getDevice(), &semaphoreInfo, nullptr,
                                   &renderFinishedSemaphores[i]));

        VK_CHECK(vkCreateFence(device->getDevice(), &fenceInfo, nullptr, &inFlightFences[i]));
    }
}