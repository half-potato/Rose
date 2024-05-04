#pragma once

#include <ranges>

#include <Core/CommandContext.hpp>

namespace RoseEngine {

struct Mesh;

struct AccelerationStructure {
	ref<vk::raii::AccelerationStructureKHR> accelerationStructure;
	BufferView buffer;

	inline static AccelerationStructure Create(CommandContext& context, const vk::AccelerationStructureTypeKHR type, const vk::ArrayProxy<const vk::AccelerationStructureGeometryKHR>& geometries, const vk::ArrayProxy<const vk::AccelerationStructureBuildRangeInfoKHR>& buildRanges) {
		vk::AccelerationStructureBuildGeometryInfoKHR buildGeometry {
			.type  = type,
			.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace,
			.mode  = vk::BuildAccelerationStructureModeKHR::eBuild };
		buildGeometry.setGeometries(geometries);

		vk::AccelerationStructureBuildSizesInfoKHR buildSizes;
		if (buildRanges.size() > 0 && buildRanges.front().primitiveCount > 0) {
			std::vector<uint32_t> counts((uint32_t)geometries.size());
			for (uint32_t i = 0; i < geometries.size(); i++)
				counts[i] = (buildRanges.data() + i)->primitiveCount;
			buildSizes = context.GetDevice()->getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, buildGeometry, counts);
		} else
			buildSizes.accelerationStructureSize = buildSizes.buildScratchSize = 4;

		auto buffer = Buffer::Create(
			context.GetDevice(),
			buildSizes.accelerationStructureSize,
			vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress);

		auto scratchData = context.GetTransientBuffer(
			buildSizes.buildScratchSize,
			vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eStorageBuffer);

		ref<vk::raii::AccelerationStructureKHR> accelerationStructure = make_ref<vk::raii::AccelerationStructureKHR>(*context.GetDevice(), vk::AccelerationStructureCreateInfoKHR{
			.buffer = **buffer.mBuffer,
			.offset = buffer.mOffset,
			.size = buffer.size_bytes(),
			.type = type });

		buildGeometry.dstAccelerationStructure = **accelerationStructure;
		buildGeometry.scratchData = context.GetDevice()->getBufferAddress(vk::BufferDeviceAddressInfo{ .buffer = **scratchData.mBuffer }) + scratchData.mOffset;

		context->buildAccelerationStructuresKHR(buildGeometry, buildRanges.data());

		buffer.SetState(Buffer::ResourceState{
			.stage = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
			.access = vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
			.queueFamily = context.QueueFamily() });

		return { accelerationStructure, buffer };
	}

	template<std::ranges::contiguous_range R> //requires(std::is_same_v<std::ranges::range_value_t<R>, vk::AccelerationStructureInstanceKHR>)
	inline static AccelerationStructure Create(CommandContext& context, R&& instances) {
		auto instanceBuf = context.UploadData(instances, vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eShaderDeviceAddress);

		vk::AccelerationStructureGeometryInstancesDataKHR instanceGeometries{
			.data = context.GetDevice()->getBufferAddress(vk::BufferDeviceAddressInfo{ .buffer = **instanceBuf.mBuffer }) };

		vk::AccelerationStructureGeometryKHR geometry{
			.geometryType = vk::GeometryTypeKHR::eInstances,
			.geometry = instanceGeometries };

		vk::AccelerationStructureBuildRangeInfoKHR range{ .primitiveCount = (uint32_t)std::ranges::size(instances) };
		return Create(context, vk::AccelerationStructureTypeKHR::eTopLevel, geometry, range);
	}

	inline static AccelerationStructure Create(CommandContext& context, const float3 aabbMin, const float3 aabbMax, const bool opaque = true) {
		vk::AabbPositionsKHR aabb{
			.minX = aabbMin.x, .minY = aabbMin.y, .minZ = aabbMin.z,
			.maxX = aabbMax.x, .maxY = aabbMax.y, .maxZ = aabbMax.z };

		auto aabbBuf = context.UploadData(std::span<vk::AabbPositionsKHR>(&aabb, 1), vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR);

		vk::AccelerationStructureGeometryAabbsDataKHR aabbs{
			.data = context.GetDevice()->getBufferAddress(vk::BufferDeviceAddressInfo{ .buffer = **aabbBuf.mBuffer }) + aabbBuf.mOffset,
			.stride = sizeof(vk::AabbPositionsKHR) };

		vk::AccelerationStructureGeometryKHR geometry{
			.geometryType = vk::GeometryTypeKHR::eAabbs,
			.geometry = aabbs,
			.flags = opaque ? vk::GeometryFlagBitsKHR::eOpaque : vk::GeometryFlagBitsKHR{}};

		vk::AccelerationStructureBuildRangeInfoKHR range{ .primitiveCount = 1 };
		return Create(context, vk::AccelerationStructureTypeKHR::eBottomLevel, geometry, range);
	}

	static AccelerationStructure Create(CommandContext& context, const Mesh& mesh, const bool opaque = true);
};

}