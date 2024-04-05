#pragma once

#include "ProceduralNode.hpp"

namespace RoseEngine {

class ProceduralFunction {
public:
	// Represents a single input variable, which is stored as the output of this node
	class InputVariable : public ProceduralNode {
	public:
		std::string variableType = {};

		InputVariable() = default;
		InputVariable(const InputVariable&) = default;
		InputVariable(InputVariable&&) = default;
		InputVariable& operator=(const InputVariable&) = default;
		InputVariable& operator=(InputVariable&&) = default;
		inline InputVariable(const std::string& varName, const std::string& varType) : variableType(varType) {
			outputs = { varName };
		}

		inline size_t hash() const override {
			return HashArgs(ProceduralNode::hash(), variableType);
		}

		inline NodeOutputMap Compile(ProceduralNodeCompiler& compiler) const override {
			const auto& variableName = outputs[0];
			return { { variableName, "inputs." + variableName } };
		}

		inline virtual const char* GetType() const { return "InputVariable"; }
		inline virtual json Serialize() const {
			json dst = ProceduralNode::Serialize();
			dst["variableType"] = variableType;
			return dst;
		}
		inline static ref<ProceduralNode> Deserialize(const json& serialized) {
			auto n = make_ref<InputVariable>();
			n->inputs.clear();
			n->outputs.clear();
			n->variableType = serialized["variableType"].get<std::string>();
			return n;
		}
	};

	// Represents all output variables, which are stored as the inputs to this node
	class OutputVariable : public ProceduralNode {
	public:
		NameMap<std::string> variableTypes;

		OutputVariable() = default;
		OutputVariable(const OutputVariable&) = default;
		OutputVariable(OutputVariable&&) = default;
		OutputVariable& operator=(const OutputVariable&) = default;
		OutputVariable& operator=(OutputVariable&&) = default;
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

		inline NodeOutputMap Compile(ProceduralNodeCompiler& compiler) const override {
			NameMap<std::string> compiledInputs = {};
			for (const auto&[name, inputPair] : inputs) {
				const auto&[node, outputName] = inputPair;
				if (node) {
					compiledInputs[name] = node->Compile(compiler).at(outputName);
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

		inline virtual const char* GetType() const { return "OutputVariable"; }
		inline virtual json Serialize() const {
			json dst = ProceduralNode::Serialize();
			dst["variableTypes"] = json::object();
			for (const auto&[var, varType] : variableTypes)
				dst["variableTypes"][var] = varType;
			return dst;
		}
		inline static ref<ProceduralNode> Deserialize(const json& serialized) {
			auto n = make_ref<OutputVariable>();
			n->inputs.clear();
			n->outputs.clear();
			for (const auto&[var, varType] : serialized["variableTypes"].items())
				n->variableTypes.emplace(var, varType.get<std::string>());
			return n;
		}
	};

	ProceduralFunction() = default;
	ProceduralFunction(const ProceduralFunction&) = default;
	ProceduralFunction(ProceduralFunction&&) = default;
	ProceduralFunction(const std::string& entryPoint, const NameMap<std::string>& outputs, const NameMap<NodeOutputConnection>& inputs = {});

	ProceduralFunction& operator=(const ProceduralFunction&) = default;
	ProceduralFunction& operator=(ProceduralFunction&&) = default;

	inline OutputVariable& Root() { return *mOutputNode; }

	void NodeGui();

	std::string Compile(const std::string& lineEnding = "\n");

	inline json Serialize() const {
		json dst = json::object();
		dst["name"] = mEntryPoint;
		dst["node"] = mOutputNode->Serialize();
		return dst;
	}
	inline static ProceduralFunction Deserialize(const json& serialized) {
		ProceduralFunction f = {};
		f.mEntryPoint = serialized["name"].get<std::string>();
		f.mOutputNode = std::dynamic_pointer_cast<OutputVariable>( ProceduralNode::DeserializeNode(serialized["node"]) );
		return f;
	}

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

	std::string mEntryPoint = "ProceduralFunction";
	ref<OutputVariable> mOutputNode = {};
};

};