#include "ProceduralNode.hpp"
#include "MathNode.hpp"
#include "ProceduralFunction.hpp"
#include <imnodes.h>

namespace RoseEngine {

std::unordered_map<int, ProceduralNode*> ProceduralNode::gNodeIdMap = {};
std::unordered_map<int, std::tuple<ProceduralNode*, std::string, bool>> ProceduralNode::gAttributeIdMap = {};
std::unordered_map<int, std::pair<int, int>> ProceduralNode::gLinkIdMap = {};

int ProceduralNode::GetNodeId(const ProceduralNode* node) {
	int id = (int)HashArgs(node);
	gNodeIdMap[id] = const_cast<ProceduralNode*>(node);
	return id;
}
int ProceduralNode::GetAttributeId(const ProceduralNode* node, const std::string& attrib, bool input) {
	int id = (int)HashArgs(node, attrib, input);
	gAttributeIdMap[id] = { const_cast<ProceduralNode*>(node), attrib, input };
	return id;
}
int ProceduralNode::GetLinkId(const int start, const int end) {
	int id = (int)HashArgs(start, end);
	gLinkIdMap[id] = {start, end};
	return id;
}

void ProceduralNode::Gui(float w) {
	auto it = inputs.begin();
	// draw inputs and outputs on the same line
	for (uint32_t i = 0; i < std::max(outputs.size(), inputs.size()); i++) {
		// draw input
		float offset = w;
		bool inputRow = it != inputs.end();
		if (inputRow) {
			auto&[name, c] = *it;
			ImNodes::BeginInputAttribute(GetAttributeId(this, name, true));
			ImGui::TextUnformatted(name.c_str());
			offset -= ImGui::GetItemRectSize().x;
			ImNodes::EndInputAttribute();
			it++;
		}

		// draw output
		if (i < outputs.size()) {
			auto& name = outputs[i];
			if (inputRow)
				ImGui::SameLine();
			// align to right side of the node
			if (offset > 0) {
				ImVec2 s = ImGui::CalcTextSize(name.c_str());
				offset -= s.x;
				ImGui::Dummy(ImVec2(offset, s.y));
				ImGui::SameLine();
			}
			ImNodes::BeginOutputAttribute(GetAttributeId(this, name, false));
			ImGui::TextUnformatted(name.c_str());
			ImNodes::EndOutputAttribute();
		}
	}
}

void ProceduralNode::NodeGui(std::unordered_set<int>& drawn) {
	drawn.emplace(GetNodeId(this));
	ImNodes::BeginNode(GetNodeId(this));
	Gui();
	ImNodes::EndNode();

	if (hasPos) {
		ImNodes::SetNodeGridSpacePos(GetNodeId(this), std::bit_cast<ImVec2>(pos));
		hasPos = false;
	}

	for (auto&[inputName, c] : inputs) {
		auto&[n, outputName] = c;
		if (n) {
			if (!drawn.contains(GetNodeId(n.get()))) {
				n->NodeGui(drawn);
			}

			int start = GetAttributeId(n.get(), outputName, false);
			int end   = GetAttributeId(this, inputName, true);
			ImNodes::Link(GetLinkId(start, end), start, end);
		}
	}
}

json ProceduralNode::Serialize() const {
	json dst = json::object();
	dst["type"] = std::string(GetType());
	dst["inputs"] = json::object();
	for (const auto&[name, c] : inputs) {
		const auto&[node, outputName] = c;
		dst["inputs"][name] = json::object({
			{"outputName", outputName },
			{"node", node ? node->Serialize() : json::object() },
		});
	}
	dst["outputs"] = json::array();
	for (const auto& name : outputs)
		dst["outputs"].emplace_back(name);
	ImVec2 p = ImNodes::GetNodeGridSpacePos(GetNodeId(this));
	dst["pos"] = { p.x, p.y};
	return dst;
}

ref<ProceduralNode> ProceduralNode::DeserializeNode(const json& serialized) {
	ref<ProceduralNode> node;
	NodeType type = gNodeTypeMap.at(serialized["type"].get<std::string>());
	switch (type) {
		case NodeType::eMathNode:
			node = MathNode::Deserialize(serialized);
			break;
		case NodeType::eExpressionNode:
			node = ExpressionNode::Deserialize(serialized);
			break;
		case NodeType::eInputVariable:
			node = ProceduralFunction::InputVariable::Deserialize(serialized);
			break;
		case NodeType::eOutputVariable:
			node = ProceduralFunction::OutputVariable::Deserialize(serialized);
			break;
	}

	for (const auto&[name,i] : serialized["inputs"].items()) {
		node->inputs.emplace(name, NodeOutputConnection(
			i["node"].contains("type") ? ProceduralNode::DeserializeNode(i["node"]) : ref<ProceduralNode>{},
			i["outputName"].get<std::string>()));
	}

	for (const auto& i : serialized["outputs"]) {
		node->outputs.emplace_back(i.get<std::string>());
	}

	node->pos = float2(serialized["pos"][0].get<float>(), serialized["pos"][1].get<float>());
	node->hasPos = true;

	return node;
}

}