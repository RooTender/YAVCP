#include "vk_device.h"

class SwapChain {
public:
    SwapChain(Device& device);
    ~SwapChain();

    SwapChain(const SwapChain&) = delete;
    SwapChain& operator=(const SwapChain&) = delete;

    VkSwapchainKHR getSwapChain() const { return swapChain; }

    std::vector<VkImageView> getSwapChainImageViews() const { return swapChainImageViews; }
    VkFormat getSwapChainImageFormat() const { return swapChainImageFormat; }
    VkExtent2D getSwapChainExtent() const { return swapChainExtent; }
    VkImageView getDepthImageView() const { return depthImageView; }

private:
    Device& device;

    VkSwapchainKHR swapChain;
    std::vector<VkImage> swapChainImages;
    std::vector<VkImageView> swapChainImageViews;
    VkFormat swapChainImageFormat;
    VkExtent2D swapChainExtent;
    VkSurfaceTransformFlagBitsKHR pretransformFlag;

    VkImage depthImage;
    VkDeviceMemory depthImageMemory;
    VkImageView depthImageView;

    void createImageViews();
    void createSwapChain();

    VkExtent2D getDisplaySizeIdentity();

    void createDepthResources();
    void destroyDepthResources();
    void createImage(VkFormat format, VkImage &image, VkDeviceMemory &imageMemory);

    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
};

SwapChain::SwapChain(Device &device)  : device(device) {
    createSwapChain();
    createImageViews();
    createDepthResources();
}

void SwapChain::createImageViews() {
    swapChainImageViews.resize(swapChainImages.size());
    for (size_t i = 0; i < swapChainImages.size(); i++) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = swapChainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = swapChainImageFormat;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;
        VK_CHECK(vkCreateImageView(device.getDevice(), &createInfo, nullptr,
                                   &swapChainImageViews[i]));
    }
}

void SwapChain::createSwapChain() {
    SwapChainSupportDetails swapChainSupport =
            device.querySwapChainSupport(device.getPhysicalDevice());

    auto chooseSwapSurfaceFormat =
            [](const std::vector<VkSurfaceFormatKHR> &availableFormats) {
                for (const auto &availableFormat : availableFormats) {
                    if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
                        availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                        return availableFormat;
                    }
                }
                return availableFormats[0];
            };

    VkSurfaceFormatKHR surfaceFormat =
            chooseSwapSurfaceFormat(swapChainSupport.formats);

    // Please check
    // https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkPresentModeKHR.html
    // for a discourse on different present modes.
    //
    // VK_PRESENT_MODE_FIFO_KHR = Hard Vsync
    // This is always supported on Android phones
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 &&
        imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    pretransformFlag = swapChainSupport.capabilities.currentTransform;
    auto displaySizeIdentity = getDisplaySizeIdentity();

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = device.getSurface();
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = displaySizeIdentity;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.preTransform = pretransformFlag;

    QueueFamilyIndices indices = device.findQueueFamilies(device.getPhysicalDevice());
    uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(),
                                     indices.presentFamily.value()};

    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = nullptr;
    }
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    VK_CHECK(vkCreateSwapchainKHR(device.getDevice(), &createInfo, nullptr, &swapChain));

    vkGetSwapchainImagesKHR(device.getDevice(), swapChain, &imageCount, nullptr);
    swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device.getDevice(), swapChain, &imageCount,
                            swapChainImages.data());

    swapChainImageFormat = surfaceFormat.format;
    swapChainExtent = displaySizeIdentity;
}

VkExtent2D SwapChain::getDisplaySizeIdentity() {
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device.getPhysicalDevice(), device.getSurface(),
                                              &capabilities);

    uint32_t width = capabilities.currentExtent.width;
    uint32_t height = capabilities.currentExtent.height;
    if (capabilities.currentTransform & VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR ||
        capabilities.currentTransform & VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR) {
        // Swap to get identity width and height
        capabilities.currentExtent.height = width;
        capabilities.currentExtent.width = height;
    }

    return capabilities.currentExtent;
}

void SwapChain::createDepthResources() {
    VkFormat depthFormat = device.findDepthFormat();

    createImage(depthFormat, depthImage, depthImageMemory);
    depthImageView = createImageView(depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
}

void SwapChain::createImage(VkFormat format, VkImage& image, VkDeviceMemory& imageMemory) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = swapChainExtent.width;
    imageInfo.extent.height = swapChainExtent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK(vkCreateImage(device.getDevice(), &imageInfo, nullptr, &image));

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device.getDevice(), image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = device.findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VK_CHECK(vkAllocateMemory(device.getDevice(), &allocInfo, nullptr, &imageMemory));
    vkBindImageMemory(device.getDevice(), image, imageMemory, 0);
}

VkImageView SwapChain::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    VK_CHECK(vkCreateImageView(device.getDevice(), &viewInfo, nullptr, &imageView));
    return imageView;
}

void SwapChain::destroyDepthResources() {
    vkDestroyImageView(device.getDevice(), depthImageView, nullptr);
    vkDestroyImage(device.getDevice(), depthImage, nullptr);
    vkFreeMemory(device.getDevice(), depthImageMemory, nullptr);
}

SwapChain::~SwapChain() {
    destroyDepthResources();

    for (size_t i = 0; i < swapChainImageViews.size(); i++) {
        vkDestroyImageView(device.getDevice(), swapChainImageViews[i], nullptr);
    }

    vkDestroySwapchainKHR(device.getDevice(), swapChain, nullptr);
}

