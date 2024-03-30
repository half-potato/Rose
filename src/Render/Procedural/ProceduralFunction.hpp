#pragma once

#include "ProceduralNode.hpp"

namespace RoseEngine {

class ProceduralFunction {
public:
	// Represents a single input variable, which is stored as the output of this node
	class InputVariable : public ProceduralNode {
	public:
		std::string variableType;

		inline InputVariable(const std::string& varName, const std::string& varType) : variableType(varType) {
			outputs = { varName };
		}

		inline size_t hash() const override {
			return HashArgs(ProceduralNode::hash(), variableType);
		}

		inline NodeOutputMap compile(ProceduralNodeCompiler& compiler) const override {
			const auto& variableName = outputs[0];
			return { { variableName, "inputs." + variableName } };
		}
	};

	// Represents all output variables, which are stored as the inputs to this node
	class OutputVariable : public ProceduralNode {
	public:
		NameMap<std::string> variableTypes;

		inline OutputVariable(const NameMap<std::string>& varTypes) : variableTypes(varTypes) {
			for (const auto&[name, type] : variableTypes)
				inputs[name] = {};
			outputs = {};
		}

		inline size_t hash() const override {
			size_t h = ProceduralNode::hash();
			for (const auto&[name, type] : variableTypes)
				h = HashArgs(h, name, type);
			return h;
		}

		inline NodeOutputMap compile(ProceduralNodeCompiler& compiler) const override {
			NameMap<std::string> compiledInputs = {};
			for (const auto&[name, inputPair] : inputs) {
				const auto&[node, outputName] = inputPair;
				if (node) {
					compiledInputs[name] = node->compile(compiler).at(outputName);
				}
			}

			for (const auto&[name, inputPair] : inputs) {
				const auto&[node, outputName] = inputPair;
				if (node) {
					compiler.output << "outputs." << name << " = " << compiledInputs[name] << ";" << compiler.lineEnding;
				}
			}
			return {};
		}
	};

	ProceduralFunction(const NameMap<std::string>& outputs, const NameMap<NodeOutputConnection>& inputs = {});

	inline OutputVariable& Root() { return *mOutputNode; }

	void NodeGui();

	std::string compile(const std::string& lineEnding = "\n");

private:
	// variable name -> type
	using VariableMap = NameMap<std::string>;

	inline void FindInputs(const ProceduralNode& node, VariableMap& dst) {
		if (const InputVariable* inputNode = dynamic_cast<const InputVariable*>(&node)) {
			dst[node.GetOutputNames()[0]] = inputNode->variableType;;
		}
		for (const auto&[name, inputPair] : node.GetInputs()) {
			const auto& [n, _] = inputPair;
			if (n) FindInputs(*n, dst);
		}
	}

	ref<OutputVariable> mOutputNode = {};
};

};