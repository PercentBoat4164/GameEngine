#pragma once

#include <vector>
#include <deque>
#include <functional>

#include <vk_mem_alloc.h>

#include "vulkanGraphicsEngineLink.hpp"
#include "bufferManager.hpp"

/** These are a few variables set for the images.*/
enum ImageType {
    DEPTH = 0,
    COLOR = 1,
    TEXTURE = 2
};

/** This is the ImageManager class*/
class ImageManager {
public:
    /** This is a Vulkan image called image{}.*/
    VkImage image{};
    /** This is a Vulkan image view called view{}*/
    VkImageView view{};
    /** This is a Vulkan sampler called sampler{}.*/
    VkSampler sampler{};
    /** This is a Vulkan format called imageFormat{}.*/
    VkFormat imageFormat{};
    /** This is a Vulkan image layout called imageLayout{}.*/
    VkImageLayout imageLayout{};

    /** This method destroys the items in the deletion queue and clears the deletion queue.*/
    void destroy() {
        for (std::function<void()>& function : deletionQueue) { function(); }
        deletionQueue.clear();
    }

    /** This method sets the graphics engine link.
     * @param engineLink This is the Vulkan graphics engine that is being linked.*/
    void setEngineLink(VulkanGraphicsEngineLink *engineLink) {
        linkedRenderEngine = engineLink;
    }

    /** This creates the image manager.
     * @param format This is the Vulkan format.
     * @param tiling This is the Vulkan image tilting.
     * @param msaaSamples This is the Vulkan sample count flag bits.
     * @param usage This is the Vulkan image usage.
     * @param allocationUsage This is the Vulkan memory allocation usage.
     * @param mipLevels This is the mip levels of the image.
     * @param width This is the width of the image.
     * @param height This is the height of the image.
     * @param imageType This is the type of image.
     * @param dataSource This is the data source.*/
    void create(VkFormat format, VkImageTiling tiling, VkSampleCountFlagBits msaaSamples, VkImageUsageFlags usage, VmaMemoryUsage allocationUsage, int mipLevels, int width, int height, ImageType imageType, BufferManager *dataSource = nullptr) {
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
        vmaCreateImage(*linkedRenderEngine->allocator, &imageCreateInfo, &allocationCreateInfo, &image, &allocation, nullptr);
        deletionQueue.emplace_front([&]{ if(image != VK_NULL_HANDLE) { vmaDestroyImage(*linkedRenderEngine->allocator, image, allocation); image = VK_NULL_HANDLE; } });
        imageFormat = format;
        imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageViewCreateInfo imageViewCreateInfo{};
        imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        imageViewCreateInfo.image = image;
        imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imageViewCreateInfo.format = format;
        imageViewCreateInfo.subresourceRange.aspectMask = imageType == ImageType::DEPTH ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
        imageViewCreateInfo.subresourceRange.levelCount = 1;
        imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
        imageViewCreateInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(linkedRenderEngine->device->device, &imageViewCreateInfo, nullptr, &view) != VK_SUCCESS) { throw std::runtime_error("failed to create texture image view!"); }
        deletionQueue.emplace_front([&] { vkDestroyImageView(linkedRenderEngine->device->device, view, nullptr); view = VK_NULL_HANDLE; });
        if (dataSource != nullptr) {
            transition(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            dataSource->toImage(image, width, height);
            if (imageType == ImageType::TEXTURE) {
                VkSamplerCreateInfo samplerInfo{};
                samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
                samplerInfo.magFilter = VK_FILTER_LINEAR;
                samplerInfo.minFilter = VK_FILTER_LINEAR;
                samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                samplerInfo.anisotropyEnable = linkedRenderEngine->settings->anisotropicFilterLevel > 0 ? VK_TRUE : VK_FALSE;
                samplerInfo.maxAnisotropy = linkedRenderEngine->settings->anisotropicFilterLevel;
                samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
                samplerInfo.unnormalizedCoordinates = VK_FALSE;
                samplerInfo.compareEnable = VK_FALSE;
                samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
                samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
                samplerInfo.mipLodBias = 0.0f;
                samplerInfo.minLod = 0.0f;
                samplerInfo.maxLod = 0.0f;
                if (vkCreateSampler(linkedRenderEngine->device->device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) { throw std::runtime_error("failed to create texture sampler!"); }
                deletionQueue.emplace_front([&] { vkDestroySampler(linkedRenderEngine->device->device, sampler, nullptr); sampler = VK_NULL_HANDLE; });
                transition(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
        }
    }

    /** I still don't know what this is...
     * @todo @PercentBoat4164 tell @JoelLogan what this is and does.*/
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
        VkCommandBuffer commandBuffer = linkedRenderEngine->beginSingleTimeCommands();
        vkCmdCopyImageToBuffer(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, buffer, 1, &region);
        linkedRenderEngine->endSingleTimeCommands(commandBuffer);
    }

    /** This method transitions from one layout to another layout.
     * @param oldLayout This is the old layout.
     * @param newLayout This is the new layout that will be set.*/
    void transition(VkImageLayout oldLayout, VkImageLayout newLayout) {
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
        VkCommandBuffer commandBuffer = linkedRenderEngine->beginSingleTimeCommands();
        vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        linkedRenderEngine->endSingleTimeCommands(commandBuffer);
        imageLayout = newLayout;
    }

private:
    /** This variable holds the items that will be deleted.*/
    std::deque<std::function<void()>> deletionQueue{};
    /** This is the graphics engine link.*/
    VulkanGraphicsEngineLink *linkedRenderEngine{};
    /** This is the Vulkan memory allocation.*/
    VmaAllocation allocation{};
};
