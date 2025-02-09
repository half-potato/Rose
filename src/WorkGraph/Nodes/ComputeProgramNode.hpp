#pragma once

#include <WorkGraph/WorkNode.hpp>
#include <Core/TransientResourceCache.hpp>

namespace RoseEngine {

struct ComputeProgramNode {
	inline static const std::array<WorkNodeAttribute, 4> kInputAttributes = {
		WorkNodeAttribute{ "count",       WorkAttributeFlagBits::eInput },
		WorkNodeAttribute{ "bufferSize",  WorkAttributeFlagBits::eInput },
		WorkNodeAttribute{ "bufferUsage", WorkAttributeFlagBits::eInput },
		WorkNodeAttribute{ "memoryFlags", WorkAttributeFlagBits::eInput },
	};

	std::vector<WorkNodeAttribute> attributes;

	std::string   shaderPath; // can be relative to src/
	std::string   entryPoint;
	std::string   shaderProfile;
	ShaderDefines defines;
	std::vector<std::string> compileArgs;

	ComputePipelineInfo   computePipelineInfo;
	PipelineLayoutInfo    pipelineLayoutInfo;
	DescriptorSetLayouts  descriptorSetLayouts;

	uint3 threadCount;
	ShaderParameter rootParameter;

	// Runtime variables

	WorkNodeId    nodeId;
	ref<Pipeline> pipeline;
	std::string   statusText;

	inline static std::filesystem::path GetAbsolutePath(const std::filesystem::path& p) {
		if (p.is_relative()) // path is relative to src/
			return std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path().parent_path();
		else
			return p;
	}

	inline const ShaderModule* GetShader() {
		if (pipeline)
			return pipeline->GetShader().get();
		return nullptr;
	}

	void CreatePipeline(const Device& device) {
		statusText.clear();
		auto p = GetAbsolutePath(shaderPath);
		if (!std::filesystem::exists(p)) {
			statusText = "Could not find file: " + p.string();
			return;
		}

		ref<ShaderModule> shader;
		try {
			shader = ShaderModule::Create(device, p, entryPoint, shaderProfile, defines, compileArgs, false);
		} catch (std::runtime_error e) {
			statusText = e.what();
			return;
		}

		pipeline = Pipeline::CreateCompute(device, shader, computePipelineInfo, pipelineLayoutInfo, descriptorSetLayouts);
	}

	inline void operator()(CommandContext& context, WorkResourceMap& resources) {
		if (auto s = GetShader(); s == nullptr || s->IsStale()) {
			CreatePipeline(context.GetDevice());
		}

		context.Dispatch(*pipeline, threadCount, rootParameter);
	}
};

template<> constexpr static const char* kSerializedTypeName<ComputeProgramNode> = "ComputeProgramNode";

template<>
inline void Serialize(json& data, const ComputeProgramNode& node) {
	data["shaderPath"] = node.shaderPath;
}
template<>
inline void Deserialize(const json& data, ComputeProgramNode& node) {
	data["shaderPath"].get_to(node.shaderPath);
}

inline const auto& GetAttributes(const ComputeProgramNode& node) {
	return node.attributes;
}

inline void DrawNode(ComputeProgramNode& node) {
	DrawNodeTitle("Compute Pipeline");
	ImGui::SetNextItemWidth(200);
	if (ImGui::InputText("Shader", &node.shaderPath)) {

	}
	for (const auto& a : node.attributes)
		DrawNodeAttribute(node.nodeId, a);

	if (!node.statusText.empty()) {
		ImGui::SetNextItemWidth(200);
		ImGui::TextUnformatted(node.statusText.c_str());
	}
}

}