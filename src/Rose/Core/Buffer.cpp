#include <iostream>
#include "Buffer.hpp"

namespace RoseEngine {

ref<Buffer> Buffer::Create(const Device& device, const vk::BufferCreateInfo& createInfo, const VmaAllocationCreateInfo& allocationInfo) {
	VmaAllocation alloc;
	VmaAllocationInfo allocInfo;
	VkBuffer vkbuffer;
	vk::Result result = (vk::Result)vmaCreateBuffer(device.MemoryAllocator(), &(const VkBufferCreateInfo&)createInfo, &allocationInfo, &vkbuffer, &alloc, &allocInfo);
	if (result != vk::Result::eSuccess) {
		std::cerr << "Failed to create buffer: " << vk::to_string(result) << std::endl;
		return nullptr;
	}

	auto buffer = make_ref<Buffer>();
	buffer->mBuffer = vkbuffer;
	buffer->mMemoryAllocator = device.MemoryAllocator();
	buffer->mAllocation = alloc;
	buffer->mAllocationInfo = allocInfo;
	buffer->mSize  = createInfo.size;
	buffer->mUsage = createInfo.usage;
	buffer->mMemoryFlags = (vk::MemoryPropertyFlags)allocInfo.memoryType;
	buffer->mSharingMode = createInfo.sharingMode;
	return buffer;
}

Buffer::~Buffer() {
	if (mMemoryAllocator && mBuffer && mAllocation) {
		vmaDestroyBuffer(mMemoryAllocator, mBuffer, mAllocation);
		mMemoryAllocator = nullptr;
		mBuffer     = nullptr;
		mAllocation = nullptr;
	}
}

BufferView Buffer::Create(
	const Device& device,
	const vk::BufferCreateInfo&    createInfo,
	const vk::MemoryPropertyFlags  memoryFlags,
	const VmaAllocationCreateFlags allocationFlags) {
	auto buf = Create(
		device,
		createInfo,
		VmaAllocationCreateInfo{
			.flags = allocationFlags,
			.usage = VMA_MEMORY_USAGE_AUTO,
			.requiredFlags = (VkMemoryPropertyFlags)memoryFlags,
			.memoryTypeBits = 0,
			.pool = VK_NULL_HANDLE,
			.pUserData = VK_NULL_HANDLE,
			.priority = 0 });
	return { buf, 0, createInfo.size };
}

BufferView Buffer::Create(
	const Device& device,
	const vk::DeviceSize size,
	const vk::BufferUsageFlags     usage,
	const vk::MemoryPropertyFlags  memoryFlags,
	const VmaAllocationCreateFlags allocationFlags) {
	auto buf = Create(
		device,
		vk::BufferCreateInfo{
			.size = size,
			.usage = usage },
		VmaAllocationCreateInfo{
			.flags = allocationFlags,
			.usage = VMA_MEMORY_USAGE_AUTO,
			.requiredFlags = (VkMemoryPropertyFlags)memoryFlags,
			.memoryTypeBits = 0,
			.pool = VK_NULL_HANDLE,
			.pUserData = VK_NULL_HANDLE,
			.priority = 0 });
	return { buf, 0, size };
}

TexelBufferView TexelBufferView::Create(const Device& device, const BufferView& buffer, vk::Format format) {
	TexelBufferView b;
	b.mBufferView = make_ref<vk::raii::BufferView>(device->createBufferView(vk::BufferViewCreateInfo{
		.buffer = **buffer.mBuffer,
		.format = format,
		.offset = buffer.mOffset,
		.range  = buffer.size_bytes(),
	}));
	b.mBuffer = buffer;
	b.mFormat = format;
	return b;
}

}