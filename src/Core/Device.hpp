#pragma once

#include <unordered_set>
#include <vk_mem_alloc.h>

#include "RoseEngine.hpp"

namespace RoseEngine {

class Instance;

using DescriptorSets = std::vector<vk::raii::DescriptorSet>;

class Device {
private:
	vk::raii::Device         mDevice = nullptr;
	vk::raii::PhysicalDevice mPhysicalDevice = nullptr;
	vk::raii::PipelineCache  mPipelineCache = nullptr;
	VmaAllocator             mMemoryAllocator = nullptr;

	uint64_t                 mCurrentSemaphoreValue = 0;
	vk::raii::Semaphore      mTimelineSemaphore = nullptr;

	vk::PhysicalDeviceFeatures mFeatures = {};
	vk::StructureChain<
		vk::DeviceCreateInfo,
		vk::PhysicalDeviceVulkan12Features,
		vk::PhysicalDeviceVulkan13Features,
		vk::PhysicalDevice16BitStorageFeatures,
		vk::PhysicalDeviceAccelerationStructureFeaturesKHR,
		vk::PhysicalDeviceRayTracingPipelineFeaturesKHR,
		vk::PhysicalDeviceRayQueryFeaturesKHR
		> mCreateInfo = {};
	vk::PhysicalDeviceLimits mLimits = {};
	vk::PhysicalDeviceAccelerationStructurePropertiesKHR mAccelerationStructureProperties = {};

	std::unordered_set<std::string> mExtensions = {};

	bool mUseDebugUtils = false;

	std::unordered_map<uint32_t/*queue family*/, vk::raii::CommandPool> mCachedCommandPools;

	std::list<vk::raii::DescriptorPool> mCachedDescriptorPools;
	void AllocateDescriptorPool();

public:
	~Device();

	static ref<Device> Create(const Instance& instance, const vk::raii::PhysicalDevice physicalDevice, const std::vector<std::string>& deviceExtensions = {});

	inline       vk::raii::Device& operator*()        { return mDevice; }
	inline const vk::raii::Device& operator*() const  { return mDevice; }
	inline       vk::raii::Device* operator->()       { return &mDevice; }
	inline const vk::raii::Device* operator->() const { return &mDevice; }

	inline VmaAllocator MemoryAllocator() const { return mMemoryAllocator; }
	inline const auto& PipelineCache() const { return mPipelineCache; }
	inline const auto& EnabledExtensions() const { return mExtensions; }

	void  LoadPipelineCache(const std::filesystem::path& path);
	void StorePipelineCache(const std::filesystem::path& path);

	inline uint32_t FindQueueFamily(const vk::QueueFlags flags = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer) {
		const auto queueFamilyProperties = mPhysicalDevice.getQueueFamilyProperties();
		for (uint32_t i = 0; i < queueFamilyProperties.size(); i++) {
			if (queueFamilyProperties[i].queueFlags | flags)
				return i;
		}
		return -1;
	}

	vk::CommandPool GetCommandPool(uint32_t queueFamily);

	DescriptorSets AllocateDescriptorSets(const vk::ArrayProxy<const vk::DescriptorSetLayout>& layouts);

	inline const vk::raii::Semaphore& TimelineSemaphore() const { return mTimelineSemaphore; }
	inline uint64_t NextTimelineCounterValue() const { return mCurrentSemaphoreValue; }
	inline uint64_t IncrementTimelineCounter() { return mCurrentSemaphoreValue++; }

	inline void Wait(uint64_t value) {
		auto result = mDevice.waitSemaphores(vk::SemaphoreWaitInfo{}
				.setSemaphores(*mTimelineSemaphore)
				.setValues(value),
			UINT64_MAX);

		if (result != vk::Result::eSuccess)
			throw std::runtime_error("waitSemaphores failed: " + vk::to_string(result));
	}

	inline void Wait() {
		const uint64_t value = IncrementTimelineCounter();
		mDevice.signalSemaphore(vk::SemaphoreSignalInfo{ .value = value });
		Wait(value);
	}

	template<typename T> requires(std::convertible_to<decltype(T::objectType), vk::ObjectType>)
	inline void SetDebugName(T object, const std::string_view& name) const {
		if (!mUseDebugUtils) return;
		vk::DebugUtilsObjectNameInfoEXT info = {};
		info.objectHandle = *reinterpret_cast<const uint64_t*>(&object);
		info.objectType = T::objectType;
		info.pObjectName = name.data();
		mDevice.setDebugUtilsObjectNameEXT(info);
	}
};

}