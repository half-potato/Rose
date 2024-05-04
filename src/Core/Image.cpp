#include "Image.hpp"
#include "Buffer.hpp"
#include <iostream>

namespace RoseEngine {

std::vector<std::vector<Image::ResourceState>> CreateSubresourceStates(const ImageInfo& info) {
	return std::vector<std::vector<Image::ResourceState>>(
		info.arrayLayers,
		std::vector<Image::ResourceState>(
			info.mipLevels,
			Image::ResourceState{
				.layout = vk::ImageLayout::eUndefined,
				.stage  = vk::PipelineStageFlagBits2::eTopOfPipe,
				.access = vk::AccessFlagBits2::eNone,
				.queueFamily = info.queueFamilies.empty() ? VK_QUEUE_FAMILY_IGNORED : info.queueFamilies.front() }));
}

ref<Image> Image::Create(Device& device, const ImageInfo& info, const vk::MemoryPropertyFlags memoryFlags, const VmaAllocationCreateFlags allocationFlags) {
	VmaAllocationCreateInfo allocationCreateInfo {
		.flags = allocationFlags,
		.usage = VMA_MEMORY_USAGE_AUTO,
		.requiredFlags = (VkMemoryPropertyFlags)memoryFlags,
		.memoryTypeBits = 0,
		.pool = VK_NULL_HANDLE,
		.pUserData = VK_NULL_HANDLE,
		.priority = 0 };

	vk::ImageCreateInfo createInfo{
		.flags       = info.createFlags,
		.imageType   = info.type,
		.format      = info.format,
		.extent      = vk::Extent3D{info.extent.x, info.extent.y, info.extent.z},
		.mipLevels   = info.mipLevels,
		.arrayLayers = info.arrayLayers,
		.samples     = info.samples,
		.tiling      = info.tiling,
		.usage       = info.usage,
		.sharingMode = info.sharingMode,
		.initialLayout = vk::ImageLayout::eUndefined };
	createInfo.setQueueFamilyIndices(info.queueFamilies);

	VkImage vkimg;
	VmaAllocation alloc;
	vk::Result result = (vk::Result)vmaCreateImage(device.MemoryAllocator(), &(const VkImageCreateInfo&)createInfo, &allocationCreateInfo, &vkimg, &alloc, nullptr);
	if (result != vk::Result::eSuccess) {
		std::cerr << "Failed to create image: " << vk::to_string(result) << std::endl;
		return nullptr;
	}

	auto image = make_ref<Image>();
	image->mImage = vkimg;
	image->mDevice = **device;
	image->mMemoryAllocator = device.MemoryAllocator();
	image->mAllocation = alloc;
	image->mInfo = info;
	image->mSubresourceStates = CreateSubresourceStates(info);
	return image;
}
ref<Image> Image::Create(const vk::Device device, const vk::Image vkimage, const ImageInfo& info) {
	auto image = make_ref<Image>();
	image->mImage = vkimage;
	image->mDevice = device;
	image->mMemoryAllocator = nullptr;
	image->mAllocation = nullptr;
	image->mInfo = info;
	image->mSubresourceStates = CreateSubresourceStates(info);
	return image;
}
Image::~Image() {
	for (auto[key, v] : mCachedViews)
		mDevice.destroyImageView(v);
	if (mMemoryAllocator && mImage && mAllocation) {
		vmaDestroyImage(mMemoryAllocator, mImage, mAllocation);
		mMemoryAllocator = nullptr;
		mImage = nullptr;
		mAllocation = nullptr;
	}
}

ImageView ImageView::Create(const ref<Image>& image, const vk::ImageSubresourceRange& subresource, const vk::ImageViewType type, const vk::ComponentMapping& componentMapping) {
	if (!image) return {};
	auto key = std::tie(subresource, type, componentMapping);
	auto it = image->mCachedViews.find(key);
	if (it == image->mCachedViews.end()) {
		vk::ImageView v = image->mDevice.createImageView(vk::ImageViewCreateInfo{
			.image = **image,
			.viewType = type,
			.format = image->Info().format,
			.components = componentMapping,
			.subresourceRange = subresource });
		it = image->mCachedViews.emplace(key, v).first;
	}
	return ImageView{
		.mView = it->second,
		.mImage = image,
		.mSubresource = subresource,
		.mType = type,
		.mComponentMapping = componentMapping
	};
}

}