#pragma once

#include "ProceduralNode.hpp"

namespace RoseEngine {

// Represents an input to the procedural function
class ProceduralInputNode : public ProceduralNode {
public:
	std::string variableType;

	inline ProceduralInputNode(const std::string& varName, const std::string& varType) : variableType(varType) {
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

// Represents a return value from the procedural function
class ProceduralOutputNode : public ProceduralNode {
public:
	std::unordered_map<std::string, std::string> variableTypes;

	inline ProceduralOutputNode(const std::unordered_map<std::string, std::string>& varTypes) : variableTypes(varTypes) {
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
		std::unordered_map<std::string, std::string> compiledInputs = {};
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

class ProceduralNodeTree {
private:
	using VariableMap = std::unordered_map<std::string/*variable*/, std::string/*typename*/>;

	inline void FindInputs(const ProceduralNode& node, VariableMap& dst) {
		if (const ProceduralInputNode* inputNode = dynamic_cast<const ProceduralInputNode*>(&node)) {
			dst[node.GetOutputNames()[0]] = inputNode->variableType;;
		}
		for (const auto&[name, inputPair] : node.GetInputs()) {
			const auto& [n, _] = inputPair;
			if (n) FindInputs(*n, dst);
		}
	}

	ref<ProceduralOutputNode> mOutputNode = {};

public:
	ProceduralNodeTree(const std::string& outputName, const std::string& outputType, const NodeOutputConnection& input = {});

	inline ProceduralOutputNode& Root() { return *mOutputNode; }

	std::string compile(const std::string& lineEnding = "\n");

	void NodeGui();
};

};