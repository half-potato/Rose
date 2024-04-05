#include "ProceduralFunction.hpp"
#include <imnodes.h>
#include "MathNode.hpp"

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

void ProceduralFunction::NodeGui() {
	ImNodes::BeginNodeEditor();
	mOutputNode->NodeGui();
	ImNodes::EndNodeEditor();
}

void ProceduralNode::Gui(float w) {
	auto it = inputs.begin();
	for (uint32_t i = 0; i < std::max(outputs.size(), inputs.size()); i++) {
		float offset = w;
		bool inputRow = it != inputs.end();
		if (inputRow) {
			auto&[name, c] = *it;
			ImNodes::BeginInputAttribute((int)HashArgs(this, name));
			ImGui::TextUnformatted(name.c_str());
			offset -= ImGui::GetItemRectSize().x;
			ImNodes::EndInputAttribute();
			it++;
		}

		if (i < outputs.size()) {
			auto& name = outputs[i];
			if (inputRow)
				ImGui::SameLine();
			if (offset > 0) {
				ImVec2 s = ImGui::CalcTextSize(name.c_str());
				offset -= s.x;
				ImGui::Dummy(ImVec2(offset, s.y));
				ImGui::SameLine();
			}
			ImNodes::BeginOutputAttribute((int)HashArgs(this, name, 1));
			ImGui::TextUnformatted(name.c_str());
			ImNodes::EndOutputAttribute();
		}
	}
}

void ProceduralNode::NodeGui() {
	ImNodes::BeginNode((int)HashArgs(this));
	Gui();
	ImNodes::EndNode();

	if (hasPos) {
		ImNodes::SetNodeGridSpacePos((int)HashArgs(this), std::bit_cast<ImVec2>(pos));
		hasPos = false;
	}

	for (auto&[inputName, c] : inputs) {
		auto&[n, outputName] = c;
		if (n) {
			n->NodeGui();
			ImNodes::Link((int)HashArgs(n.get()), (int)HashArgs(n.get(), outputName, 1), (int)HashArgs(this, inputName));
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
	ImVec2 p = ImNodes::GetNodeGridSpacePos((int)HashArgs(this));
	dst["pos"] = { p.x, p.y};
	return dst;
}

ref<ProceduralNode> ProceduralNode::DeserializeNode(const json& serialized) {
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