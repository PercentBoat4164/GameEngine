#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>
#include <glm/glm.hpp>

#include <vk-bootstrap/src/VkBootstrap.h>

#include <vector>
#include <deque>
#include <functional>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

struct RenderEngineLink {
    Settings *settings = nullptr;
    vkb::Device *device;
    VkCommandPool *commandPool;
    VmaAllocator *allocator;

    [[nodiscard]] VkCommandBuffer beginSingleTimeCommands() const {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = *commandPool;
        allocInfo.commandBufferCount = 1;
        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers((*device).device, &allocInfo, &commandBuffer);
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commandBuffer, &beginInfo);
        return commandBuffer;
    }

    void endSingleTimeCommands(VkCommandBuffer commandBuffer) const {
        vkEndCommandBuffer(commandBuffer);
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        vkQueueSubmit((*device).get_queue(vkb::QueueType::graphics).value(), 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle((*device).get_queue(vkb::QueueType::graphics).value());
        vkFreeCommandBuffers((*device).device, *commandPool, 1, &commandBuffer);
    }
};

struct Camera {
    std::array<glm::mat4, 2> update() {
        front = glm::normalize(glm::vec3{cos(glm::radians(yaw)) * cos(glm::radians(pitch)), sin(glm::radians(yaw)) * cos(glm::radians(pitch)), sin(glm::radians(pitch))});
        right = glm::normalize(glm::cross(front, up));
        view = {glm::lookAt(position, position + front, up)};
        proj = {glm::perspective(glm::radians(fov), double(resolution[0]) / std::max(resolution[1], 1), 0.0001, renderDistance)};
        proj[1][1] *= -1;
        return {view, proj};
    }

    float movementSpeed{2.5};
    glm::vec3 position{0, 0, 2};
    glm::vec3 front{0, 1, 0};
    glm::vec3 up{0, 0, 1};
    glm::vec3 right{glm::cross(front, up)};
    float yaw{-90};
    float pitch{};
    std::array<int, 2> resolution{};
    double renderDistance{10};
    double fov{90};
    glm::mat4 view{glm::lookAt(position, position + front, glm::vec3(0.0f, 0.0f, 1.0f))};
    glm::mat4 proj{glm::perspective(glm::radians(fov), double(resolution[0]) / std::max(resolution[1], 1), 0.0001, renderDistance)};
    double mouseSensitivity{0.1};
};

struct AllocatedBuffer {
    void *data{};
    VkBuffer buffer{};

    void destroy() {
        for (std::function<void()> &function : deletionQueue) { function(); }
        deletionQueue.clear();
        data = nullptr;
    }

    void setEngineLink(const RenderEngineLink& renderEngineLink) {
        linkedRenderEngine = renderEngineLink;
    }

    void *create(VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage allocationUsage) {
        VkBufferCreateInfo bufferCreateInfo{};
        bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferCreateInfo.size = size;
        bufferCreateInfo.usage = usage;
        VmaAllocationCreateInfo allocationCreateInfo{};
        allocationCreateInfo.usage = allocationUsage;
        if (vmaCreateBuffer(*linkedRenderEngine.allocator, &bufferCreateInfo, &allocationCreateInfo, &buffer, &allocation, nullptr) != VK_SUCCESS) { throw std::runtime_error("failed to create buffer"); }
        deletionQueue.emplace_front([&]{ if (buffer != VK_NULL_HANDLE) { vmaDestroyBuffer(*linkedRenderEngine.allocator, buffer, allocation); buffer = VK_NULL_HANDLE; } });
        vmaMapMemory(*linkedRenderEngine.allocator, allocation, &data);
        deletionQueue.emplace_front([&]{ vmaUnmapMemory(*linkedRenderEngine.allocator, allocation); });
        return data;
    }

    void toImage(VkImage image, uint32_t width, uint32_t height) const {
        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {width, height, 1};
        VkCommandBuffer commandBuffer = linkedRenderEngine.beginSingleTimeCommands();
        vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        linkedRenderEngine.endSingleTimeCommands(commandBuffer);
    }

private:
    std::deque<std::function<void()>> deletionQueue{};
    RenderEngineLink linkedRenderEngine{};
    VmaAllocation allocation{};
};

struct AllocatedImage {
    VkImage image{};
    VkImageView view{};
    VkSampler sampler{};

    void destroy() {
        for (std::function<void()>& function : deletionQueue) { function(); }
        deletionQueue.clear();
    }

    void setEngineLink(const RenderEngineLink& renderEngineLink) {
        linkedRenderEngine = renderEngineLink;
    }

    void create(VkFormat format, VkImageTiling tiling, VkSampleCountFlagBits msaaSamples, VkImageUsageFlags usage, VmaMemoryUsage allocationUsage, int mipLevels, int width, int height, bool depth, bool asTexture, AllocatedBuffer *dataSource = nullptr) {
        VkImageCreateInfo imageCreateInfo{};
        imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        imageCreateInfo.extent.width = width;
        imageCreateInfo.extent.height = height;
        imageCreateInfo.extent.depth = 1;
        imageCreateInfo.mipLevels = mipLevels;
        imageCreateInfo.arrayLayers = 1;
        imageCreateInfo.format = format;
        imageCreateInfo.tiling = tiling;
        imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageCreateInfo.usage = usage;
        imageCreateInfo.samples = msaaSamples;
        imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo allocationCreateInfo{};
        allocationCreateInfo.usage = allocationUsage;
        vmaCreateImage(*linkedRenderEngine.allocator, &imageCreateInfo, &allocationCreateInfo, &image, &allocation, nullptr);
        deletionQueue.emplace_front([&]{ if(image != VK_NULL_HANDLE) { vmaDestroyImage(*linkedRenderEngine.allocator, image, allocation); image = VK_NULL_HANDLE; } });
        VkImageViewCreateInfo imageViewCreateInfo{};
        imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        imageViewCreateInfo.image = image;
        imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imageViewCreateInfo.format = format;
        imageViewCreateInfo.subresourceRange.aspectMask = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
        imageViewCreateInfo.subresourceRange.levelCount = 1;
        imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
        imageViewCreateInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView((*linkedRenderEngine.device).device, &imageViewCreateInfo, nullptr, &view) != VK_SUCCESS) { throw std::runtime_error("failed to create texture image view!"); }
        deletionQueue.emplace_front([&] { vkDestroyImageView((*linkedRenderEngine.device).device, view, nullptr); view = VK_NULL_HANDLE; });
        if (dataSource != nullptr) {
            transition(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            dataSource->toImage(image, width, height);
            if (asTexture) {
                VkSamplerCreateInfo samplerInfo{};
                samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
                samplerInfo.magFilter = VK_FILTER_LINEAR;
                samplerInfo.minFilter = VK_FILTER_LINEAR;
                samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                samplerInfo.anisotropyEnable = (*linkedRenderEngine.settings).anisotropicFilterLevel > 0 ? VK_TRUE : VK_FALSE;
                samplerInfo.maxAnisotropy = linkedRenderEngine.settings->anisotropicFilterLevel;
                samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
                samplerInfo.unnormalizedCoordinates = VK_FALSE;
                samplerInfo.compareEnable = VK_FALSE;
                samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
                samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
                samplerInfo.mipLodBias = 0.0f;
                samplerInfo.minLod = 0.0f;
                samplerInfo.maxLod = 0.0f;
                if (vkCreateSampler((*linkedRenderEngine.device).device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) { throw std::runtime_error("failed to create texture sampler!"); }
                deletionQueue.emplace_front([&] { vkDestroySampler((*linkedRenderEngine.device).device, sampler, nullptr); sampler = VK_NULL_HANDLE; });
                transition(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
        }
    }

    [[maybe_unused]] void toBuffer(VkBuffer buffer, uint32_t width, uint32_t height) const {
        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {width, height, 1};
        VkCommandBuffer commandBuffer = linkedRenderEngine.beginSingleTimeCommands();
        vkCmdCopyImageToBuffer(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, buffer, 1, &region);
        linkedRenderEngine.endSingleTimeCommands(commandBuffer);
    }

    void transition(VkImageLayout oldLayout, VkImageLayout newLayout) const {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        VkPipelineStageFlags sourceStage;
        VkPipelineStageFlags destinationStage;
        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        } else { throw std::invalid_argument("unsupported layout transition!"); }
        VkCommandBuffer commandBuffer = linkedRenderEngine.beginSingleTimeCommands();
        vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        linkedRenderEngine.endSingleTimeCommands(commandBuffer);
    }

private:
    std::deque<std::function<void()>> deletionQueue{};
    RenderEngineLink linkedRenderEngine{};
    VmaAllocation allocation{};
};

struct UniformBufferObject {
    alignas(16) glm::mat4 model{};
    alignas(16) glm::mat4 view{};
    alignas(16) glm::mat4 proj{};
};

struct DescriptorSetManager {
    VkDescriptorSetLayout descriptorSetLayout{};
    VkDescriptorPool descriptorPool{};
    VkDescriptorSet imagesDescriptorSet{};
    VkDescriptorSet cameraDescriptorSet{};
    VkDescriptorSet sceneDataDescriptorSet{};

    AllocatedBuffer lightsBuffer{};
    AllocatedBuffer uniformBuffer{};
    AllocatedImage albedo{};
    AllocatedImage merh{};
    AllocatedImage normal{};

    RenderEngineLink linkedRenderEngine{};

    void createDescriptorPool() {
        VkDescriptorPoolSize uboDescriptorPoolSize{};
        uboDescriptorPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    }

    void allocatedDescriptorSets() {

    }

    void updateDescriptorSets() {

    }
};

struct Vertex {
    glm::vec3 pos{};
    glm::vec3 color{};
    glm::vec2 texCoord{};
    glm::vec3 normal{};

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription;
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, pos);
        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, color);
        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[2].offset = offsetof(Vertex, texCoord);
        attributeDescriptions[3].binding = 0;
        attributeDescriptions[3].location = 3;
        attributeDescriptions[3].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[3].offset = offsetof(Vertex, texCoord);
        return attributeDescriptions;
    }

    bool operator==(const Vertex &other) const {return pos == other.pos && color == other.color && texCoord == other.texCoord;}
};

namespace std {template<> struct hash<Vertex> {size_t operator()(Vertex const& vertex) const {return ((hash<glm::vec3>()(vertex.pos) ^ (hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^ (hash<glm::vec2>()(vertex.texCoord) << 1);}};}
