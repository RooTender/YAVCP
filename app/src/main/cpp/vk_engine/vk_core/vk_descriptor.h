#include "vk_swapchain.h"

class Descriptor {
public:
    Descriptor(Device& device, std::vector<VkBuffer>& uniformBuffers);
    ~Descriptor();

    VkDescriptorSetLayout& getDescriptorSetLayout() { return descriptorSetLayout; }
    std::vector<VkDescriptorSet> getDescriptorSets() const { return  descriptorSets; }

private:
    Device& device;

    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorPool descriptorPool;
    std::vector<VkDescriptorSet> descriptorSets;

    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createDescriptorSets(std::vector<VkBuffer>& uniformBuffer);
};

Descriptor::Descriptor(Device& device, std::vector<VkBuffer>& uniformBuffers) : device(device) {
    createDescriptorSetLayout();
    createDescriptorPool();
    createDescriptorSets(uniformBuffers);
}

void Descriptor::createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboLayoutBinding;

    VK_CHECK(vkCreateDescriptorSetLayout(device.getDevice(), &layoutInfo, nullptr,
                                         &descriptorSetLayout));
}

void Descriptor::createDescriptorPool() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    VK_CHECK(vkCreateDescriptorPool(device.getDevice(), &poolInfo, nullptr, &descriptorPool));
}

void Descriptor::createDescriptorSets(std::vector<VkBuffer>& uniformBuffers) {
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT,
                                               descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();

    descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    VK_CHECK(vkAllocateDescriptorSets(device.getDevice(), &allocInfo, descriptorSets.data()));

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = descriptorSets[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(device.getDevice(), 1, &descriptorWrite, 0, nullptr);
    }
}

Descriptor::~Descriptor() {
    vkDestroyDescriptorPool(device.getDevice(), descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device.getDevice(), descriptorSetLayout, nullptr);
}