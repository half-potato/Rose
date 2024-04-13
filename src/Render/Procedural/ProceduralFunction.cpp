#include "ProceduralFunction.hpp"
#include "MathNode.hpp"
#include <imnodes.h>
#include <iostream>

namespace RoseEngine {

enum NodeType {
	eMathNode,
	eExpressionNode,
	eInputVariable,
	eOutputVariable
};
static const NameMap<NodeType> nodeTypes = {
	{ "MathNode",       NodeType::eMathNode },
	{ "ExpressionNode", NodeType::eExpressionNode },
	{ "InputVariable",  NodeType::eInputVariable },
	{ "OutputVariable", NodeType::eOutputVariable },
};

static std::unordered_map<int, ProceduralNode*> gNodeIdMap;
static std::unordered_map<int, std::tuple<ProceduralNode*, std::string, bool>> gAttributeIdMap;
static std::unordered_map<int, std::pair<int, int>> gLinkIdMap;

int GetNodeId(const ProceduralNode* node) {
	int id = (int)HashArgs(node);
	gNodeIdMap[id] = const_cast<ProceduralNode*>(node);
	return id;
}
int GetAttributeId(const ProceduralNode* node, const std::string& attrib, bool input) {
	int id = (int)HashArgs(node, attrib, input);
	gAttributeIdMap[id] = { const_cast<ProceduralNode*>(node), attrib, input };
	return id;
}
int GetLinkId(const int start, const int end) {
	int id = (int)HashArgs(start, end);
	gLinkIdMap[id] = {start, end};
	return id;
}
ProceduralNode* GetNode(int id) { return gNodeIdMap.at(id); }
std::tuple<ProceduralNode*, std::string, bool> GetAttribute(int id) { return gAttributeIdMap.at(id); }
std::pair<int, int> GetLink(const int id) { return gLinkIdMap.at(id); }

ProceduralFunction::ProceduralFunction(const std::string& entryPoint, const NameMap<std::string>& outputTypes, const NameMap<NodeOutputConnection>& inputs) {
	mEntryPoint = entryPoint,
	mOutputNode = make_ref<OutputVariable>(outputTypes);
	for (const auto& [outputName, input] : inputs)
		mOutputNode->SetInput(outputName, input);
}

std::string ProceduralFunction::Compile(const std::string& lineEnding) {
	ProceduralNodeCompiler compiler = {};
	compiler.lineEnding = lineEnding;

	std::string inputStruct = "ProceduralNodeArgs";
	std::string outputStruct = "ProceduralEvalResult";

	VariableMap inputVars = {};
	FindInputs(*mOutputNode, inputVars);
	compiler.output << "struct " << inputStruct << " {" << lineEnding;
	for (const auto&[name, type] : inputVars) {
		compiler.output << ' ' << type << ' ' << name << ';' << lineEnding;
	}
	compiler.output << "};" << lineEnding << lineEnding;

	compiler.output << "struct " << outputStruct << " {" << lineEnding;
	for (const auto&[name, type] : mOutputNode->variableTypes) {
		compiler.output << ' ' << type << ' ' << name << ';' << lineEnding;
	}
	compiler.output << "};" << lineEnding << lineEnding;

	compiler.output << outputStruct << " " << mEntryPoint << "(" << inputStruct << " inputs) {" << lineEnding;
	compiler.output << outputStruct << " outputs = {};" << lineEnding << lineEnding;
	mOutputNode->Compile(compiler);
	compiler.output << lineEnding;
	compiler.output << "return outputs;" << lineEnding;
	compiler.output << '}' << lineEnding;

	return compiler.output.str();
}

bool ScanInputs(const ProceduralNode* n, const ProceduralNode* b) {
	if (n == b)
		return true;
	for (auto[name,c] : n->GetInputs()) {
		if (!c.node) continue;
		if (ScanInputs(c.node.get(), b))
			return true;
	}
	return false;
}

void ProceduralFunction::NodeEditorGui() {
	ImNodes::BeginNodeEditor();

	// draw node tree
	mOutputNode->NodeGui();

	// draw disconnected nodes
	for (auto it = mDisconnectedNodes.begin(); it != mDisconnectedNodes.end();) {
		if (it->use_count() > 1)
			it = mDisconnectedNodes.erase(it);
		else {
			(*it)->NodeGui();
			it++;
		}
	}
	ImNodes::EndNodeEditor();

	// handle new links
	int start,end;
	if (ImNodes::IsLinkCreated(&start, &end)) {
		auto[startNode, startAttribName, startIsInput] = GetAttribute(start);
		auto[endNode  , endAttribName, endIsInput]     = GetAttribute(end);
		if (startIsInput != endIsInput) {
			if (!startIsInput) {
				std::swap(startNode, endNode);
				std::swap(startAttribName, endAttribName);
			}

			if (!ScanInputs(endNode, startNode)) {
				if (auto it = startNode->GetInputs().find(startAttribName); it != startNode->GetInputs().end()) {
					if (it->second.node.use_count() == 1)
						mDisconnectedNodes.emplace_back(it->second.node);
				}
				startNode->SetInput(startAttribName, endNode->shared_from_this(), endAttribName);
				ImNodes::Link(GetLinkId(end, start), end, start);
			}
		}
	}

	// handle destroyed links
	int link;
	if (ImNodes::IsLinkDestroyed(&link)) {
		std::tie(start, end) = GetLink(link);
		const auto&[startNode, startAttribName, startIsInput] = GetAttribute(start);
		const auto&[endNode  , endAttribName, endIsInput]     = GetAttribute(end);
		if (startIsInput) {
			startNode->SetInput(startAttribName, NodeOutputConnection{});
		} else if (endIsInput) {
			endNode->SetInput(endAttribName, NodeOutputConnection{});
		}
	}

	// context menu
	const float2 m    = std::bit_cast<float2>(ImGui::GetIO().MousePos);
	const float2 wmin = std::bit_cast<float2>(ImGui::GetWindowPos());
	const float2 wmax = wmin + std::bit_cast<float2>(ImGui::GetWindowSize());
	if (m.x >= wmin.x && m.y >= wmin.y && m.x < wmax.x && m.y < wmax.y) {
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
			ImGui::OpenPopup("Node context menu");
		}
	}

	bool addNode = false;
	if (ImGui::BeginPopupContextWindow("Node context menu")) {
		if (ImGui::MenuItem("Add node")) {
			addNode = true;
		}
		ImGui::EndPopup();
	}
	if (addNode)
		ImGui::OpenPopup("Add node menu");

	if (ImGui::BeginPopupContextWindow("Add node menu")) {
		std::optional<NodeType> selected = std::nullopt;
		for (const auto& [name, type] : nodeTypes) {
			if (ImGui::MenuItem(name.c_str()))
				selected = type;
		}
		if (selected.has_value()) {
			ref<ProceduralNode> node;
			switch (*selected) {
				case NodeType::eMathNode:
					node = make_ref<MathNode>();
					break;
				case NodeType::eExpressionNode:
					node = make_ref<ExpressionNode>();
					break;
				case NodeType::eInputVariable:
					node = make_ref<ProceduralFunction::InputVariable>();
					break;
				case NodeType::eOutputVariable:
					node = make_ref<ProceduralFunction::OutputVariable>();
					break;
			}
			mDisconnectedNodes.emplace_back(node);
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
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

void ProceduralNode::NodeGui() {
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
			n->NodeGui();

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
	NodeType type = nodeTypes.at(serialized["type"].get<std::string>());
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