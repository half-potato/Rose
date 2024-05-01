#include "MathNode.hpp"
#include <Core/Gui.hpp>
#include <imnodes.h>
#include <imgui/imgui_stdlib.h>

namespace RoseEngine {

void ExpressionNode::Gui(float width) {
	ImNodes::BeginNodeTitleBar();
	ImGui::TextUnformatted("Expression");
	ImNodes::EndNodeTitleBar();

	{
		auto& name = outputs[0];
		ImNodes::BeginOutputAttribute(GetAttributeId(this, name, false));
		ImGui::SetNextItemWidth(150);
		ImGui::InputText("##", &expression);
		ImNodes::EndOutputAttribute();
	}

	ImGui::SetNextItemWidth(150);
	ImGui::TextUnformatted("Inputs");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(100);
	if (ImGui::Button("+")) {
		inputMapping.emplace_back("arg" + std::to_string(inputs.size()));
		inputs.emplace(std::to_string(inputs.size()), NodeOutputConnection{});
	}

	{
		auto it = inputs.begin();
		for (size_t i = 0; i < inputs.size();) {
			ImGui::PushID(HashArgs(this, i));

			const std::string name = std::to_string(i);
			ImNodes::BeginInputAttribute(GetAttributeId(this, name, true));
			ImGui::SetNextItemWidth(150);
			ImGui::InputText("##", &inputMapping[i]);
			ImNodes::EndInputAttribute();

			ImGui::SameLine();
			ImGui::SetNextItemWidth(100);
			if (ImGui::Button("-")) {
				it = inputs.erase(it);
				inputMapping.erase(inputMapping.begin() + i);
				continue;
			}

			it++;
			i++;

			ImGui::PopID();
		}

	}
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
			ImNodes::BeginInputAttribute(GetAttributeId(this, name, true));
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
			ImNodes::BeginOutputAttribute(GetAttributeId(this, name, false));
			ImGui::SameLine();
			ImGui::TextUnformatted(name.c_str());
			ImNodes::EndOutputAttribute();
		}
	}
}

NodeOutputMap ExpressionNode::Compile(ProceduralNodeCompiler& compiler) const {
	auto[vars, cached] = compiler.GetNodeOutputNames(this);
	if (cached) return *vars;

	std::string value = expression;

	auto it = inputs.begin();
	for (size_t i = 0; i < inputs.size(); i++, it++) {
		const auto& [node, output] = it->second;
		if (!node) continue;

		const std::string arg = node->Compile(compiler).at(output);
		for (size_t p = 0; p < value.length(); p += arg.length()) {
			p = value.find(inputMapping[i], p);
			if (p == std::string::npos) break;
			value.replace(p, inputMapping[i].length(), arg);
		}
	}

	return { {  NodeOutputConnection::gDefaultOutputName, value } };
}

NodeOutputMap MathNode::Compile(ProceduralNodeCompiler& compiler) const {
	auto[vars, cached] = compiler.GetNodeOutputNames(this);
	if (cached) return *vars;

	uint32_t numArgs = GetArgCount(op);

	std::string args[3];
	uint32_t i = 0;
	for (auto arg : { "x", "y", "z" }) {
		if (i >= numArgs) break;
		const auto& [node, output] = inputs.at(arg);
		if (node)
			args[i] = node->Compile(compiler).at(output);
		else
			args[i] = "0";
		i++;
	}

	compiler.output << "let " << vars->begin()->second << " = " << GetOpName(op) << "(";
	if (numArgs > 0) compiler.output << args[0];
	if (numArgs > 1) compiler.output << ", " << args[1];
	if (numArgs > 2) compiler.output << ", " << args[2];
	compiler.output << ");" << compiler.lineEnding;

	return *vars;
}

}