#pragma once

#include <WorkGraph/WorkNode.hpp>
#include <Core/TransientResourceCache.hpp>
#include <Core/Gui.hpp>

namespace RoseEngine {

struct ResourceCreateNode {
	inline static const std::array<WorkNodeAttribute, 4> kInputAttributes = {
		WorkNodeAttribute{ "count",       WorkAttributeFlagBits::eInput },
		WorkNodeAttribute{ "bufferSize",  WorkAttributeFlagBits::eInput },
		WorkNodeAttribute{ "bufferUsage", WorkAttributeFlagBits::eInput },
		WorkNodeAttribute{ "memoryFlags", WorkAttributeFlagBits::eInput },
	};

	uint32_t outputCount = 1;

	union {
		struct {
			vk::DeviceSize          size;
			vk::BufferUsageFlags    usage;
			vk::MemoryPropertyFlags memoryFlags;
		} buffer;
	} resourceCreateInfo = {};

	// runtime data

	WorkNodeId nodeId;
	TransientResourceCache<WorkResource> cachedResources;

	inline WorkResource CreateResource(const Device& device, const std::string& id) {
		return Buffer::Create(device, resourceCreateInfo.buffer.size, resourceCreateInfo.buffer.usage, resourceCreateInfo.buffer.memoryFlags);
	}

	inline void operator()(CommandContext& context, WorkResourceMap& resources) {
		auto create_fn = [&](){ return CreateResource(context.GetDevice(), "work resource"); };

		for (uint32_t i = 0; i < outputCount; i++) {
			WorkResource r = cachedResources.pop_or_create(context.GetDevice(), create_fn);
			cachedResources.push(r, context.GetDevice().NextTimelineSignal());
			resources[{nodeId, (outputCount > 1) ? "output" + std::to_string(i) : "output"}] = r;
		}
	}
};
template<> constexpr static const char* kSerializedTypeName<ResourceCreateNode> = "ResourceCreateNode";

inline json& operator<<(json& data, const ResourceCreateNode& node) {
	data["bufferSize"]  = node.resourceCreateInfo.buffer.size;
	data["bufferUsage"] = (uint32_t)node.resourceCreateInfo.buffer.usage;
	data["memoryFlags"] = (uint32_t)node.resourceCreateInfo.buffer.memoryFlags;
	return data;
}
inline const json& operator>>(const json& data, ResourceCreateNode& node) {
	node.resourceCreateInfo.buffer.size = data["bufferSize"];
	node.resourceCreateInfo.buffer.usage       = (vk::BufferUsageFlags)data["bufferUsage"].get<uint32_t>();
	node.resourceCreateInfo.buffer.memoryFlags = (vk::MemoryPropertyFlags)data["memoryFlags"].get<uint32_t>();
	return data;
}

inline auto GetAttributes(const ResourceCreateNode& node) {
	std::vector<WorkNodeAttribute> attribs(ResourceCreateNode::kInputAttributes.size() + node.outputCount);
	if (node.outputCount > 1) {
		for (uint32_t i = 0; i < node.outputCount; i++) {
			attribs[i] = WorkNodeAttribute{ "output" + std::to_string(i), WorkAttributeFlagBits::eOutput };
		}
	} else {
		attribs[0] = WorkNodeAttribute{ "output", WorkAttributeFlagBits::eOutput };
	}
	std::ranges::copy(ResourceCreateNode::kInputAttributes, attribs.begin() + node.outputCount);
	return attribs;
}

inline void DrawNode(CommandContext& context, ResourceCreateNode& node) {
	DrawNodeTitle("Create Resource");
	if (node.outputCount > 1) {
		for (uint32_t i = 0; i < node.outputCount; i++) {
			DrawNodeAttribute(node.nodeId, WorkNodeAttribute{ "output" + std::to_string(i), WorkAttributeFlagBits::eOutput });
		}
	} else {
		DrawNodeAttribute(node.nodeId, WorkNodeAttribute{ "output", WorkAttributeFlagBits::eOutput });
	}
	for (const auto& attrib : ResourceCreateNode::kInputAttributes)
		DrawNodeAttribute(node.nodeId, attrib);

	ImGui::SetNextItemWidth(200);
	Gui::ScalarField("Size", &node.resourceCreateInfo.buffer.size);

	ImGui::SetNextItemWidth(200);
	Gui::vkFlagDropdown("Usage flags", node.resourceCreateInfo.buffer.usage);
}

}
