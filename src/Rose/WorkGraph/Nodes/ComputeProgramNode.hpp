#pragma once

#include <Rose/WorkGraph/WorkNode.hpp>
#include <Rose/Core/TransientResourceCache.hpp>
#include <portable-file-dialogs.h>

namespace
{
	inline static const std::filesystem::path kSrcFolder = std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path().parent_path();
}

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
	std::string   entryPoint = "main";
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

	inline std::filesystem::path GetAbsolutePath() {
		std::filesystem::path p = shaderPath;
		if (p.is_relative()) {
			// p is relative to src/
			p = kSrcFolder / p;
		}
		return p;
	}

	inline const ShaderModule* GetShader() {
		if (pipeline)
			return pipeline->GetShader().get();
		return nullptr;
	}

	void CreatePipeline(const Device& device) {
		statusText.clear();
		auto p = GetAbsolutePath();
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

inline json& operator<<(json& data, const ComputeProgramNode& node) {
	data["shaderPath"]    = node.shaderPath;
	data["entryPoint"]    = node.entryPoint;
	data["shaderProfile"] = node.shaderProfile;
	data["defines"]       = node.defines;
	data["compileArgs"]   = node.compileArgs;
	return data;
}
inline const json& operator>>(const json& data, ComputeProgramNode& node) {
	data["shaderPath"]   .get_to(node.shaderPath);
	data["entryPoint"]   .get_to(node.entryPoint);
	data["shaderProfile"].get_to(node.shaderProfile);
	data["defines"]      .get_to(node.defines);
	data["compileArgs"]  .get_to(node.compileArgs);
	return data;
}

inline const auto& GetAttributes(const ComputeProgramNode& node) {
	return node.attributes;
}

inline void DrawNode(CommandContext& context, ComputeProgramNode& node) {
	DrawNodeTitle("Compute Pipeline");

	{
		ImGui::SetNextItemWidth(200);
		bool dirty = ImGui::InputText("Shader", &node.shaderPath);
		ImGui::SameLine();
		if (ImGui::Button("Choose...")) {
			if (auto r = pfd::open_file("Choose shader", kSrcFolder.string(), {
				//"All files (.*)", "*.*",
				"Shader files (.slang .hlsl .glsl .vert .frag .geom .tesc .tese .comp)", "*.slang *.hlsl *.glsl *.vert *.frag *.geom *.tesc *.tese *.comp",
			}, false).result(); !r.empty()) {
				node.shaderPath = *r.begin();
				if (std::filesystem::path(node.shaderPath).generic_string().starts_with(kSrcFolder.generic_string()))
					node.shaderPath = std::filesystem::relative(node.shaderPath, kSrcFolder).string();
				dirty = true;
			}
		}
		if (dirty) {
			node.CreatePipeline(context.GetDevice());
		}
	}

	for (const auto& a : node.attributes)
		DrawNodeAttribute(node.nodeId, a);

	if (!node.statusText.empty()) {
		ImGui::SetNextItemWidth(200);
		ImGui::TextUnformatted(node.statusText.c_str());
	}
}

}