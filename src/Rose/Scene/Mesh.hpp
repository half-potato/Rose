#pragma once

#include <Rose/Core/Buffer.hpp>
#include <Rose/Core/Hash.hpp>
#include "AccelerationStructure.hpp"

namespace RoseEngine {

class CommandContext;
class ShaderModule;

enum class MeshVertexAttributeType {
	ePosition,
	eNormal,
	eTangent,
	eBinormal,
	eColor,
	eTexcoord,
	ePointSize,
	eBlendIndex,
	eBlendWeight
};
struct MeshVertexAttributeLayout {
	uint32_t            stride = sizeof(float) * 3;
	vk::Format          format = vk::Format::eR32G32B32Sfloat;
	uint32_t            offset = 0;
	vk::VertexInputRate inputRate = vk::VertexInputRate::eVertex;
	inline bool operator==(const MeshVertexAttributeLayout&) const = default;
};
using MeshVertexAttribute        = std::pair<BufferView/*vertex buffer*/, MeshVertexAttributeLayout>;
using MeshVertexAttributeBinding = std::pair<uint32_t  /*binding index*/, MeshVertexAttributeLayout>;
using MeshVertexAttributes        = std::unordered_map<MeshVertexAttributeType, std::vector<MeshVertexAttribute>>;
using MeshVertexAttributeBindings = std::unordered_map<MeshVertexAttributeType, std::vector<MeshVertexAttributeBinding>>;

struct MeshLayout {
	MeshVertexAttributeBindings vertexAttributeBindings = {};
	std::vector<vk::VertexInputBindingDescription>   bindings = {};
	std::vector<vk::VertexInputAttributeDescription> attributes = {};
	vk::PrimitiveTopology       topology  = vk::PrimitiveTopology::eTriangleList;
	bool                        hasIndices = true;

	inline bool operator==(const MeshLayout& rhs) const {
		if (topology != rhs.topology ||
			hasIndices != rhs.hasIndices)
			return false;

		if (vertexAttributeBindings.size() != rhs.vertexAttributeBindings.size())
			return false;

		for (const auto&[type, attribs] : vertexAttributeBindings) {
			if (auto it = rhs.vertexAttributeBindings.find(type); it == rhs.vertexAttributeBindings.end())
				return false;

			const auto& rhsAttribs = rhs.vertexAttributeBindings.at(type);
			if (attribs.size() != rhsAttribs.size())
				return false;

			for (size_t i = 0; i < attribs.size(); i++) {
				if (attribs[i].first != rhsAttribs[i].first || attribs[i].second != rhsAttribs[i].second)
					return false;
			}
		}
		return true;
	}
};

struct Mesh {
	MeshVertexAttributes  vertexAttributes = {};
	BufferView            indexBuffer = {};
	MeshVertexAttributes  vertexAttributesCpu = {};
	BufferView            indexBufferCpu = {};
	uint32_t              indexSize = sizeof(uint32_t);
	vk::PrimitiveTopology topology = {};
	vk::AabbPositionsKHR  aabb = {};
	AccelerationStructure blas = {};
	uint64_t              blasUpdateTime = 0;
	uint64_t              lastUpdateTime = 0;

	inline vk::IndexType IndexType() const { return indexSize == sizeof(uint32_t) ? vk::IndexType::eUint32 : vk::IndexType::eUint16; }

	MeshLayout GetLayout(const ShaderModule& vertexShader) const;
	void Bind(CommandContext& context, const MeshLayout& layout) const;
};

inline AccelerationStructure AccelerationStructure::Create(CommandContext& context, const Mesh& mesh, const bool opaque) {
	auto [positions, vertexLayout] = mesh.vertexAttributes.at(MeshVertexAttributeType::ePosition)[0];
	const uint32_t vertexCount = (uint32_t)((positions.size_bytes() - vertexLayout.offset) / vertexLayout.stride);
	const uint32_t primitiveCount = mesh.indexBuffer.size_bytes() / (mesh.indexSize * 3);

	vk::AccelerationStructureGeometryTrianglesDataKHR triangles {
		.vertexFormat = vertexLayout.format,
		.vertexData = context.GetDevice()->getBufferAddress(vk::BufferDeviceAddressInfo{ .buffer = **positions.mBuffer }) + positions.mOffset,
		.vertexStride = vertexLayout.stride,
		.maxVertex = vertexCount,
		.indexType = mesh.IndexType(),
		.indexData = context.GetDevice()->getBufferAddress(vk::BufferDeviceAddressInfo{ .buffer = **mesh.indexBuffer.mBuffer }) + mesh.indexBuffer.mOffset };

	vk::AccelerationStructureGeometryKHR geometry {
		.geometryType = vk::GeometryTypeKHR::eTriangles,
		.geometry = triangles,
		.flags = opaque ? vk::GeometryFlagBitsKHR::eOpaque : vk::GeometryFlagBitsKHR{}};

	vk::AccelerationStructureBuildRangeInfoKHR range{ .primitiveCount = primitiveCount };
	return Create(context, vk::AccelerationStructureTypeKHR::eBottomLevel, geometry, range);
}

}

namespace std {
template<>
struct hash<RoseEngine::MeshVertexAttributeLayout> {
	inline size_t operator()(const RoseEngine::MeshVertexAttributeLayout& v) const {
		return RoseEngine::HashArgs(v.stride, v.format, v.offset, v.inputRate);
	}
};

template<>
struct hash<RoseEngine::MeshLayout> {
	inline size_t operator()(const RoseEngine::MeshLayout& v) const {
		size_t h = 0;
		for (const auto[type, attribs] : v.vertexAttributeBindings) {
			h = RoseEngine::HashArgs(h, type);
			for (const auto&[a,i] : attribs)
				h = RoseEngine::HashArgs(h, a, i);
		}
		h = RoseEngine::HashArgs(h, v.topology, v.hasIndices);
		return h;
	}
};

inline std::string to_string(const RoseEngine::MeshVertexAttributeType& value) {
	switch (value) {
		case RoseEngine::MeshVertexAttributeType::ePosition:    return "Position";
		case RoseEngine::MeshVertexAttributeType::eNormal:      return "Normal";
		case RoseEngine::MeshVertexAttributeType::eTangent:     return "Tangent";
		case RoseEngine::MeshVertexAttributeType::eBinormal:    return "Binormal";
		case RoseEngine::MeshVertexAttributeType::eBlendIndex:  return "BlendIndex";
		case RoseEngine::MeshVertexAttributeType::eBlendWeight: return "BlendWeight";
		case RoseEngine::MeshVertexAttributeType::eColor:       return "Color";
		case RoseEngine::MeshVertexAttributeType::ePointSize:   return "PointSize";
		case RoseEngine::MeshVertexAttributeType::eTexcoord:    return "Texcoord";
		default: return "invalid ( " + vk::toHexString( static_cast<uint32_t>( value ) ) + " )";
	}
}
}