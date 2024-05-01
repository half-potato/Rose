#include "ProceduralFunction.hpp"
#include "MathNode.hpp"
#include <imnodes.h>
#include <iostream>

namespace RoseEngine {

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
	compiler.output << "struct " << inputStruct << " : IDifferentiable {" << lineEnding;
	for (const auto&[name, type] : inputVars) {
		compiler.output << ' ' << type << ' ' << name << ';' << lineEnding;
	}
	compiler.output << "};" << lineEnding << lineEnding;

	compiler.output << "struct " << outputStruct << " : IDifferentiable {" << lineEnding;
	for (const auto&[name, type] : mOutputNode->variableTypes) {
		compiler.output << ' ' << type << ' ' << name << ';' << lineEnding;
	}
	compiler.output << "};" << lineEnding << lineEnding;

	compiler.output << "[Differentiable]" << lineEnding;
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

	std::unordered_set<int> drawn;

	// draw node tree
	mOutputNode->NodeGui(drawn);

	// draw disconnected nodes
	for (auto it = mDisconnectedNodes.begin(); it != mDisconnectedNodes.end();) {
		if (it->use_count() > 1)
			it = mDisconnectedNodes.erase(it);
		else {
			(*it)->NodeGui(drawn);
			it++;
		}
	}
	ImNodes::EndNodeEditor();

	// handle new links
	int start,end;
	if (ImNodes::IsLinkCreated(&start, &end)) {
		auto[startNode, startAttribName, startIsInput] = ProceduralNode::GetAttribute(start);
		auto[endNode  , endAttribName, endIsInput]     = ProceduralNode::GetAttribute(end);
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
			}
		}
	}

	// handle destroyed links
	int link;
	if (ImNodes::IsLinkDestroyed(&link)) {
		std::tie(start, end) = ProceduralNode::GetLink(link);
		const auto&[startNode, startAttribName, startIsInput] = ProceduralNode::GetAttribute(start);
		const auto&[endNode  , endAttribName, endIsInput]     = ProceduralNode::GetAttribute(end);
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
		std::optional<ProceduralNode::NodeType> selected = std::nullopt;
		for (const auto& [name, type] : ProceduralNode::gNodeTypeMap) {
			if (ImGui::MenuItem(name.c_str()))
				selected = type;
		}
		if (selected.has_value()) {
			ref<ProceduralNode> node;
			switch (*selected) {
				case ProceduralNode::NodeType::eMathNode:
					node = make_ref<MathNode>();
					break;
				case ProceduralNode::NodeType::eExpressionNode:
					node = make_ref<ExpressionNode>();
					break;
				case ProceduralNode::NodeType::eInputVariable:
					node = make_ref<ProceduralFunction::InputVariable>();
					break;
				case ProceduralNode::NodeType::eOutputVariable:
					node = make_ref<ProceduralFunction::OutputVariable>();
					break;
			}
			mDisconnectedNodes.emplace_back(node);
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

}