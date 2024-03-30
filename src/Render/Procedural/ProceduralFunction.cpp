#include "ProceduralFunction.hpp"
#include <imnodes.h>

namespace RoseEngine {

ProceduralFunction::ProceduralFunction(const NameMap<std::string>& outputTypes, const NameMap<NodeOutputConnection>& inputs) {
	mOutputNode = make_ref<OutputVariable>(outputTypes);
	for (const auto& [outputName, input] : inputs)
		mOutputNode->SetInput(outputName, input);
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

	for (auto&[inputName, c] : inputs) {
		auto&[n, outputName] = c;
		if (n) {
			n->NodeGui();
			ImNodes::Link((int)HashArgs(n.get()), (int)HashArgs(n.get(), outputName, 1), (int)HashArgs(this, inputName));
		}
	}
}

std::string ProceduralFunction::compile(const std::string& lineEnding) {
	ProceduralNodeCompiler compiler = {};
	compiler.lineEnding = lineEnding;

	VariableMap inputVars = {};
	FindInputs(*mOutputNode, inputVars);
	compiler.output << "struct ProceduralNodeArgs {" << lineEnding;
	for (const auto&[name, type] : inputVars) {
		compiler.output << ' ' << type << ' ' << name << ';' << lineEnding;
	}
	compiler.output << "};" << lineEnding << lineEnding;

	compiler.output << "struct ProceduralEvalResult {" << lineEnding;
	for (const auto&[name, type] : mOutputNode->variableTypes) {
		compiler.output << ' ' << type << ' ' << name << ';' << lineEnding;
	}
	compiler.output << "};" << lineEnding << lineEnding;

	compiler.output << "ProceduralEvalResult eval_node(ProceduralNodeArgs inputs) {" << lineEnding;
	compiler.output << " ProceduralEvalResult outputs = {};" << lineEnding << lineEnding;
	mOutputNode->compile(compiler);
	compiler.output << lineEnding;
	compiler.output << " return outputs;" << lineEnding;
	compiler.output << '}' << lineEnding;

	return compiler.output.str();
}

}