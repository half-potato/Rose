#include "MathNode.hpp"
#include <Core/Gui.hpp>
#include <imnodes.h>
#include <imgui/imgui_stdlib.h>

namespace RoseEngine {

void ExpressionNode::Gui(float width) {
	ImNodes::BeginNodeTitleBar();
	ImGui::TextUnformatted("Expression");
	ImNodes::EndNodeTitleBar();
	ImGui::SetNextItemWidth(150);

	{
		auto& name = outputs[0];
		ImNodes::BeginOutputAttribute((int)HashArgs(this, name, 1));

		ImGui::InputText("Value", &value);

		ImNodes::EndOutputAttribute();
	}
}

NodeOutputMap MathNode::Compile(ProceduralNodeCompiler& compiler) const {
	auto[vars, cached] = compiler.GetNodeOutputNames(this);
	if (cached) return *vars;

	uint32_t numArgs = GetArgCount(op);

	std::string a, b, c;
	if (numArgs > 0) {
		const auto& [node, output] = inputs.at("a");
		if (node)
			a = node->Compile(compiler).at(output);
		else
			a = "0";
	}
	if (numArgs > 1) {
		const auto& [node, output] = inputs.at("b");
		if (node)
			b = node->Compile(compiler).at(output);
		else
			b = "0";
	}
	if (numArgs > 2) {
		const auto& [node, output] = inputs.at("c");
		if (node)
			c = node->Compile(compiler).at(output);
		else
			c = "0";
	}

	compiler.output << "let " << vars->begin()->second << " = " << GetOpName(op) << "(";
	if (numArgs > 0) compiler.output << a;
	if (numArgs > 1) compiler.output << ", " << b;
	if (numArgs > 2) compiler.output << ", " << c;
	compiler.output << ");" << compiler.lineEnding;

	return *vars;
}

void MathNode::Gui(float width) {
	ImNodes::BeginNodeTitleBar();
	ImGui::TextUnformatted("Math Op");
	ImNodes::EndNodeTitleBar();
	ImGui::SetNextItemWidth(150);
	float w;
	if (ImGui::BeginCombo("Op", GetOpName(op))) {
		w = ImGui::GetItemRectSize().x;
		for (uint32_t i = 0; i < MathOp::eOpCount; i++) {
			if (ImGui::Selectable(GetOpName((MathOp)i), op == (MathOp)i)) {
				op = (MathOp)i;
			}
		}
		ImGui::EndCombo();
	} else
		w = ImGui::GetItemRectSize().x;

	auto it = inputs.begin();
	uint32_t argCount = GetArgCount(op);
	for (uint32_t i = 0; i < std::max(outputs.size(), inputs.size()); i++) {
		float offset = w;
		bool inputRow = it != inputs.end() && i < argCount;
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
			ImVec2 s = ImGui::CalcTextSize(name.c_str());
			offset -= s.x;
			ImGui::Dummy(ImVec2(offset, s.y));
			ImNodes::BeginOutputAttribute((int)HashArgs(this, name, 1));
			ImGui::SameLine();
			ImGui::TextUnformatted(name.c_str());
			ImNodes::EndOutputAttribute();
		}
	}
}


std::string ExpressionNode::Serialize() const {
	return "";
}
void ExpressionNode::Deserialize(const std::string& serialized) {

}

std::string MathNode::Serialize() const {
	return "";
}
void MathNode::Deserialize(const std::string& serialized) {

}


}