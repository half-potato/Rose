#pragma once

#include <unordered_set>

#include <vk_mem_alloc.h>

#include "Instance.hpp"

namespace RoseEngine {

struct Device {
	vk::raii::Device         mDevice = nullptr;
	vk::raii::PhysicalDevice mPhysicalDevice = nullptr;
	vk::raii::PipelineCache  mPipelineCache = nullptr;
	VmaAllocator             mMemoryAllocator = nullptr;

	vk::PhysicalDeviceFeatures mFeatures;
	vk::StructureChain<
		vk::DeviceCreateInfo,
		vk::PhysicalDeviceVulkan12Features,
		vk::PhysicalDeviceVulkan13Features,
		vk::PhysicalDevice16BitStorageFeatures,
		vk::PhysicalDeviceAccelerationStructureFeaturesKHR,
		vk::PhysicalDeviceRayTracingPipelineFeaturesKHR,
		vk::PhysicalDeviceRayQueryFeaturesKHR
		> mCreateInfo;
	vk::PhysicalDeviceLimits mLimits;
	vk::PhysicalDeviceAccelerationStructurePropertiesKHR mAccelerationStructureProperties;

	std::unordered_set<std::string> mExtensions;

	Device() = default;
	Device(Device&&) = default;
	Device(const Instance& instance, const vk::raii::PhysicalDevice physicalDevice, const std::vector<std::string>& deviceExtensions = {});
	~Device();

	inline       vk::raii::Device& operator*()        { return mDevice; }
	inline const vk::raii::Device& operator*() const  { return mDevice; }
	inline       vk::raii::Device* operator->()       { return &mDevice; }
	inline const vk::raii::Device* operator->() const { return &mDevice; }

	void LoadPipelineCache(const std::filesystem::path& path);
	void StorePipelineCache(const std::filesystem::path& path);

	template<typename T> requires(std::convertible_to<decltype(T::objectType), vk::ObjectType>)
	inline void SetDebugName(T object, const std::string_view& name) const {
		vk::DebugUtilsObjectNameInfoEXT info = {};
		info.objectHandle = *reinterpret_cast<const uint64_t*>(&object);
		info.objectType = T::objectType;
		info.pObjectName = name.data();
		mDevice.setDebugUtilsObjectNameEXT(info);
	}
};

}