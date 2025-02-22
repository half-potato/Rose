#include "Mesh.hpp"

#include <Rose/Core/ShaderModule.hpp>
#include <Rose/Core/CommandContext.hpp>

namespace RoseEngine {

void Mesh::Bind(CommandContext& context, const MeshLayout& layout) const {
	for (const auto&[type, bindings] : layout.vertexAttributeBindings) {
		const auto& attributes = vertexAttributes.at(type);
		if (bindings.size() > attributes.size()) throw std::runtime_error("Attribute needed by pipeline not found in mesh");
		for (size_t i = 0; i < bindings.size(); i++) {
			const auto& buf = attributes[i].first;
			context->bindVertexBuffers(bindings[i].first, **buf.mBuffer, buf.mOffset);
		}
	}

	if (indexBuffer)
		context->bindIndexBuffer(**indexBuffer.mBuffer, indexBuffer.mOffset, IndexType());
}

struct stride_view_hash {
	inline size_t operator()(const std::pair<BufferView, uint32_t>& v) const {
		return HashArgs(**v.first.mBuffer, v.first.mOffset, v.second);
	}
};
using BufferBindingMap = std::unordered_map<std::pair<BufferView, uint32_t>, uint32_t, stride_view_hash>;

void ReflectVertexInputs(const ShaderParameterBinding& binding, const Mesh& mesh, MeshLayout& layout, BufferBindingMap& uniqueBuffers) {
	static const std::unordered_map<std::string, MeshVertexAttributeType> attributeNameMap {
		{ "position",    MeshVertexAttributeType::ePosition },
		{ "normal",      MeshVertexAttributeType::eNormal },
		{ "tangent",     MeshVertexAttributeType::eTangent },
		{ "binormal",    MeshVertexAttributeType::eBinormal },
		{ "color",       MeshVertexAttributeType::eColor },
		{ "texcoord",    MeshVertexAttributeType::eTexcoord },
		{ "pointsize",   MeshVertexAttributeType::ePointSize },
		{ "blendindex",  MeshVertexAttributeType::eBlendIndex },
		{ "blendweight", MeshVertexAttributeType::eBlendWeight }
	};
	if (const auto* attrib = binding.get_if<ShaderVertexAttributeBinding>()) {
		std::string s = attrib->semantic;
		std::ranges::transform(attrib->semantic, s.begin(), static_cast<int(*)(int)>(std::tolower));
		if (auto attrib_it = attributeNameMap.find(s); attrib_it != attributeNameMap.end()) {
			MeshVertexAttributeType attributeType = attrib_it->second;

			auto it = mesh.vertexAttributes.find(attributeType);
			if (it == mesh.vertexAttributes.end() || it->second.size() <= attrib->semanticIndex)
				throw std::logic_error("Mesh does not contain required shader input " + std::to_string(attributeType) + "." + std::to_string(attrib->semanticIndex));
			const auto& [vertexBuffer, attributeDescription] = it->second[attrib->semanticIndex];

			// get/create attribute in attribute array
			auto& dstAttribs = layout.vertexAttributeBindings[attributeType];
			if (dstAttribs.size() <= attrib->semanticIndex)
				dstAttribs.resize(attrib->semanticIndex + 1);
			auto& [dstBindingIndex, dstAttribDesc] = dstAttribs[attrib->semanticIndex];

			// store attribute description
			dstAttribDesc = attributeDescription;

			// get unique binding index for buffer+stride
			const auto key = std::make_pair(vertexBuffer, attributeDescription.stride);
			if (auto it = uniqueBuffers.find(key); it != uniqueBuffers.end())
				dstBindingIndex = it->second;
			else {
				dstBindingIndex = (uint32_t)uniqueBuffers.size();
				uniqueBuffers.emplace(key, dstBindingIndex);
			}

			// add attribute
			layout.attributes.emplace_back(vk::VertexInputAttributeDescription{
				.location=attrib->location,
				.binding=dstBindingIndex,
				.format=attributeDescription.format,
				.offset=attributeDescription.offset });
		}
	}
	for (const auto&[name, b] : binding)
		ReflectVertexInputs(b, mesh, layout, uniqueBuffers);
}

MeshLayout Mesh::GetLayout(const ShaderModule& vertexShader) const {
	MeshLayout layout {
		.topology = topology,
		.hasIndices = indexBuffer };

	BufferBindingMap uniqueBuffers = {};
	ReflectVertexInputs(vertexShader.RootBinding(), *this, layout, uniqueBuffers);

	for (const auto& [attributeType, bindings] : layout.vertexAttributeBindings) {
		for (const auto[index, binding] : bindings) {
			if (layout.bindings.size() <= index)
				layout.bindings.resize(index + 1);
			layout.bindings[index] = vk::VertexInputBindingDescription{
				.binding = index,
				.stride  = binding.stride,
				.inputRate = binding.inputRate };
		}
	}

	return layout;
}

}