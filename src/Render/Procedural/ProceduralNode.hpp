#pragma once

#include <string>
#include <sstream>
#include <optional>
#include <typeindex>
#include <bit>
#include <concepts>
#include <Core/RoseEngine.hpp>
#include <Core/MathTypes.hpp>
#include <Core/Hash.hpp>
#include <json.hpp>

namespace RoseEngine {

// node output id -> compiled variable name
using NodeOutputMap = NameMap<std::string>;
using json = nlohmann::json;

class ProceduralNode;

struct ProceduralNodeCompiler {
	std::stringstream output = {};
	std::unordered_map<const ProceduralNode*, NodeOutputMap> nodeMap = {};
	std::string lineEnding = "\n";

	std::pair<NodeOutputMap*, bool> GetNodeOutputNames(const ProceduralNode* node);
};

struct NodeOutputConnection {
	inline static const char* gDefaultOutputName = "result";
	ref<ProceduralNode> node = {};
	std::string outputName = gDefaultOutputName;

	NodeOutputConnection() = default;
	NodeOutputConnection(const NodeOutputConnection&) = default;
	NodeOutputConnection(NodeOutputConnection&&) = default;
	NodeOutputConnection& operator=(const NodeOutputConnection&) = default;
	NodeOutputConnection& operator=(NodeOutputConnection&&) = default;

	inline NodeOutputConnection(nullptr_t node_, const std::string& outputName_ = gDefaultOutputName) : node(nullptr), outputName(outputName_) {}
	template<std::derived_from<ProceduralNode> NodeType>
	inline NodeOutputConnection(const ref<NodeType>& node_, const std::string& outputName_ = gDefaultOutputName) : node(node_), outputName(outputName_) {}
	template<std::derived_from<ProceduralNode> NodeType>
	inline NodeOutputConnection(ref<NodeType>&& node_, const std::string& outputName_ = gDefaultOutputName) : node(node_), outputName(outputName_) {}

	template<std::derived_from<ProceduralNode> NodeType>
	inline NodeOutputConnection& operator=(const ref<NodeType>& node_) {
		node = node_;
		outputName = gDefaultOutputName;
		return *this;
	}
	template<std::derived_from<ProceduralNode> NodeType>
	inline NodeOutputConnection& operator=(ref<NodeType>&& node_) {
		node = node_;
		outputName = gDefaultOutputName;
		return *this;
	}
};

class ProceduralNode : public std::enable_shared_from_this<ProceduralNode> {
protected:
	friend struct ProceduralNodeCompiler;
	NameMap<NodeOutputConnection> inputs = {};
	std::vector<std::string> outputs = { NodeOutputConnection::gDefaultOutputName };
	float2 pos = { 0, 0 };
	bool hasPos = false;

public:
	inline const auto& GetInputs() const { return inputs; }
	inline const auto& GetOutputNames() const { return outputs; }
	inline void SetInput(const std::string& name, const NodeOutputConnection& connection) {
		const auto&[node, outputName] = connection;
		if (node && std::ranges::find(node->outputs, outputName) == node->outputs.end()) {
			throw std::runtime_error("Node does not have output " + outputName);
		}
		inputs.at(name) = connection;
	}
	inline void SetInput(const std::string& name, const ref<ProceduralNode>& node, const std::string& outputName = "result") {
		if (node && std::ranges::find(node->outputs, outputName) == node->outputs.end()) {
			throw std::runtime_error("Node does not have output " + outputName);
		}
		inputs.at(name) = NodeOutputConnection(node, outputName);
	}

	inline void SetPosition(const float2 p) { pos = p; hasPos = true; }

	inline virtual size_t hash() const {
		size_t h = 0;
		h = HashArgs(h, HashRange(outputs));
		for (const auto&[name, c] : inputs) {
			const auto&[node, outputName] = c;
			h = HashArgs(h, name, outputName);
			if (node)
				h = HashArgs(h, node->hash());
		}
		return h;
	}

	// Gui function for the whole node. Calls Gui() and recurses on inputs
	virtual void NodeGui();
	virtual void Gui(float width = 0);

	virtual NodeOutputMap Compile(ProceduralNodeCompiler& compiler) const = 0;

	inline virtual const char* GetType() const { return "ProceduralNode"; }
	virtual json Serialize() const;
	static ref<ProceduralNode> DeserializeNode(const json& serialized);
};

inline std::pair<NodeOutputMap*, bool> ProceduralNodeCompiler::GetNodeOutputNames(const ProceduralNode* node) {
	if (auto it = nodeMap.find(node); it != nodeMap.end()) {
		return std::make_pair(&it->second, true);
	}

	std::string nodeId = "node_" + std::to_string(nodeMap.size());

	NodeOutputMap vars = {};
	if (node->outputs.size() == 1) {
		const auto& name = node->outputs[0];
		vars[name] = nodeId;
	} else
		for (const auto& name : node->outputs)
			vars[name] = nodeId + "_" + name;

	return std::make_pair(&nodeMap.emplace(node, vars).first->second, false);
}

int GetNodeId(const ProceduralNode* n);
int GetAttributeId(const ProceduralNode* node, const std::string& attrib, bool input);
ProceduralNode* GetNode(int id);
std::tuple<ProceduralNode*, std::string, bool> GetAttribute(int id);

}

namespace std {
template<std::derived_from<RoseEngine::ProceduralNode> NodeType>
struct hash<NodeType> {
inline size_t operator()(const NodeType& node) {
	return node.hash();
}
};
}