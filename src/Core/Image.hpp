#pragma once

#include <vk_mem_alloc.h>

#include "Hash.hpp"
#include "Buffer.hpp"

namespace RoseEngine {

struct PixelData {
	BufferView mData;
	vk::Format mFormat;
	vk::Extent3D mExtent;
};
PixelData LoadImageFile(Device& device, const std::filesystem::path& filename, const bool srgb = true, int desiredChannels = 0);

inline vk::Extent3D GetLevelExtent(const vk::Extent3D& extent, const uint32_t level = 0) {
	uint32_t s = 1 << level;
	return vk::Extent3D(std::max(extent.width / s, 1u), std::max(extent.height / s, 1u), std::max(extent.depth / s, 1u));
}

struct ImageInfo {
	vk::ImageCreateFlags    mCreateFlags = {};
	vk::ImageType           mType = vk::ImageType::e2D;
	vk::Format              mFormat;
	vk::Extent3D            mExtent;
	uint32_t                mLevels = 1;
	uint32_t                mLayers = 1;
	vk::SampleCountFlagBits mSamples = vk::SampleCountFlagBits::e1;
	vk::ImageUsageFlags     mUsage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
	vk::ImageTiling         mTiling = vk::ImageTiling::eOptimal;
	vk::SharingMode         mSharingMode = vk::SharingMode::eExclusive;
	std::vector<uint32_t>   mQueueFamilies;
};

struct Image {
	vk::Image     mImage = nullptr;
	VmaAllocator  mMemoryAllocator = nullptr;
	VmaAllocation mAllocation = nullptr;
	ImageInfo     mInfo;
	TupleMap<vk::raii::ImageView, vk::ImageSubresourceRange, vk::ImageViewType, vk::ComponentMapping> mViews;

	using SubresourceLayoutState = std::tuple<vk::ImageLayout, vk::PipelineStageFlags, vk::AccessFlags, uint32_t /*queueFamily*/>;
	std::vector<std::vector<SubresourceLayoutState>> mSubresourceStates; // mSubresourceStates[arrayLayer][level]

	Image(Device& device, const ImageInfo& info, const vk::MemoryPropertyFlags memoryFlags = vk::MemoryPropertyFlagBits::eDeviceLocal, const VmaAllocationCreateFlags allocationFlags = VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT);
	Image(const vk::Image image, const ImageInfo& info);
	~Image();

	inline       vk::Image& operator*()        { return mImage; }
	inline const vk::Image& operator*() const  { return mImage; }
	inline       vk::Image* operator->()       { return &mImage; }
	inline const vk::Image* operator->() const { return &mImage; }

	const vk::ImageView GetView(const Device& device, const vk::ImageSubresourceRange& subresource, const vk::ImageViewType viewType = vk::ImageViewType::e2D, const vk::ComponentMapping& componentMapping = {});

	inline const SubresourceLayoutState& GetSubresourceState(const uint32_t arrayLayer, const uint32_t level) const {
		return mSubresourceStates[arrayLayer][level];
	}
	inline void SetSubresourceState(const vk::ImageSubresourceRange& subresource, const SubresourceLayoutState& newState) {
		const uint32_t maxLayer = std::min(mInfo.mLayers, subresource.baseArrayLayer + subresource.layerCount);
		const uint32_t maxLevel = std::min(mInfo.mLevels, subresource.baseMipLevel   + subresource.levelCount);
		for (uint32_t arrayLayer = subresource.baseArrayLayer; arrayLayer < maxLayer; arrayLayer++) {
			for (uint32_t level = subresource.baseMipLevel; level < maxLevel; level++) {
				mSubresourceStates[arrayLayer][level] = newState;
			}
		}
	}
	inline void SetSubresourceState(const vk::ImageSubresourceRange& subresource, const vk::ImageLayout layout, const vk::PipelineStageFlags stage, const vk::AccessFlags accessMask, const uint32_t queueFamily = VK_QUEUE_FAMILY_IGNORED) {
		SetSubresourceState(subresource, { layout, stage, accessMask, queueFamily });
	}
};

struct ImageView {
	vk::ImageView mView;
	std::shared_ptr<Image> mImage;
	vk::ImageSubresourceRange mSubresource;
	vk::ImageViewType mType;
	vk::ComponentMapping mComponentMapping;

	ImageView() = default;
	ImageView(const ImageView&) = default;
	ImageView(ImageView&&) = default;
	inline ImageView(const Device& device, const std::shared_ptr<Image>& image, const vk::ImageSubresourceRange& subresource = { vk::ImageAspectFlagBits::eColor, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS }, vk::ImageViewType viewType = vk::ImageViewType::e2D, const vk::ComponentMapping& componentMapping = {})
		: mImage(image), mSubresource(subresource), mType(viewType), mComponentMapping(componentMapping) {
		if (image) {
			if (IsDepthStencil(image->mInfo.mFormat))
				mSubresource.aspectMask = vk::ImageAspectFlagBits::eDepth;
			if (mSubresource.levelCount == VK_REMAINING_MIP_LEVELS)   mSubresource.levelCount = image->mInfo.mLevels;
			if (mSubresource.layerCount == VK_REMAINING_ARRAY_LAYERS) mSubresource.layerCount = image->mInfo.mLayers;
			mView = image->GetView(device, mSubresource, mType, mComponentMapping);
		}
	}
	ImageView& operator=(const ImageView&) = default;
	ImageView& operator=(ImageView&& v) = default;

	inline       vk::ImageView& operator*()        { return mView; }
	inline const vk::ImageView& operator*() const  { return mView; }
	inline       vk::ImageView* operator->()       { return &mView; }
	inline const vk::ImageView* operator->() const { return &mView; }

	inline bool operator==(const ImageView& rhs) const { return mView == rhs.mView; }
	inline bool operator!=(const ImageView& rhs) const { return mView != rhs.mView; }

	inline vk::ImageSubresourceLayers GetSubresourceLayer(const uint32_t levelOffset = 0) const {
		return vk::ImageSubresourceLayers(mSubresource.aspectMask, mSubresource.baseMipLevel + levelOffset, mSubresource.baseArrayLayer, mSubresource.layerCount);
	}
	inline void SetSubresourceState(const Image::SubresourceLayoutState& newState) const {
		mImage->SetSubresourceState(mSubresource, newState);
	}
	inline void SetSubresourceState(const vk::ImageLayout layout, const vk::PipelineStageFlags stage, const vk::AccessFlags accessMask, const uint32_t queueFamily = VK_QUEUE_FAMILY_IGNORED) const {
		mImage->SetSubresourceState(mSubresource, { layout, stage, accessMask, queueFamily });
	}
};

}

namespace std {

template<>
struct hash<RoseEngine::ImageView> {
	inline size_t operator()(const RoseEngine::ImageView& v) const {
		return hash<vk::ImageView>()(*v);
	}
};

}
