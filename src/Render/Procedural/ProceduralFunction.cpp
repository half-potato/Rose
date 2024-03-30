#include "ProceduralFunction.hpp"
#include <imnodes.h>

namespace RoseEngine {

ProceduralFunction::ProceduralFunction(const std::string& entryPoint, const NameMap<std::string>& outputTypes, const NameMap<NodeOutputConnection>& inputs) {
	mEntryPoint = entryPoint,
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


std::string ProceduralNode::Serialize() const {
	return "";
}
void ProceduralNode::Deserialize(const std::string& serialized) {

}

std::string ProceduralFunction::Serialize() const {
	return "";
}
void ProceduralFunction::Deserialize(const std::string& serialized) {

}


}