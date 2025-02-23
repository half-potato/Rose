#pragma once

#include <variant>
#include <imgui/imgui_stdlib.h>
#include <json.hpp>
#include <Rose/Core/RoseEngine.hpp>

namespace RoseEngine {

using nlohmann::json;

using WorkNodeId = size_t;

enum WorkAttributeFlagBits : uint32_t {
	eNone     = 0,
	eOutput   = (1 << 0),
	eInput    = (1 << 1),
	eOptional = (1 << 2),

	eOptionalInput = eInput | eOptional
};
using WorkAttributeFlags = vk::Flags<WorkAttributeFlagBits>;
struct WorkNodeAttribute {
	std::string        name;
	WorkAttributeFlags flags = WorkAttributeFlagBits::eNone;
};

struct WorkAttributePointer {
	WorkNodeId  node;
	std::string attribute;
	bool operator==(const WorkAttributePointer& rhs) const = default;
};
struct WorkAttributePointerHasher { inline size_t operator()(const WorkAttributePointer& c) const { return HashArgs(c.node, c.attribute); } };

// imnode node id
inline int GetImNodeId(const WorkNodeId id) { return (int)id; }
// imnode node attrib id
inline int GetImNodeId(const WorkAttributePointer& connection) { return (int)HashArgs(connection.node, connection.attribute); }
// imnode node attrib id
inline int GetImNodeId(const WorkNodeId node, const std::string& attribute) { return GetImNodeId(WorkAttributePointer{ node, attribute }); }
// imnode link id
inline int GetImNodeId(const WorkAttributePointer& src, const WorkAttributePointer& dst) { return (int)HashArgs(GetImNodeId(src), GetImNodeId(dst)); }

inline WorkNodeId GetUniqueNodeId() { return std::chrono::high_resolution_clock::now().time_since_epoch().count(); }

inline void DrawNodeTitle(const char* title) {
	ImNodes::BeginNodeTitleBar();
	ImGui::TextUnformatted(title);
	ImNodes::EndNodeTitleBar();
}
inline void DrawNodeAttribute(const WorkNodeId nodeId, const WorkNodeAttribute& attribute, auto draw_fn) {
	auto&[name, flag] = attribute;

	int id = GetImNodeId(nodeId, name);

	if (flag & WorkAttributeFlagBits::eInput)
		ImNodes::BeginInputAttribute (id);
	else if (flag & WorkAttributeFlagBits::eOutput)
		ImNodes::BeginOutputAttribute(id);
	else
		ImNodes::BeginStaticAttribute(id);

	ImGui::TextUnformatted(name.c_str());
	draw_fn();

	if (flag & WorkAttributeFlagBits::eInput)
		ImNodes::EndInputAttribute();
	else if (flag & WorkAttributeFlagBits::eOutput)
		ImNodes::EndOutputAttribute();
	else
		ImNodes::EndStaticAttribute();
}
inline void DrawNodeAttribute(const WorkNodeId nodeId, const WorkNodeAttribute& attribute) {
	DrawNodeAttribute(nodeId, attribute, [](){});
}

using WorkResource = std::variant<
	ConstantParameter,
	BufferParameter,
	ImageParameter,
	AccelerationStructureParameter>;

// node attrib -> resource
using WorkResourceMap = std::unordered_map<WorkAttributePointer, WorkResource, WorkAttributePointerHasher>;
template<typename T>
inline T GetResource(const WorkResourceMap& resources, const WorkAttributePointer& attribute) {
	if (auto it = resources.find(attribute); it != resources.end()) {
		if (const T* n = std::get_if<T>(&it->second)) {
			return *n;
		}
	}
	return T{};
}

template<typename...NodeTypes>
class WorkGraph {
public:
	using NodeType = std::variant<NodeTypes...>;

	std::unordered_map<WorkNodeId, NodeType> nodes;

	// dst input attrib -> src output attrib
	std::unordered_map<WorkAttributePointer, WorkAttributePointer, WorkAttributePointerHasher> edges;

	using iterator       = std::unordered_map<WorkNodeId, NodeType>::iterator;
	using const_iterator = std::unordered_map<WorkNodeId, NodeType>::const_iterator;

	inline iterator begin() { return nodes.begin(); }
	inline iterator end() { return nodes.end(); }
	inline const_iterator begin() const { return nodes.begin(); }
	inline const_iterator end() const { return nodes.end(); }

	inline NodeType& operator[](const WorkNodeId id) { return nodes[id]; }
	inline void erase(const WorkNodeId id) {
		// erase edges adjacent to node
		for (auto it = edges.begin(); it != edges.end();) {
			const auto&[src,dst] = *it;
			if (src.node == id || dst.node == id)
				it = edges.erase(it);
			else
				it++;
		}

		nodes.erase(id);
	};

	inline NodeType* find(const WorkNodeId id) {
		if (auto it = nodes.find(id); it != nodes.end())
			return &it->second;
		return nullptr;
	}
	inline const NodeType* find(const WorkNodeId id) const {
		if (auto it = nodes.find(id); it != nodes.end())
			return &it->second;
		return nullptr;
	}

	inline void operator()(WorkNodeId targetNode, CommandContext& context) {
		WorkResourceMap resources;

		std::unordered_set<WorkNodeId> done;
		std::stack<WorkNodeId> todo;
		todo.push(targetNode);
		while (!todo.empty()) {
			const WorkNodeId nodeId = todo.top();
			if (done.contains(nodeId))
				continue;

			NodeType* n = find(nodeId);
			if (!n) {
				std::cout << "Warning: No node \"" << nodeId << "\"";
				std::cout << std::endl;
				todo.pop();
				continue;
			}

			// check inputs. if an edge hasn't been executed yet, push it to the stack and wait
			std::visit([&](auto& v) {
				bool ready = true;

				for (const WorkNodeAttribute& attribute : GetAttributes(v)) {
					if ((attribute.flags & WorkAttributeFlagBits::eInput) == 0)
						continue;

					const WorkAttributePointer& dst{nodeId, attribute.name};
					if (auto dstIt = edges.find(dst); dstIt != edges.end()) {
						const WorkAttributePointer& src = dstIt->second;
						const auto& [srcNodeId, srcAttrib] = src;
						if (!nodes.contains(srcNodeId)) {
							std::cout << "Warning: Input node \"" << srcNodeId << "\" does not exist";
							std::cout << " (connected to " << attribute.name << " in node " << nodeId << ")";
							std::cout << std::endl;
							continue;
						}

						if (auto it = resources.find(src); it != resources.end()) {
							// edge exists and src attribute has been created
							// copy src attribute to dst attribute, so that the dst node can find it
							resources[dst] = it->second;
							continue;
						}

						// src attribute exists but has not yet been created
						// queue the src node
						todo.push(srcNodeId);
						ready = false;
					} else {
						if ((attribute.flags & WorkAttributeFlagBits::eOptional) == 0) {
							throw std::runtime_error("Error: Non-optional input attribute \"" + attribute.name + "\" in node " + nodeId + " is disconnected.");
						}
					}
				}

				if (ready) {
					v(context, resources);
					todo.pop();
					done.emplace(nodeId);
				}
			}, *n);
		}
	}
};

template<class T>
concept is_work_graph = requires { typename T::WorkResourceMap; };

template<typename T>
constexpr static const char* kSerializedTypeName = typeid(T).name();

template<is_work_graph WorkGraphType>
inline json& operator<<(json& data, WorkGraphType& graph) {
	json& serializedNodes = data["nodes"];
	json& serializedEdges = data["edges"];

	for (const auto&[id, node] : graph.nodes) {
		json& n = serializedNodes.emplace_back();
		std::visit(operator<<, node, n);
		n["id"] = id;
		n["type"] = std::visit(kSerializedTypeName, node);
	}

	for (const auto&[dst, src] : graph.edges) {
		json& edge = serializedEdges.emplace_back();
		edge["srcNode"]      = src.node;
		edge["srcAttribute"] = src.attribute;
		edge["dstNode"]      = dst.node;
		edge["dstAttribute"] = dst.attribute;
	}
	return data;
}

template<typename VariantType, int Index = 0>
inline void DeserializeWorkNode(const json& data, VariantType& node) {
	using T = std::variant_alternative_t<VariantType, Index>;
	if (data["type"] == kSerializedTypeName<T>) {
		T tmp;
		data >> tmp;
		node = tmp;
	} else {
		if constexpr (Index+1 < std::variant_size_v<VariantType>) {
			DeserializeWorkNode<VariantType, Index+1>(data, node);
		}
	}
}

template<is_work_graph WorkGraphType>
inline const json& operator>>(const json& data, WorkGraphType& graph) {
	for (const json& n : data["nodes"]) {
		const WorkNodeId id = n["id"].get<size_t>();
		auto& node = graph.nodes[id];
		DeserializeWorkNode(n, node);
	}

	for (const json& c : data["edges"]) {
		WorkAttributePointer dst;
		c["dstNode"]     .get_to(dst.node);
		c["dstAttribute"].get_to(dst.attribute);

		WorkAttributePointer& src = graph.edges[dst];
		c["srcNode"]     .get_to(src.node);
		c["srcAttribute"].get_to(src.attribute);
	}
	return data;
}

}