#pragma once

#include "Device.hpp"

namespace RoseEngine {

struct Buffer {
	vk::Buffer              mBuffer = nullptr;
	VmaAllocator            mMemoryAllocator = nullptr;
	VmaAllocation           mAllocation = nullptr;
	VmaAllocationInfo       mAllocationInfo = {};
	vk::DeviceSize          mSize = 0;
	vk::BufferUsageFlags    mUsage = {};
	vk::MemoryPropertyFlags mMemoryFlags = {};
	vk::SharingMode         mSharingMode = {};

	inline       vk::Buffer& operator*()        { return mBuffer; }
	inline const vk::Buffer& operator*() const  { return mBuffer; }
	inline       vk::Buffer* operator->()       { return &mBuffer; }
	inline const vk::Buffer* operator->() const { return &mBuffer; }

	Buffer(const Device& device, const vk::BufferCreateInfo& createInfo, const VmaAllocationCreateInfo& allocationInfo);
	~Buffer();
};

struct BufferView {
	std::shared_ptr<Buffer> mBuffer = nullptr;
	vk::DeviceSize mOffset = 0; // In bytes
	vk::DeviceSize mSize = 0; // Number of elements

	inline operator vk::DescriptorBufferInfo() const { return vk::DescriptorBufferInfo{ **mBuffer, mOffset, mSize }; }
};

template<typename T>
struct BufferRange {
	using value_type = T;
	using size_type = vk::DeviceSize;
	using reference = value_type&;
	using pointer = value_type*;
	using iterator = T*;

	BufferView mView;

	inline bool empty() const { return !mView.mBuffer || mView.mSize == 0; }
	inline size_type size() const { return mView.mSize; }
	inline T* data() const { return (T*)((std::byte*)mView.mBuffer->mAllocationInfo.pMappedData + mView.mOffset); }

	inline reference at(size_type index) const { return data()[index]; }
	inline reference operator[](size_type index) const { return at(index); }

	inline reference front() { return at(0); }
	inline reference back() { return at(mView.mSize - 1); }

	inline iterator begin() const { return data(); }
	inline iterator end() const { return data() + mView.mSize; }

	inline operator const BufferView&() const { return mView; }

	template<typename Ty> inline BufferRange<Ty> as() const { return BufferRange<Ty>{ mView.mBuffer, mView.mOffset, (mView.mSize*sizeof(T)) / sizeof(Ty) }; }
	template<typename Ty> inline operator BufferRange<Ty>() const { return as<Ty>(); }
};

inline std::shared_ptr<Buffer> CreateBuffer(
	const Device& device,
	const vk::BufferCreateInfo& createInfo,
	const vk::MemoryPropertyFlags memoryFlags = vk::MemoryPropertyFlagBits::eDeviceLocal,
	const VmaAllocationCreateFlags allocationFlags = VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT) {
	return std::make_shared<Buffer>(device, createInfo,
	VmaAllocationCreateInfo{
	.flags = allocationFlags,
	.usage = VMA_MEMORY_USAGE_AUTO,
	.requiredFlags = (VkMemoryPropertyFlags)memoryFlags,
	.memoryTypeBits = 0,
	.pool = VK_NULL_HANDLE,
	.pUserData = VK_NULL_HANDLE,
	.priority = 0 });
}

inline std::shared_ptr<Buffer> CreateBuffer(
	const Device& device,
	const vk::DeviceSize size,
	const vk::BufferUsageFlags usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst,
	const vk::MemoryPropertyFlags memoryFlags = vk::MemoryPropertyFlagBits::eDeviceLocal,
	const VmaAllocationCreateFlags allocationFlags = VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT) {
	return std::make_shared<Buffer>(device,
		vk::BufferCreateInfo({}, size, usage),
		VmaAllocationCreateInfo{
		.flags = allocationFlags,
		.usage = VMA_MEMORY_USAGE_AUTO,
		.requiredFlags = (VkMemoryPropertyFlags)memoryFlags,
		.memoryTypeBits = 0,
		.pool = VK_NULL_HANDLE,
		.pUserData = VK_NULL_HANDLE,
		.priority = 0 });
}


template<std::ranges::range R>
inline BufferRange<std::ranges::range_value_t<R>> CreateBuffer(
	const Device& device,
	const R& data,
	const vk::BufferUsageFlags usage = vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst,
	const vk::MemoryPropertyFlags memoryFlags = vk::MemoryPropertyFlagBits::eHostVisible|vk::MemoryPropertyFlagBits::eHostCoherent,
	const VmaAllocationCreateFlags allocationFlags = VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT|VMA_ALLOCATION_CREATE_MAPPED_BIT|VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT) {

	using T = std::ranges::range_value_t<R>;
	const size_t size = std::ranges::size(data);

	auto buf = CreateBuffer(
		device,
		size * sizeof(T),
		usage,
		memoryFlags,
		allocationFlags);

	std::ranges::copy_n(std::ranges::begin(data), size, (T*)buf->mAllocationInfo.pMappedData);

	return { buf, 0, size };
}

}