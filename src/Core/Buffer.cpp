#include "Buffer.hpp"

namespace RoseEngine {

Buffer::Buffer(const Device& device, const vk::BufferCreateInfo& createInfo, const VmaAllocationCreateInfo& allocationInfo) {
	vk::Result result = (vk::Result)vmaCreateBuffer(device.mMemoryAllocator, &(const VkBufferCreateInfo&)createInfo, &allocationInfo, &(VkBuffer&)mBuffer, &mAllocation, &mAllocationInfo);
	if (result != vk::Result::eSuccess) {
		mMemoryAllocator  = nullptr;
		mAllocation = nullptr;
		mAllocationInfo = {};
		mSize = 0;
		mUsage = {};
		mMemoryFlags = {};
		mSharingMode = {};
		return;
	}

	mMemoryAllocator = device.mMemoryAllocator;
	mSize  = createInfo.size;
	mUsage = createInfo.usage;
	mMemoryFlags = (vk::MemoryPropertyFlags)mAllocationInfo.memoryType;
	mSharingMode = createInfo.sharingMode;
}

Buffer::~Buffer() {
	if (mMemoryAllocator && mBuffer && mAllocation) {
		vmaDestroyBuffer(mMemoryAllocator, mBuffer, mAllocation);
		mMemoryAllocator  = nullptr;
		mBuffer     = nullptr;
		mAllocation = nullptr;
	}
}

}