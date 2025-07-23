#pragma once

#include "MathTypes.hpp"
#include "Buffer.hpp"

namespace RoseEngine {

inline uint32_t GetMaxMipLevels(const uint3& extent) {
	return 32 - (uint32_t)std::countl_zero(std::max(std::max(extent.x, extent.y), extent.z));
}

inline uint3 GetLevelExtent(const uint3& extent, const uint32_t level = 0) {
	uint32_t s = 1 << level;
	return uint3(std::max(extent.x / s, 1u), std::max(extent.y / s, 1u), std::max(extent.z / s, 1u));
}

inline constexpr bool IsDepthStencil(vk::Format format) {
	return
		format == vk::Format::eS8Uint ||
		format == vk::Format::eD16Unorm ||
		format == vk::Format::eD16UnormS8Uint ||
		format == vk::Format::eX8D24UnormPack32 ||
		format == vk::Format::eD24UnormS8Uint ||
		format == vk::Format::eD32Sfloat ||
		format == vk::Format::eD32SfloatS8Uint;
}

// Size of an element of format, in bytes
template<typename T = uint32_t> requires(std::is_arithmetic_v<T>)
inline constexpr T GetTexelSize(vk::Format format) {
	switch (format) {
	default:
		throw std::runtime_error("Texel size unknown for format " + vk::to_string(format));
	case vk::Format::eR4G4UnormPack8:
	case vk::Format::eR8Unorm:
	case vk::Format::eR8Snorm:
	case vk::Format::eR8Uscaled:
	case vk::Format::eR8Sscaled:
	case vk::Format::eR8Uint:
	case vk::Format::eR8Sint:
	case vk::Format::eR8Srgb:
	case vk::Format::eS8Uint:
		return 1;

	case vk::Format::eR4G4B4A4UnormPack16:
	case vk::Format::eB4G4R4A4UnormPack16:
	case vk::Format::eR5G6B5UnormPack16:
	case vk::Format::eB5G6R5UnormPack16:
	case vk::Format::eR5G5B5A1UnormPack16:
	case vk::Format::eB5G5R5A1UnormPack16:
	case vk::Format::eA1R5G5B5UnormPack16:
	case vk::Format::eR8G8Unorm:
	case vk::Format::eR8G8Snorm:
	case vk::Format::eR8G8Uscaled:
	case vk::Format::eR8G8Sscaled:
	case vk::Format::eR8G8Uint:
	case vk::Format::eR8G8Sint:
	case vk::Format::eR8G8Srgb:
	case vk::Format::eR16Unorm:
	case vk::Format::eR16Snorm:
	case vk::Format::eR16Uscaled:
	case vk::Format::eR16Sscaled:
	case vk::Format::eR16Uint:
	case vk::Format::eR16Sint:
	case vk::Format::eR16Sfloat:
	case vk::Format::eD16Unorm:
		return 2;

	case vk::Format::eR8G8B8Unorm:
	case vk::Format::eR8G8B8Snorm:
	case vk::Format::eR8G8B8Uscaled:
	case vk::Format::eR8G8B8Sscaled:
	case vk::Format::eR8G8B8Uint:
	case vk::Format::eR8G8B8Sint:
	case vk::Format::eR8G8B8Srgb:
	case vk::Format::eB8G8R8Unorm:
	case vk::Format::eB8G8R8Snorm:
	case vk::Format::eB8G8R8Uscaled:
	case vk::Format::eB8G8R8Sscaled:
	case vk::Format::eB8G8R8Uint:
	case vk::Format::eB8G8R8Sint:
	case vk::Format::eB8G8R8Srgb:
	case vk::Format::eD16UnormS8Uint:
		return 3;

	case vk::Format::eR8G8B8A8Unorm:
	case vk::Format::eR8G8B8A8Snorm:
	case vk::Format::eR8G8B8A8Uscaled:
	case vk::Format::eR8G8B8A8Sscaled:
	case vk::Format::eR8G8B8A8Uint:
	case vk::Format::eR8G8B8A8Sint:
	case vk::Format::eR8G8B8A8Srgb:
	case vk::Format::eB8G8R8A8Unorm:
	case vk::Format::eB8G8R8A8Snorm:
	case vk::Format::eB8G8R8A8Uscaled:
	case vk::Format::eB8G8R8A8Sscaled:
	case vk::Format::eB8G8R8A8Uint:
	case vk::Format::eB8G8R8A8Sint:
	case vk::Format::eB8G8R8A8Srgb:
	case vk::Format::eA8B8G8R8UnormPack32:
	case vk::Format::eA8B8G8R8SnormPack32:
	case vk::Format::eA8B8G8R8UscaledPack32:
	case vk::Format::eA8B8G8R8SscaledPack32:
	case vk::Format::eA8B8G8R8UintPack32:
	case vk::Format::eA8B8G8R8SintPack32:
	case vk::Format::eA8B8G8R8SrgbPack32:
	case vk::Format::eA2R10G10B10UnormPack32:
	case vk::Format::eA2R10G10B10SnormPack32:
	case vk::Format::eA2R10G10B10UscaledPack32:
	case vk::Format::eA2R10G10B10SscaledPack32:
	case vk::Format::eA2R10G10B10UintPack32:
	case vk::Format::eA2R10G10B10SintPack32:
	case vk::Format::eA2B10G10R10UnormPack32:
	case vk::Format::eA2B10G10R10SnormPack32:
	case vk::Format::eA2B10G10R10UscaledPack32:
	case vk::Format::eA2B10G10R10SscaledPack32:
	case vk::Format::eA2B10G10R10UintPack32:
	case vk::Format::eA2B10G10R10SintPack32:
	case vk::Format::eR16G16Unorm:
	case vk::Format::eR16G16Snorm:
	case vk::Format::eR16G16Uscaled:
	case vk::Format::eR16G16Sscaled:
	case vk::Format::eR16G16Uint:
	case vk::Format::eR16G16Sint:
	case vk::Format::eR16G16Sfloat:
	case vk::Format::eR32Uint:
	case vk::Format::eR32Sint:
	case vk::Format::eR32Sfloat:
	case vk::Format::eD24UnormS8Uint:
	case vk::Format::eD32Sfloat:
		return 4;

	case vk::Format::eD32SfloatS8Uint:
		return 5;

	case vk::Format::eR16G16B16Unorm:
	case vk::Format::eR16G16B16Snorm:
	case vk::Format::eR16G16B16Uscaled:
	case vk::Format::eR16G16B16Sscaled:
	case vk::Format::eR16G16B16Uint:
	case vk::Format::eR16G16B16Sint:
	case vk::Format::eR16G16B16Sfloat:
		return 6;

	case vk::Format::eR16G16B16A16Unorm:
	case vk::Format::eR16G16B16A16Snorm:
	case vk::Format::eR16G16B16A16Uscaled:
	case vk::Format::eR16G16B16A16Sscaled:
	case vk::Format::eR16G16B16A16Uint:
	case vk::Format::eR16G16B16A16Sint:
	case vk::Format::eR16G16B16A16Sfloat:
	case vk::Format::eR32G32Uint:
	case vk::Format::eR32G32Sint:
	case vk::Format::eR32G32Sfloat:
	case vk::Format::eR64Uint:
	case vk::Format::eR64Sint:
	case vk::Format::eR64Sfloat:
		return 8;

	case vk::Format::eR32G32B32Uint:
	case vk::Format::eR32G32B32Sint:
	case vk::Format::eR32G32B32Sfloat:
		return 12;

	case vk::Format::eR32G32B32A32Uint:
	case vk::Format::eR32G32B32A32Sint:
	case vk::Format::eR32G32B32A32Sfloat:
	case vk::Format::eR64G64Uint:
	case vk::Format::eR64G64Sint:
	case vk::Format::eR64G64Sfloat:
		return 16;

	case vk::Format::eR64G64B64Uint:
	case vk::Format::eR64G64B64Sint:
	case vk::Format::eR64G64B64Sfloat:
		return 24;

	case vk::Format::eR64G64B64A64Uint:
	case vk::Format::eR64G64B64A64Sint:
	case vk::Format::eR64G64B64A64Sfloat:
		return 32;

	}
	return 0;
}

template<typename T = uint32_t> requires(std::is_arithmetic_v<T>)
inline constexpr T GetChannelCount(const vk::Format format) {
	switch (format) {
	default:
		throw std::runtime_error(std::string("Channel count unknown for format ") + vk::to_string(format));
	case vk::Format::eR8Unorm:
	case vk::Format::eR8Snorm:
	case vk::Format::eR8Uscaled:
	case vk::Format::eR8Sscaled:
	case vk::Format::eR8Uint:
	case vk::Format::eR8Sint:
	case vk::Format::eR8Srgb:
	case vk::Format::eR16Unorm:
	case vk::Format::eR16Snorm:
	case vk::Format::eR16Uscaled:
	case vk::Format::eR16Sscaled:
	case vk::Format::eR16Uint:
	case vk::Format::eR16Sint:
	case vk::Format::eR16Sfloat:
	case vk::Format::eR32Uint:
	case vk::Format::eR32Sint:
	case vk::Format::eR32Sfloat:
	case vk::Format::eR64Uint:
	case vk::Format::eR64Sint:
	case vk::Format::eR64Sfloat:
	case vk::Format::eD16Unorm:
	case vk::Format::eD32Sfloat:
	case vk::Format::eD16UnormS8Uint:
	case vk::Format::eD24UnormS8Uint:
	case vk::Format::eX8D24UnormPack32:
	case vk::Format::eS8Uint:
	case vk::Format::eD32SfloatS8Uint:
	case vk::Format::eBc4UnormBlock:
	case vk::Format::eBc4SnormBlock:
		return 1;
	case vk::Format::eR4G4UnormPack8:
	case vk::Format::eR8G8Unorm:
	case vk::Format::eR8G8Snorm:
	case vk::Format::eR8G8Uscaled:
	case vk::Format::eR8G8Sscaled:
	case vk::Format::eR8G8Uint:
	case vk::Format::eR8G8Sint:
	case vk::Format::eR8G8Srgb:
	case vk::Format::eR16G16Unorm:
	case vk::Format::eR16G16Snorm:
	case vk::Format::eR16G16Uscaled:
	case vk::Format::eR16G16Sscaled:
	case vk::Format::eR16G16Uint:
	case vk::Format::eR16G16Sint:
	case vk::Format::eR16G16Sfloat:
	case vk::Format::eR32G32Uint:
	case vk::Format::eR32G32Sint:
	case vk::Format::eR32G32Sfloat:
	case vk::Format::eR64G64Uint:
	case vk::Format::eR64G64Sint:
	case vk::Format::eR64G64Sfloat:
	case vk::Format::eBc5UnormBlock:
	case vk::Format::eBc5SnormBlock:
		return 2;
	case vk::Format::eR4G4B4A4UnormPack16:
	case vk::Format::eB4G4R4A4UnormPack16:
	case vk::Format::eR5G6B5UnormPack16:
	case vk::Format::eB5G6R5UnormPack16:
	case vk::Format::eR8G8B8Unorm:
	case vk::Format::eR8G8B8Snorm:
	case vk::Format::eR8G8B8Uscaled:
	case vk::Format::eR8G8B8Sscaled:
	case vk::Format::eR8G8B8Uint:
	case vk::Format::eR8G8B8Sint:
	case vk::Format::eR8G8B8Srgb:
	case vk::Format::eB8G8R8Unorm:
	case vk::Format::eB8G8R8Snorm:
	case vk::Format::eB8G8R8Uscaled:
	case vk::Format::eB8G8R8Sscaled:
	case vk::Format::eB8G8R8Uint:
	case vk::Format::eB8G8R8Sint:
	case vk::Format::eB8G8R8Srgb:
	case vk::Format::eR16G16B16Unorm:
	case vk::Format::eR16G16B16Snorm:
	case vk::Format::eR16G16B16Uscaled:
	case vk::Format::eR16G16B16Sscaled:
	case vk::Format::eR16G16B16Uint:
	case vk::Format::eR16G16B16Sint:
	case vk::Format::eR16G16B16Sfloat:
	case vk::Format::eR32G32B32Uint:
	case vk::Format::eR32G32B32Sint:
	case vk::Format::eR32G32B32Sfloat:
	case vk::Format::eR64G64B64Uint:
	case vk::Format::eR64G64B64Sint:
	case vk::Format::eR64G64B64Sfloat:
	case vk::Format::eB10G11R11UfloatPack32:
	case vk::Format::eBc1RgbUnormBlock:
	case vk::Format::eBc1RgbSrgbBlock:
	case vk::Format::eBc3UnormBlock:
	case vk::Format::eBc3SrgbBlock:
		return 3;
	case vk::Format::eR5G5B5A1UnormPack16:
	case vk::Format::eB5G5R5A1UnormPack16:
	case vk::Format::eA1R5G5B5UnormPack16:
	case vk::Format::eR8G8B8A8Unorm:
	case vk::Format::eR8G8B8A8Snorm:
	case vk::Format::eR8G8B8A8Uscaled:
	case vk::Format::eR8G8B8A8Sscaled:
	case vk::Format::eR8G8B8A8Uint:
	case vk::Format::eR8G8B8A8Sint:
	case vk::Format::eR8G8B8A8Srgb:
	case vk::Format::eB8G8R8A8Unorm:
	case vk::Format::eB8G8R8A8Snorm:
	case vk::Format::eB8G8R8A8Uscaled:
	case vk::Format::eB8G8R8A8Sscaled:
	case vk::Format::eB8G8R8A8Uint:
	case vk::Format::eB8G8R8A8Sint:
	case vk::Format::eB8G8R8A8Srgb:
	case vk::Format::eA8B8G8R8UnormPack32:
	case vk::Format::eA8B8G8R8SnormPack32:
	case vk::Format::eA8B8G8R8UscaledPack32:
	case vk::Format::eA8B8G8R8SscaledPack32:
	case vk::Format::eA8B8G8R8UintPack32:
	case vk::Format::eA8B8G8R8SintPack32:
	case vk::Format::eA8B8G8R8SrgbPack32:
	case vk::Format::eA2R10G10B10UnormPack32:
	case vk::Format::eA2R10G10B10SnormPack32:
	case vk::Format::eA2R10G10B10UscaledPack32:
	case vk::Format::eA2R10G10B10SscaledPack32:
	case vk::Format::eA2R10G10B10UintPack32:
	case vk::Format::eA2R10G10B10SintPack32:
	case vk::Format::eA2B10G10R10UnormPack32:
	case vk::Format::eA2B10G10R10SnormPack32:
	case vk::Format::eA2B10G10R10UscaledPack32:
	case vk::Format::eA2B10G10R10SscaledPack32:
	case vk::Format::eA2B10G10R10UintPack32:
	case vk::Format::eA2B10G10R10SintPack32:
	case vk::Format::eR16G16B16A16Unorm:
	case vk::Format::eR16G16B16A16Snorm:
	case vk::Format::eR16G16B16A16Uscaled:
	case vk::Format::eR16G16B16A16Sscaled:
	case vk::Format::eR16G16B16A16Uint:
	case vk::Format::eR16G16B16A16Sint:
	case vk::Format::eR16G16B16A16Sfloat:
	case vk::Format::eR32G32B32A32Uint:
	case vk::Format::eR32G32B32A32Sint:
	case vk::Format::eR32G32B32A32Sfloat:
	case vk::Format::eR64G64B64A64Uint:
	case vk::Format::eR64G64B64A64Sint:
	case vk::Format::eR64G64B64A64Sfloat:
	case vk::Format::eE5B9G9R9UfloatPack32:
	case vk::Format::eBc1RgbaUnormBlock:
	case vk::Format::eBc1RgbaSrgbBlock:
	case vk::Format::eBc2UnormBlock:
	case vk::Format::eBc2SrgbBlock:
		return 4;
	}
}


struct PixelData {
	BufferView data     = {};
	vk::Format format   = {};
	uint3 extent = {};
};
class CommandContext;
PixelData LoadImageFile(CommandContext& context, const std::filesystem::path& filename, const bool srgb = true, int desiredChannels = 0);

struct ImageInfo {
	vk::ImageCreateFlags    createFlags   = {};
	vk::ImageType           type          = vk::ImageType::e2D;
	vk::Format              format        = {};
	uint3                   extent        = {};
	uint32_t                mipLevels     = 1;
	uint32_t                arrayLayers   = 1;
	vk::SampleCountFlagBits samples       = vk::SampleCountFlagBits::e1;
	vk::ImageUsageFlags     usage         = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
	vk::ImageTiling         tiling        = vk::ImageTiling::eOptimal;
	vk::SharingMode         sharingMode   = vk::SharingMode::eExclusive;
	std::vector<uint32_t>   queueFamilies = {};

	inline bool operator==(const ImageInfo& rhs) const = default;
};

class Image {
public:
	struct ResourceState {
		vk::ImageLayout         layout      = {};
		vk::PipelineStageFlags2 stage       = {};
		vk::AccessFlags2        access      = {};
		uint32_t                queueFamily = VK_QUEUE_FAMILY_IGNORED;
	};

private:
	vk::Image     mImage = nullptr;
	vk::Device    mDevice = nullptr;
	VmaAllocator  mMemoryAllocator = nullptr;
	VmaAllocation mAllocation = nullptr;
	ImageInfo     mInfo = {};

	friend struct ImageView;
	TupleMap<vk::ImageView, vk::ImageSubresourceRange, vk::ImageViewType, vk::ComponentMapping> mCachedViews = {};

	std::vector<std::vector<ResourceState>> mSubresourceStates = {}; // mSubresourceStates[arrayLayer][mipLevel]

public:
	static ref<Image> Create(Device& device, const ImageInfo& info, const vk::MemoryPropertyFlags memoryFlags = vk::MemoryPropertyFlagBits::eDeviceLocal, const VmaAllocationCreateFlags allocationFlags = VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT);
	static ref<Image> Create(const vk::Device device, const vk::Image image, const ImageInfo& info);
	~Image();

	inline       vk::Image& operator*()        { return mImage; }
	inline const vk::Image& operator*() const  { return mImage; }
	inline       vk::Image* operator->()       { return &mImage; }
	inline const vk::Image* operator->() const { return &mImage; }

	inline vk::Device GetDevice() const { return mDevice; }

	inline operator bool() const { return mImage; }

	inline const ImageInfo& Info() const { return mInfo; }

	inline const ResourceState& GetSubresourceState(const uint32_t arrayLayer, const uint32_t level) const {
		return mSubresourceStates[arrayLayer][level];
	}
	inline std::vector<vk::ImageMemoryBarrier2> SetSubresourceState(const vk::ImageSubresourceRange& subresource, const ResourceState& newState) {
		std::vector<vk::ImageMemoryBarrier2> barriers;

		const uint32_t maxLayer = std::min(mInfo.arrayLayers, subresource.baseArrayLayer + subresource.layerCount);
		const uint32_t maxLevel = std::min(mInfo.mipLevels  , subresource.baseMipLevel   + subresource.levelCount);
		for (uint32_t arrayLayer = subresource.baseArrayLayer; arrayLayer < maxLayer; arrayLayer++) {
			for (uint32_t level = subresource.baseMipLevel; level < maxLevel; level++) {
				const auto oldState = mSubresourceStates[arrayLayer][level];

				const vk::ImageMemoryBarrier2 barrier{
					.srcStageMask        = oldState.stage,
					.srcAccessMask       = oldState.access,
					.dstStageMask        = newState.stage,
					.dstAccessMask       = newState.access,
					.oldLayout           = oldState.layout,
					.newLayout           = newState.layout,
					.srcQueueFamilyIndex = oldState.queueFamily,
					.dstQueueFamilyIndex = newState.queueFamily,
					.image = mImage,
					.subresourceRange = vk::ImageSubresourceRange{
						.aspectMask = subresource.aspectMask,
						.baseMipLevel   = level,
						.levelCount     = 1,
						.baseArrayLayer = arrayLayer,
						.layerCount     = 1
					}
				};

				mSubresourceStates[arrayLayer][level] = newState;

				// try to combine barrier with the last one
				// this only works when barriers are for sequential mip levels
				if (!barriers.empty()) {
					vk::ImageMemoryBarrier2& prev = barriers.back();
					if (prev.srcStageMask        == newState.stage &&
						prev.dstAccessMask       == newState.access &&
						prev.newLayout           == newState.layout &&
						prev.dstQueueFamilyIndex == newState.queueFamily &&
						prev.subresourceRange.aspectMask     == subresource.aspectMask &&
						prev.subresourceRange.baseArrayLayer == subresource.baseArrayLayer &&
						prev.subresourceRange.layerCount     == subresource.layerCount) {
						// everything but the mip levels match...
						const uint32_t baseMip = std::min(prev.subresourceRange.baseMipLevel, barrier.subresourceRange.baseMipLevel);
						const uint32_t count = prev.subresourceRange.levelCount + barrier.subresourceRange.levelCount;
						if (prev.subresourceRange.baseMipLevel + prev.subresourceRange.levelCount == barrier.subresourceRange.baseMipLevel ||
							barrier.subresourceRange.baseMipLevel + barrier.subresourceRange.levelCount == prev.subresourceRange.baseMipLevel) {
							// barriers are for sequential mip levels, we can combine them
							prev.subresourceRange.baseMipLevel = baseMip;
							prev.subresourceRange.levelCount = count;
							continue;
						}
					}
				}

				barriers.emplace_back(barrier);

			}
		}

		return barriers;
	}
};

struct ImageView {
	vk::ImageView             mView = {};
	ref<Image>                mImage = {};
	vk::ImageSubresourceRange mSubresource = { vk::ImageAspectFlagBits::eColor, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS };
	vk::ImageViewType         mType = vk::ImageViewType::e2D;
	vk::ComponentMapping      mComponentMapping = {};

	static ImageView Create(const ref<Image>& image, const vk::ImageSubresourceRange& subresource = { vk::ImageAspectFlagBits::eColor, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS }, const vk::ImageViewType type = vk::ImageViewType::e2D, const vk::ComponentMapping& componentMapping = {});

	inline       vk::ImageView& operator*()        { return mView; }
	inline const vk::ImageView& operator*() const  { return mView; }
	inline       vk::ImageView* operator->()       { return &mView; }
	inline const vk::ImageView* operator->() const { return &mView; }

	inline bool operator==(const ImageView& rhs) const { return mView == rhs.mView; }
	inline bool operator!=(const ImageView& rhs) const { return mView != rhs.mView; }

	inline operator bool() const { return mView && mImage; }

	inline uint3 Extent(const uint32_t levelOffset = 0) const { return GetLevelExtent(mImage->Info().extent, mSubresource.baseMipLevel + levelOffset); }
	inline const ref<Image>& GetImage() const { return mImage; }

	inline vk::ImageSubresourceLayers GetSubresourceLayer(const uint32_t levelOffset = 0) const {
		return vk::ImageSubresourceLayers{
			.aspectMask     = mSubresource.aspectMask,
			.mipLevel       = mSubresource.baseMipLevel + levelOffset,
			.baseArrayLayer = mSubresource.baseArrayLayer,
			.layerCount     = mSubresource.layerCount
		};
	}
	inline std::vector<vk::ImageMemoryBarrier2> SetState(const Image::ResourceState& newState) const {
		return mImage->SetSubresourceState(mSubresource, newState);
	}
};

}

namespace std {

template<>
struct hash<RoseEngine::ImageInfo> {
	inline size_t operator()(const RoseEngine::ImageInfo& v) const {
		return RoseEngine::HashArgs(
			v.createFlags,
			v.type,
			v.format,
			v.extent.x, v.extent.y, v.extent.z,
			v.mipLevels,
			v.arrayLayers,
			v.samples,
			v.usage,
			v.tiling,
			v.sharingMode,
			RoseEngine::HashRange(v.queueFamilies) );
	}
};

template<>
struct hash<RoseEngine::ImageView> {
	inline size_t operator()(const RoseEngine::ImageView& v) const {
		return hash<vk::ImageView>()(*v);
	}
};

}
