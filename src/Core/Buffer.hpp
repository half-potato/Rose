#pragma once

#include "Device.hpp"
#include "Hash.hpp"

namespace RoseEngine {

template<typename T>
struct BufferRange;

using BufferView = BufferRange<std::byte>;

class Buffer {
public:
	struct ResourceState {
		vk::PipelineStageFlags2 stage       = {};
		vk::AccessFlags2        access      = {};
		uint32_t                queueFamily = VK_QUEUE_FAMILY_IGNORED;
	};

private:
	vk::Buffer              mBuffer = nullptr;
	VmaAllocator            mMemoryAllocator = nullptr;
	VmaAllocation           mAllocation = nullptr;
	VmaAllocationInfo       mAllocationInfo = {};
	vk::DeviceSize          mSize = 0;
	vk::BufferUsageFlags    mUsage = {};
	vk::MemoryPropertyFlags mMemoryFlags = {};
	vk::SharingMode         mSharingMode = {};

	PairMap<ResourceState, vk::DeviceSize, vk::DeviceSize> mState;

public:
	static ref<Buffer> Create(
		const Device&                  device,
		const vk::BufferCreateInfo&    createInfo,
		const VmaAllocationCreateInfo& allocationInfo);
	static BufferView Create(
		const Device&                  device,
		const vk::BufferCreateInfo&    createInfo,
		const vk::MemoryPropertyFlags  memoryFlags     = vk::MemoryPropertyFlagBits::eDeviceLocal,
		const VmaAllocationCreateFlags allocationFlags = VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT);
	static BufferView Create(
		const Device&                  device,
		const vk::DeviceSize           size,
		const vk::BufferUsageFlags     usage           = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst,
		const vk::MemoryPropertyFlags  memoryFlags     = vk::MemoryPropertyFlagBits::eDeviceLocal,
		const VmaAllocationCreateFlags allocationFlags = VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT);
	template<std::ranges::contiguous_range R>
	static BufferRange<std::ranges::range_value_t<R>> Create(
		const Device& device,
		R&&           data,
		const vk::BufferUsageFlags     usage           = vk::BufferUsageFlagBits::eTransferSrc,
		const vk::MemoryPropertyFlags  memoryFlags     = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
		const VmaAllocationCreateFlags allocationFlags = VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
	~Buffer();

	inline       vk::Buffer& operator*()        { return mBuffer; }
	inline const vk::Buffer& operator*() const  { return mBuffer; }
	inline       vk::Buffer* operator->()       { return &mBuffer; }
	inline const vk::Buffer* operator->() const { return &mBuffer; }

	inline const VmaAllocationInfo& AllocationInfo() const { return mAllocationInfo; }
	inline vk::DeviceSize          Size() const  { return mSize; }
	inline vk::BufferUsageFlags    Usage() const { return mUsage; }
	inline vk::MemoryPropertyFlags MemoryFlags() const  { return mMemoryFlags; }
	inline vk::SharingMode         SharingMode() const  { return mSharingMode; }

	inline void* data() const { return mAllocationInfo.pMappedData; }

	inline const ResourceState& GetState(vk::DeviceSize offset, vk::DeviceSize size) {
		auto it = mState.find(std::make_pair(offset, size));
		if (it == mState.end())
			it = mState.emplace(std::make_pair(offset, size), ResourceState{
				.stage       = vk::PipelineStageFlagBits2::eTopOfPipe,
				.access      = vk::AccessFlagBits2::eNone,
				.queueFamily = VK_QUEUE_FAMILY_IGNORED }).first;
		return it->second;
	}
	inline vk::BufferMemoryBarrier2 SetState(const ResourceState& newState, vk::DeviceSize offset, vk::DeviceSize size) {
		const auto& oldState = GetState(offset, size);
		mState[std::make_pair(offset, size)] = newState;
		return vk::BufferMemoryBarrier2 {
			.srcStageMask        = oldState.stage,
			.srcAccessMask       = oldState.access,
			.dstStageMask        = newState.stage,
			.dstAccessMask       = newState.access,
			.srcQueueFamilyIndex = oldState.queueFamily,
			.dstQueueFamilyIndex = newState.queueFamily,
			.buffer = mBuffer,
			.offset = offset,
			.size = size
		};
	}
};

template<typename T>
struct BufferRange {
	ref<Buffer>    mBuffer = nullptr;
	vk::DeviceSize mOffset = 0; // in bytes
	vk::DeviceSize mSize   = 0; // element count

	using value_type = T;
	using size_type = vk::DeviceSize;
	using reference = value_type&;
	using pointer   = value_type*;
	using iterator   = T*;

	inline operator bool() const { return mBuffer != nullptr; }

	inline bool empty() const { return !mBuffer || mSize == 0; }
	inline size_type size() const { return mSize; }
	inline size_type size_bytes() const { return mSize == VK_WHOLE_SIZE ? VK_WHOLE_SIZE : mSize * sizeof(T); }
	inline T* data() const { return mBuffer ? reinterpret_cast<T*>(reinterpret_cast<std::byte*>(mBuffer->data()) + mOffset) : nullptr; }

	inline reference at(size_type index) const { return data()[index]; }
	inline reference operator[](size_type index) const { return at(index); }

	inline reference front() { return at(0); }
	inline reference back() { return at(mSize - 1); }

	inline iterator begin() const { return data(); }
	inline iterator end() const { return data() + mSize; }

	inline BufferRange slice(size_t start, size_t count = VK_WHOLE_SIZE) const {
		return BufferRange{
			.mBuffer = mBuffer,
			.mOffset = mOffset + sizeof(T) * start,
			.mSize = count == VK_WHOLE_SIZE ? size() - start : count };
	}

	template<typename Ty> inline BufferRange<Ty> cast() const { return BufferRange<Ty>{ mBuffer, mOffset, size_bytes() / sizeof(Ty) }; }
	template<typename Ty> inline operator BufferRange<Ty>() const { return cast<Ty>(); }

	inline bool operator==(const BufferRange& rhs) const = default;

	inline const Buffer::ResourceState& GetState() const {
		return mBuffer->GetState(mOffset, size_bytes());
	}
	inline vk::BufferMemoryBarrier2 SetState(const Buffer::ResourceState& newState) const {
		return mBuffer->SetState(newState, mOffset, size_bytes());
	}
};

template<std::ranges::contiguous_range R>
inline BufferRange<std::ranges::range_value_t<R>> Buffer::Create(
	const Device&                  device,
	R&&                            data,
	const vk::BufferUsageFlags     usage,
	const vk::MemoryPropertyFlags  memoryFlags,
	const VmaAllocationCreateFlags allocationFlags ) {

	const size_t size = data.size() * sizeof(std::ranges::range_value_t<R>);

	BufferView buf = Buffer::Create(
		device,
		size,
		usage,
		memoryFlags,
		allocationFlags);

	std::memcpy(buf.data(), data.data(), size);

	return buf;
}

}