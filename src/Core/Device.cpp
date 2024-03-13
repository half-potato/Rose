#define VMA_IMPLEMENTATION
#include "Device.hpp"

#include <ranges>
#include <iostream>

namespace RoseEngine {

void ConfigureFeatures(Device& device) {
	device.mFeatures.fillModeNonSolid = true;
	device.mFeatures.samplerAnisotropy = true;
	device.mFeatures.shaderImageGatherExtended = true;
	device.mFeatures.shaderStorageImageExtendedFormats = true;
	device.mFeatures.wideLines = true;
	device.mFeatures.largePoints = true;
	device.mFeatures.sampleRateShading = true;
	device.mFeatures.shaderInt16 = true;
	device.mFeatures.shaderStorageBufferArrayDynamicIndexing = true;
	device.mFeatures.shaderSampledImageArrayDynamicIndexing = true;
	device.mFeatures.shaderStorageImageArrayDynamicIndexing = true;

	vk::PhysicalDeviceVulkan12Features& vk12features = std::get<vk::PhysicalDeviceVulkan12Features>(device.mCreateInfo);
	vk12features.shaderStorageBufferArrayNonUniformIndexing = true;
	vk12features.shaderSampledImageArrayNonUniformIndexing = true;
	vk12features.shaderStorageImageArrayNonUniformIndexing = true;
	vk12features.descriptorBindingPartiallyBound = true;
	vk12features.shaderInt8 = true;
	vk12features.storageBuffer8BitAccess = true;
	vk12features.shaderFloat16 = true;
	vk12features.bufferDeviceAddress = device.mExtensions.contains(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);

	vk::PhysicalDeviceVulkan13Features& vk13features = std::get<vk::PhysicalDeviceVulkan13Features>(device.mCreateInfo);
	vk13features.dynamicRendering = true;
	vk13features.synchronization2 = true;

	vk::PhysicalDevice16BitStorageFeatures& storageFeatures = std::get<vk::PhysicalDevice16BitStorageFeatures>(device.mCreateInfo);
	storageFeatures.storageBuffer16BitAccess = true;

	std::get<vk::PhysicalDeviceAccelerationStructureFeaturesKHR>(device.mCreateInfo).accelerationStructure = device.mExtensions.contains(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);

	auto& rtfeatures = std::get<vk::PhysicalDeviceRayTracingPipelineFeaturesKHR>(device.mCreateInfo);
	rtfeatures.rayTracingPipeline = device.mExtensions.contains(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
	rtfeatures.rayTraversalPrimitiveCulling = rtfeatures.rayTracingPipeline;

	std::get<vk::PhysicalDeviceRayQueryFeaturesKHR>(device.mCreateInfo).rayQuery = device.mExtensions.contains(VK_KHR_RAY_QUERY_EXTENSION_NAME);
}

Device::Device(const Instance& instance, const vk::raii::PhysicalDevice physicalDevice, const std::vector<std::string>& deviceExtensions) {
	mPhysicalDevice = physicalDevice;

	mExtensions = deviceExtensions | std::ranges::to<std::unordered_set<std::string>>();

	ConfigureFeatures(*this);

	std::vector<vk::QueueFamilyProperties> queueFamilyProperties = mPhysicalDevice.getQueueFamilyProperties();
	std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
	float queuePriority = 1.0f;
	for (uint32_t i = 0; i < queueFamilyProperties.size(); i++) {
		if (queueFamilyProperties[i].queueFlags & (vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer)) {
			queueCreateInfos.emplace_back(vk::DeviceQueueCreateInfo({}, i, 1, &queuePriority));
		}
	}

	std::vector<const char*> deviceExts;
	std::vector<const char*> validationLayers;
	for (const auto& s : mExtensions) deviceExts.emplace_back(s.c_str());
	for (const auto& s : instance.mValidationLayers) validationLayers.emplace_back(s.c_str());

	auto& createInfo = mCreateInfo.get<vk::DeviceCreateInfo>();
	createInfo.setQueueCreateInfos(queueCreateInfos);
	createInfo.setPEnabledLayerNames(validationLayers);
	createInfo.setPEnabledExtensionNames(deviceExts);
	createInfo.setPEnabledFeatures(&mFeatures);
	mDevice = mPhysicalDevice.createDevice(createInfo);

	const auto& properties = mPhysicalDevice.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceAccelerationStructurePropertiesKHR>();
	SetDebugName(*mDevice, "[" + std::to_string(properties.get<vk::PhysicalDeviceProperties2>().properties.deviceID) + "]: " + properties.get<vk::PhysicalDeviceProperties2>().properties.deviceName.data());

	mLimits = properties.get<vk::PhysicalDeviceProperties2>().properties.limits;
	mAccelerationStructureProperties = properties.get<vk::PhysicalDeviceAccelerationStructurePropertiesKHR>();

	mPipelineCache = vk::raii::PipelineCache(mDevice, {});

	VmaAllocatorCreateInfo allocatorInfo{};
	allocatorInfo.physicalDevice = *mPhysicalDevice;
	allocatorInfo.device   = *mDevice;
	allocatorInfo.instance = **instance;
	allocatorInfo.vulkanApiVersion = instance.mVulkanApiVersion;
	allocatorInfo.flags = 0;
	if (mExtensions.contains(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME))
		allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
	if (std::get<vk::PhysicalDeviceVulkan12Features>(mCreateInfo).bufferDeviceAddress)
		allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
	vmaCreateAllocator(&allocatorInfo, &mMemoryAllocator);
}
Device::~Device() {
	if (mMemoryAllocator != nullptr) {
		vmaDestroyAllocator(mMemoryAllocator);
		mMemoryAllocator = nullptr;
	}
}


void Device::LoadPipelineCache(const std::filesystem::path& path) {
	std::vector<uint8_t> cacheData;
	vk::PipelineCacheCreateInfo cacheInfo = {};
	try {
		cacheData = ReadFile<std::vector<uint8_t>>(path);
		if (!cacheData.empty()) {
			cacheInfo.pInitialData = cacheData.data();
			cacheInfo.initialDataSize = cacheData.size();
			std::cout << "Read pipeline cache (" << std::fixed << std::showpoint << std::setprecision(2) << cacheData.size()/1024.f << "KiB)" << std::endl;
		}
	} catch (std::exception& e) {
		std::cerr << "Warning: Failed to read pipeline cache: " << e.what() << std::endl;
	}
	mPipelineCache = vk::raii::PipelineCache(mDevice, cacheInfo);
}
void Device::StorePipelineCache(const std::filesystem::path& path) {
	try {
		const std::vector<uint8_t> cacheData = mPipelineCache.getData();
		if (!cacheData.empty())
			WriteFile(path, cacheData);
	} catch (std::exception& e) {
		std::cerr << "Warning: Failed to write pipeline cache: " << e.what() << std::endl;
	}
}

}