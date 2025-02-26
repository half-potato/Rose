#pragma once

#include <bitset>
#include <vk_mem_alloc.h>

#include "RoseEngine.hpp"

namespace RoseEngine {

class Instance;

class CommandContext;

class Device {
private:
	vk::raii::Device         mDevice = nullptr;
	vk::raii::PhysicalDevice mPhysicalDevice = nullptr;
	vk::raii::PipelineCache  mPipelineCache = nullptr;
	vk::Instance             mInstance = nullptr;
	VmaAllocator             mMemoryAllocator = nullptr;

	vk::raii::Semaphore      mTimelineSemaphore = nullptr;
	uint64_t                 mCurrentTimelineValue = 0;

	vk::PhysicalDeviceFeatures mFeatures = {};
	vk::StructureChain<
		vk::DeviceCreateInfo,
		vk::PhysicalDeviceVulkan12Features,
		vk::PhysicalDeviceVulkan13Features,
		vk::PhysicalDevice16BitStorageFeatures,
		vk::PhysicalDeviceShaderAtomicFloatFeaturesEXT,
		vk::PhysicalDeviceAccelerationStructureFeaturesKHR,
		vk::PhysicalDeviceRayTracingPipelineFeaturesKHR,
		vk::PhysicalDeviceRayQueryFeaturesKHR,
		vk::PhysicalDeviceFragmentShaderBarycentricFeaturesKHR,
		vk::PhysicalDeviceFragmentShaderInterlockFeaturesEXT
		> mCreateInfo = {};
	vk::PhysicalDeviceLimits mLimits = {};
	vk::PhysicalDeviceAccelerationStructurePropertiesKHR mAccelerationStructureProperties = {};

	std::unordered_set<std::string> mExtensions = {};

	bool mUseDebugUtils = false;

public:
	~Device();

	static ref<Device> Create(const Instance& instance, const vk::raii::PhysicalDevice& physicalDevice, const vk::ArrayProxy<const std::string>& deviceExtensions = {});

	inline       vk::raii::Device& operator*()        { return mDevice; }
	inline const vk::raii::Device& operator*() const  { return mDevice; }
	inline       vk::raii::Device* operator->()       { return &mDevice; }
	inline const vk::raii::Device* operator->() const { return &mDevice; }

	void  LoadPipelineCache(const std::filesystem::path& path);
	void StorePipelineCache(const std::filesystem::path& path);

	inline VmaAllocator                           MemoryAllocator() const { return mMemoryAllocator; }
	inline vk::Instance                           GetInstance() const { return mInstance; }
	inline const vk::raii::PhysicalDevice&        PhysicalDevice() const { return mPhysicalDevice; }
	inline const vk::raii::PipelineCache&         PipelineCache() const { return mPipelineCache; }
	inline const vk::PhysicalDeviceLimits&        Limits() const { return mLimits; }
	inline const std::unordered_set<std::string>& EnabledExtensions() const { return mExtensions; }
	inline bool                                   DebugUtilsEnabled() const { return mUseDebugUtils; }
	inline const auto&                            CreateInfo() const { return mCreateInfo; }

	inline uint32_t FindQueueFamily(const vk::QueueFlags flags = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer) {
		uint32_t min_i = -1;
		uint32_t min_bits = UINT32_MAX;
		const auto queueFamilyProperties = mPhysicalDevice.getQueueFamilyProperties();
		for (uint32_t i = 0; i < queueFamilyProperties.size(); i++) {
			const uint32_t bits =  std::bitset<32>((uint32_t)queueFamilyProperties[i].queueFlags).count();
			if ((queueFamilyProperties[i].queueFlags & flags) == flags && bits < min_bits) {
				min_i = i;
				min_bits = bits;
			}
		}
		return min_i;
	}

	inline const vk::raii::Semaphore& TimelineSemaphore() const { return mTimelineSemaphore; }
	inline uint64_t CurrentTimelineValue() const { return mTimelineSemaphore.getCounterValue(); }
	inline uint64_t NextTimelineSignal() const { return mCurrentTimelineValue; }
	inline uint64_t IncrementTimelineSignal() { return mCurrentTimelineValue++; }

	inline void Wait(uint64_t value) {
		auto result = mDevice.waitSemaphores(vk::SemaphoreWaitInfo{}
				.setSemaphores(*mTimelineSemaphore)
				.setValues(value),
			UINT64_MAX);

		if (result != vk::Result::eSuccess)
			throw std::runtime_error("waitSemaphores failed: " + vk::to_string(result));
	}

	inline void Wait() {
		Wait(mCurrentTimelineValue - 1);
		mDevice.waitIdle();
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