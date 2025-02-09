#pragma once

#include <WorkGraph/WorkNode.hpp>

namespace RoseEngine {

struct ResourceCopyNode {
	inline static const WorkNodeAttribute kOutputAttribute = { "output", WorkAttributeFlagBits::eOutput };
	inline static const WorkNodeAttribute kSrcAttribute = { "src", WorkAttributeFlagBits::eOptionalInput };
	inline static const WorkNodeAttribute kDstAttribute = { "dst", WorkAttributeFlagBits::eInput };

	// if src is disconnected, fill dst will fillValue
	uint32_t fillValue;

	// runtime data

	WorkNodeId nodeId;

	inline void operator()(CommandContext& context, WorkResourceMap& resources) {
		auto dst = GetResource<BufferParameter>(resources, {nodeId, kDstAttribute.name});
		if (!dst) return;

		auto src = GetResource<BufferParameter>(resources, {nodeId, kSrcAttribute.name});
		if (src) {
			context.Copy(src, dst);
		} else {
			context.Fill<uint32_t>(dst, fillValue, 0u);
		}

		resources[{nodeId, kOutputAttribute.name}] = dst;
	}
};

template<> constexpr static const char* kSerializedTypeName<ResourceCopyNode> = "ResourceCopyNode";

template<>
inline void Serialize(json& data, const ResourceCopyNode& node) {
	data["fillValue"] = node.fillValue;
}

template<>
inline void Deserialize(const json& data, ResourceCopyNode& node) {
	node.fillValue = data["fillValue"];
}


inline std::array<WorkNodeAttribute,2> GetAttributes(const ResourceCopyNode& node) {
	return { ResourceCopyNode::kSrcAttribute, ResourceCopyNode::kDstAttribute };
}

inline void DrawNode(ResourceCopyNode& node) {
	DrawNodeTitle("Copy Resource");
	DrawNodeAttribute(node.nodeId, ResourceCopyNode::kOutputAttribute);
	DrawNodeAttribute(node.nodeId, ResourceCopyNode::kSrcAttribute);
	DrawNodeAttribute(node.nodeId, ResourceCopyNode::kDstAttribute);
}
}
