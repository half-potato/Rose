#pragma once

#include <imgui/imgui.h>

#include "Pipeline.hpp"
#include "Hash.hpp"

namespace RoseEngine {

using PipelineInfo = std::variant<GraphicsPipelineInfo, ComputePipelineInfo>;

class PipelineCache {
public:
	struct CacheKey {
		ShaderDefines defines;
		PipelineInfo pipelineInfo;
		bool operator==(const CacheKey&) const = default;
	};
	struct CacheKeyHasher {
		inline size_t operator()(const CacheKey& key) const {
			return HashArgs(key.defines, HashVariant(key.pipelineInfo));
		}
	};
	struct ShaderEntryPoint {
		std::filesystem::path path;
		std::string           entry;
	};

	inline PipelineCache(std::filesystem::path path, const std::string& entry = "main", PipelineLayoutInfo layoutInfo_ = {}) {
		stages = { ShaderEntryPoint{ path, entry } };
		cachedShaders.resize(1);
		layoutInfo = layoutInfo_;
	}

	// create from range of ShaderEntryPoint
	inline PipelineCache(const std::vector<ShaderEntryPoint>& stages_, PipelineLayoutInfo layoutInfo_ = {}) {
		stages = stages_;
		layoutInfo = layoutInfo_;
		cachedShaders.resize(stages.size());
	}

	PipelineCache() = default;
	PipelineCache(const PipelineCache&) = default;
	PipelineCache(PipelineCache&&) = default;
	PipelineCache& operator=(const PipelineCache&) = default;
	PipelineCache& operator=(PipelineCache&&) = default;

	inline operator bool() const { return !stages.empty(); }

	inline void clear() { cachedPipelines.clear(); cachedShaders.clear(); }

	inline const PipelineLayoutInfo& GetLayoutInfo() const {
		return layoutInfo;
	}
	inline void SetLayoutInfo(const PipelineLayoutInfo& layoutInfo_) {
		layoutInfo = layoutInfo_;
		clear();
	}


	inline ref<ShaderModule> getShader(Device& device, const uint32_t index, const ShaderDefines& defines = {}) {
		if (auto it = cachedShaders[index].find(defines); it != cachedShaders[index].end()) {
			// shader hot reload
			if (ImGui::IsKeyPressed(ImGuiKey_F5, false) && it->second->IsStale()) {
				cachedShaders[index].erase(it);
			} else {
				return it->second;
			}
		}

		auto shader = ShaderModule::Create(device, stages[index].path, stages[index].entry, "sm_6_7", defines);
		cachedShaders[index].emplace(defines, shader);
		return shader;
	}


	inline ref<Pipeline> get(Device& device, const ShaderDefines& defines = {}, const PipelineInfo& pipelineInfo = ComputePipelineInfo{}) {
		CacheKey key = { defines, pipelineInfo };

		if (auto it = cachedPipelines.find(key); it != cachedPipelines.end()) {
			// shader hot reload
			bool stale = false;
			if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
				for (const auto& shader : it->second->Shaders()) {
					if (shader->IsStale()) {
						stale = true;
						break;
					}
				}
			}
			if (stale) {
				device.Wait();
				cachedPipelines.erase(it);
			} else
				return it->second; // pipeline in cache
		}

		// pipeline not in cache. create & cache pipeline.

		ref<ShaderModule> computeShader, vertexShader, fragmentShader;
		for (uint32_t i = 0; i < stages.size(); i++) {
			ref<ShaderModule> shader = getShader(device, i, defines);
			switch (shader->Stage()) {
			case vk::ShaderStageFlagBits::eCompute:
				computeShader = shader;
				break;
			case vk::ShaderStageFlagBits::eVertex:
				vertexShader = shader;
				break;
			case vk::ShaderStageFlagBits::eFragment:
				fragmentShader = shader;
				break;
			}
		}

		ref<Pipeline> p;
		if (computeShader) {
			p = Pipeline::CreateCompute(device, computeShader, std::get<ComputePipelineInfo>(pipelineInfo), layoutInfo);
		} else {
			p = Pipeline::CreateGraphics(device, vertexShader, fragmentShader, std::get<GraphicsPipelineInfo>(pipelineInfo), layoutInfo);
		}

		cachedPipelines.emplace(key, p);

		return p;
	}

private:
	std::unordered_map<CacheKey, ref<Pipeline>, CacheKeyHasher> cachedPipelines;
	std::vector<std::unordered_map<ShaderDefines, ref<ShaderModule>>> cachedShaders;
	std::vector<ShaderEntryPoint> stages;
	PipelineLayoutInfo layoutInfo;
};

}