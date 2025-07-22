#pragma once

#include <Rose/Core/CommandContext.hpp>
#include <Rose/Core/TransientResourceCache.hpp>
#include "DeviceRadixSort.h"
#include "Tuner.h"
#include "GPUSorting.h"

namespace RoseEngine {

const uint32_t k_isNotPartialBitFlag = 0;
const uint32_t k_isPartialBitFlag = 1;
const uint32_t k_maxDim = 65535;
const uint32_t k_radix = 256; // 8 bits per pass
const uint32_t k_radixPasses = 4; // 32-bit keys
const uint32_t k_maxReadback = 1 << 13; // 32-bit keys

static inline uint32_t divRoundUp(uint32_t x, uint32_t y)
{
	return (x + y - 1) / y;
}

GPUSorting::DeviceInfo GetDeviceInfo(const RoseEngine::Device& device)
{
	GPUSorting::DeviceInfo devInfo = {};
	const vk::raii::PhysicalDevice& physicalDevice = device.PhysicalDevice();

	// --- Query Core Properties, Subgroup Properties, and 16-bit/Vulkan 1.2 Features ---
	// We chain structures together to get all the information in one call.

	// 1. For features (like 16-bit support)
	vk::PhysicalDeviceShaderFloat16Int8Features features16bit = {};
	vk::PhysicalDeviceVulkan12Features features12 = {};
	features16bit.pNext = &features12; // Chain 1.2 features after 16-bit features

	vk::PhysicalDeviceFeatures2 features2 = {};
	features2.pNext = &features16bit; // The head of our feature chain
	physicalDevice.getFeatures2(&features2);

	// 2. For properties (like device name, limits, and subgroup/wave info)
	vk::PhysicalDeviceSubgroupProperties subgroupProperties = {};

	vk::PhysicalDeviceProperties2 properties2 = {};
	properties2.pNext = &subgroupProperties; // The head of our properties chain
	physicalDevice.getProperties2(&properties2);

	vk::PhysicalDeviceProperties props = properties2.properties;

	// --- Populate DeviceInfo from queried Vulkan structures ---

	devInfo.Description = props.deviceName.data();
	devInfo.deviceId = props.deviceID;
	devInfo.vendorId = props.vendorID;

	// Determine if the device is a software renderer (like LLVMPipe or SwiftShader)
	bool isSoftwareDevice = (props.deviceType == vk::PhysicalDeviceType::eCpu);

	// Get memory properties to calculate dedicated and shared memory
	vk::PhysicalDeviceMemoryProperties memProps = physicalDevice.getMemoryProperties();
	for (uint32_t i = 0; i < memProps.memoryHeapCount; ++i)
	{
		if (memProps.memoryHeaps[i].flags & vk::MemoryHeapFlagBits::eDeviceLocal)
		{
			devInfo.dedicatedVideoMemory += memProps.memoryHeaps[i].size;
		}
		else // Heaps that are not device-local can be considered shared
	{
			devInfo.sharedSystemMemory += memProps.memoryHeaps[i].size;
		}
	}

	// Vulkan doesn't use Shader Models like D3D12. We'll use the Vulkan API version.
	// A check for Vulkan 1.1+ is a good proxy for "modern" compute capabilities.
	uint32_t major = VK_API_VERSION_MAJOR(props.apiVersion);
	uint32_t minor = VK_API_VERSION_MINOR(props.apiVersion);
	devInfo.SupportedShaderModel = "Vulkan " + std::to_string(major) + "." + std::to_string(minor);
	bool hasModernAPIVersion = props.apiVersion >= VK_API_VERSION_1_1;

	// Subgroup properties map to D3D12's wave intrinsics
	devInfo.SIMDWidth = subgroupProperties.subgroupSize;
	devInfo.SIMDMaxWidth = subgroupProperties.subgroupSize; // Min/Max are the same in Vulkan
	devInfo.SIMDLaneCount = subgroupProperties.subgroupSize;

	// Check for required subgroup operations. 'Ballot' is a key one for many algorithms.
	devInfo.SupportsWaveIntrinsics =
		(subgroupProperties.supportedOperations & vk::SubgroupFeatureFlagBits::eBallot) &&
		(subgroupProperties.supportedStages & vk::ShaderStageFlagBits::eCompute);

	// Check for 16-bit type support
	devInfo.Supports16BitTypes = features16bit.shaderFloat16 && features16bit.shaderInt16;

	// --- Derive sorting support based on the queried features ---
	devInfo.SupportsDeviceRadixSort =
		devInfo.SIMDWidth >= 4 &&          // A reasonable minimum subgroup size
		devInfo.SupportsWaveIntrinsics &&  // Must support subgroup ops
		hasModernAPIVersion;               // Ensures a baseline of modern features

	// OneSweep is an optimization that should not be used on slower software devices
	devInfo.SupportsOneSweep = devInfo.SupportsDeviceRadixSort && !isSoftwareDevice;

#ifdef _DEBUG
	auto to_mb = [](uint64_t bytes) { return bytes / (1024 * 1024); };
	std::cout << "--- Vulkan Device Info ---" << std::endl;
	std::cout << "Device:                   " << devInfo.Description << std::endl;
	std::cout << "API Version:              " << devInfo.SupportedShaderModel << std::endl;
	std::cout << "Subgroup (Wave) Size:     " << devInfo.SIMDWidth << std::endl;
	std::cout << "Dedicated VRAM (MB):      " << to_mb(devInfo.dedicatedVideoMemory) << std::endl;
	std::cout << "Shared System RAM (MB):   " << to_mb(devInfo.sharedSystemMemory) << std::endl;
	std::cout << "Supports Subgroup Ops:    " << (devInfo.SupportsWaveIntrinsics ? "Yes" : "No") << std::endl;
	std::cout << "Supports 16-Bit Types:    " << (devInfo.Supports16BitTypes ? "Yes" : "No") << std::endl;
	std::cout << "Supports GPU Radix Sort:  " << (devInfo.SupportsDeviceRadixSort ? "Yes" : "No") << std::endl;
	std::cout << "Supports OneSweep Sort:   " << (devInfo.SupportsOneSweep ? "Yes" : "No") << std::endl << std::endl;
#endif

	return devInfo;
}

void AddBufferBarrier(
    CommandContext& context,
    const BufferRange<uint32_t>& buffer,
    vk::PipelineStageFlags2 srcStage, vk::AccessFlags2 srcAccess,
    vk::PipelineStageFlags2 dstStage, vk::AccessFlags2 dstAccess)
{
    auto barrier = buffer.CreateBarrier(srcStage, srcAccess, dstStage, dstAccess);
    vk::DependencyInfo depInfo{ .dependencyFlags = vk::DependencyFlagBits::eByRegion };
    depInfo.setBufferMemoryBarriers(barrier);
    context->pipelineBarrier2(depInfo);
}

class DeviceRadixSort {
private:
	// payload size -> (histogramPipeline, sortPipeline)
	std::tuple<ref<Pipeline>, ref<Pipeline>, ref<Pipeline>, ref<Pipeline>> pipelines;
	ref<Pipeline> initPipeline;
	ref<Pipeline> upsweepPipeline;
	ref<Pipeline> scanPipeline;
	ref<Pipeline> downsweepPipeline;

public:
	inline void operator()(CommandContext& context, const BufferRange<uint>& keys, const BufferRange<uint>& payloads) {
/
		auto&[initPipeline, upsweepPipeline, scanPipeline, downsweepPipeline] = pipelines;
		if (!initPipeline) {
			GPUSorting::DeviceInfo devInfo = GetDeviceInfo(&context.GetDevice());
			GPUSorting::TuningParameters k_tuningParameters = Tuner::GetTuningParameters(devInfo, GPUSorting::MODE::MODE_PAIRS)
			// TODO set parameters using TuningParameters
			ShaderDefines defs {
                { "LOCK_TO_W32",     std::to_string(m_tuning.shouldLockWavesTo32) },
                { "KEYS_PER_THREAD", std::to_string(m_tuning.keysPerThread) },
                { "D_DIM",           std::to_string(m_tuning.threadsPerThreadblock) },
                { "D_TOTAL_SMEM",    std::to_string(m_tuning.totalSharedMemory) },
                { "PART_SIZE",       std::to_string(m_tuning.partitionSize) },
                { "KEY_UINT",        "true" },
                { "PAYLOAD_UINT",    "true" },
                { "SHOULD_ASCEND",   "true" },
                { "SORT_PAIRS",      "true" }
			};
			auto shaderFile = FindShaderPath("DeviceRadixSort.slang");
			initPipeline = Pipeline::CreateCompute(context.GetDevice(), ShaderModule::Create(context.GetDevice(), shaderFile, "InitDeviceRadixSort", "sm_6_7", defs));
			upsweepPipeline = Pipeline::CreateCompute(context.GetDevice(), ShaderModule::Create(context.GetDevice(), shaderFile, "Upsweep", "sm_6_7", defs));
			scanPipeline = Pipeline::CreateCompute(context.GetDevice(), ShaderModule::Create(context.GetDevice(), shaderFile, "Scan", "sm_6_7", defs));
			downsweepPipeline = Pipeline::CreateCompute(context.GetDevice(), ShaderModule::Create(context.GetDevice(), shaderFile, "Downsweep", "sm_6_7", defs));
		}

		// TODO these values need to be obtained
		uint32_t threadBlocks = divRoundUp(numKeys, k_tuningParameters.partitionSize);

		auto m_globalHistBuffer = context.GetTransientBuffer<uint32_t>(
			k_radix * k_radixPasses,
			vk::BufferUsageFlagBits::eStorageBuffer);
		// auto m_errorCountBuffer = context.GetTransientBuffer<uint32_t>(
		// 	1
		// 	vk::BufferUsageFlagBits::eStorageBuffer);
		// auto m_readBackBuffer = context.GetTransientBuffer<uint32_t>(
		// 	k_maxReadBack,
		// 	vk::BufferUsageFlagBits::eStorageBuffer);

		auto m_sortBuffer = keys;
		auto m_altBuffer = context.GetTransientBuffer<uint32_t>(
			numKeys,
			vk::BufferUsageFlagBits::eStorageBuffer);
		auto m_passHistBuffer = context.GetTransientBuffer<uint32_t>(
			k_radix * threadBlocks,
			vk::BufferUsageFlagBits::eStorageBuffer);
		auto m_sortPayloadBuffer = payloads;
		auto m_altPayloadBuffer = context.GetTransientBuffer<uint32_t>(
			numKeys,
			vk::BufferUsageFlagBits::eStorageBuffer);

		auto descriptorSets = context.GetDescriptorSets(*sortPipeline->Layout());
		ShaderParameter params;
		params["b_sort"] = (BufferParameter)m_sortBuffer; // u0
		params["b_alt"] = (BufferParameter)m_altBuffer; // u1
		params["b_sortPayload"] = (BufferParameter)m_sortPayloadBufferBuffer; // u2
		params["b_altPayload"] = (BufferParameter)m_altPayloadBuffer; // u3
		params["b_globalHist"] = (BufferParameter)m_globalHistBuffer; //u4
		params["b_passHist"] = (BufferParameter)m_passHistBuffer; // u5
		// params["b_index"] = (BufferParameter); // u6

		context.UpdateDescriptorSets(*descriptorSets, params, *sortPipeline->Layout());

		RadixSortPushConstants pushConstants;
		pushConstants.numKeys = numKeys;
		pushConstants.radixShift = 0;
		pushConstants.threadBlocks = threadBlocks;
		pushConstants.isPartial = k_isNotPartialBitFlag;

		vk::BufferMemoryBarrier2 barriers[] {
			m_passHistBuffer.SetState(Buffer::ResourceState{
				.stage = vk::PipelineStageFlagBits2::eComputeShader,
				.access = vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite,
				.queueFamily = context.QueueFamily()
			}),
			m_globalHistBuffer.SetState(Buffer::ResourceState{
				.stage = vk::PipelineStageFlagBits2::eComputeShader,
				.access = vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite,
				.queueFamily = context.QueueFamily()
			}),
			m_sortBuffer.SetState(Buffer::ResourceState{
				.stage = vk::PipelineStageFlagBits2::eComputeShader,
				.access = vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite,
				.queueFamily = context.QueueFamily()
			}),
			m_sortPayloadBuffer.SetState(Buffer::ResourceState{
				.stage = vk::PipelineStageFlagBits2::eComputeShader,
				.access = vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite,
				.queueFamily = context.QueueFamily()
			}),
			m_altBuffer.SetState(Buffer::ResourceState{
				.stage = vk::PipelineStageFlagBits2::eComputeShader,
				.access = vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite,
				.queueFamily = context.QueueFamily()
			})
			m_altPayloadBuffer.SetState(Buffer::ResourceState{
				.stage = vk::PipelineStageFlagBits2::eComputeShader,
				.access = vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite,
				.queueFamily = context.QueueFamily()
			})
		};
		vk::DependencyInfo depInfo { .dependencyFlags = vk::DependencyFlagBits::eByRegion };
		depInfo.setBufferMemoryBarriers(barriers);
		// TODO change barriers to be more specific

		context->bindPipeline(vk::PipelineBindPoint::eCompute, **initPipeline);
		context.BindDescriptors(*initPipeline->Layout(), *descriptorSets);
		context->dispatch(1, 1, 1);
        AddBufferBarrier(context, m_globalHistBuffer,
            vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite,
            vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead);
		for (uint32_t radixShift = 0; radixShift < 32; radixShift += 8) {
			pushConstants.radixShift = radixShift;

			// Upsweep kernel
			{
				context->bindPipeline(vk::PipelineBindPoint::eCompute, **upsweepPipeline);
				context.BindDescriptors(*upsweepPipeline->Layout(), *descriptorSets);
				const uint32_t fullBlocks = threadBlocks / k_maxDim;
				const uint32_t partialBlocks = threadBlocks - fullBlocks * k_maxDim;
				if (fullBlocks) {
					pushConstants.isPartial = k_isNotPartialBitFlag;
					context->pushConstants<RadixSortPushConstants>(**upsweepPipeline->Layout(), vk::ShaderStageFlagBits::eCompute, 0u, pushConstants);
					context->dispatch(k_maxDim, fullBlocks, 1);
				}
				if (partialBlocks) {
					pushConstants.isPartial = fullBlocks << 1 | k_isPartialBitFlag;
					context->pushConstants<RadixSortPushConstants>(**upsweepPipeline->Layout(), vk::ShaderStageFlagBits::eCompute, 0u, pushConstants);
					context->dispatch(partialBlocks, 1, 1);
				}
			}

            AddBufferBarrier(context, m_passHistBuffer,
                vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite,
                vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead);


			// Scan Kernel
			{
				context->bindPipeline(vk::PipelineBindPoint::eCompute, **scanPipeline);
				context.BindDescriptors(*scanPipeline->Layout(), *descriptorSets);
				context->pushConstants<RadixSortPushConstants>(**scanPipeline->Layout(), vk::ShaderStageFlagBits::eCompute, 0u, pushConstants);
				context->dispatch(256, 1, 1);
			}
            AddBufferBarrier(context, m_passHistBuffer,
                vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite,
                vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead);
            AddBufferBarrier(context, m_globalHistBuffer,
                vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite,
                vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead);

			// Downsweep Kernel
			{
				context->bindPipeline(vk::PipelineBindPoint::eCompute, **downsweepPipeline);
				context.BindDescriptors(*downsweepPipeline->Layout(), *descriptorSets);
				const uint32_t fullBlocks = threadBlocks / k_maxDim;
				const uint32_t partialBlocks = threadBlocks - fullBlocks * k_maxDim;
				if (fullBlocks) {
					pushConstants.isPartial = k_isNotPartialBitFlag;
					context->pushConstants<RadixSortPushConstants>(**downsweepPipeline->Layout(), vk::ShaderStageFlagBits::eCompute, 0u, pushConstants);
					context->dispatch(k_maxDim, fullBlocks, 1);
				}
				if (partialBlocks) {
					pushConstants.isPartial = fullBlocks << 1 | k_isPartialBitFlag;
					context->pushConstants<RadixSortPushConstants>(**downsweepPipeline->Layout(), vk::ShaderStageFlagBits::eCompute, 0u, pushConstants);
					context->dispatch(partialBlocks, 1, 1);
				}
			}
            AddBufferBarrier(context, dstKeyBuffer,
                vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite,
                vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead);
            AddBufferBarrier(context, dstPayloadBuffer,
                vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite,
                vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead);

			// Swap doesn't work
			swap(m_sortBuffer, m_altBuffer);
			swap(m_sortPayloadBuffer, m_altPayloadBuffer);
			params["b_sort"] = (BufferParameter)m_sortBuffer; // u0
			params["b_alt"] = (BufferParameter)m_altBuffer; // u1
			params["b_sortPayload"] = (BufferParameter)m_sortPayloadBufferBuffer; // u2
			params["b_altPayload"] = (BufferParameter)m_altPayloadBuffer; // u3

			context.UpdateDescriptorSets(*descriptorSets, params, *sortPipeline->Layout());
		}

		// TODO figure out how to get the data out
		context.AddBarrier(keys.SetState(Buffer::ResourceState{
			.stage = vk::PipelineStageFlagBits2::eComputeShader,
			.access = vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite,
			.queueFamily = context.QueueFamily()
		}));

	}
};

}
#include
