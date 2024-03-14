#pragma once

#include <future>
#include <shared_mutex>

#include "ShaderModule.hpp"

namespace RoseEngine {

struct PipelineLayoutInfo {
	vk::PipelineLayoutCreateFlags                flags = {};
	vk::DescriptorSetLayoutCreateFlags           descriptorSetLayoutFlags = {};
	NameMap<vk::DescriptorBindingFlags>          descriptorBindingFlags = {};
	NameMap<std::vector<ref<vk::raii::Sampler>>> immutableSamplers = {};
};

struct PipelineInfo {
	vk::PipelineCreateFlags             flags = {};
	vk::PipelineShaderStageCreateFlags  stageFlags = {};
};

using DescriptorSetLayouts = std::vector<ref<vk::raii::DescriptorSetLayout>>;
using ShaderStageMap = std::unordered_map<vk::ShaderStageFlagBits, ref<const ShaderModule>>;

struct PipelineLayout {
private:
	vk::raii::PipelineLayout mLayout = nullptr;
	PipelineLayoutInfo       mInfo = {};
	ShaderParameterBindings  mBindings = {};
	DescriptorSetLayouts     mDescriptorSetLayouts;

public:
	inline       vk::raii::PipelineLayout& operator*()        { return mLayout; }
	inline const vk::raii::PipelineLayout& operator*() const  { return mLayout; }
	inline       vk::raii::PipelineLayout* operator->()       { return &mLayout; }
	inline const vk::raii::PipelineLayout* operator->() const { return &mLayout; }

	inline const ShaderParameterBindings& Bindings() const { return mBindings; }
	inline const DescriptorSetLayouts& GetDescriptorSetLayouts() const { return mDescriptorSetLayouts; }

	static ref<PipelineLayout> Create(const Device& device, const ShaderStageMap& shaders, const PipelineLayoutInfo& info = {}, const DescriptorSetLayouts& descriptorSetLayouts = {});
};

struct Pipeline {
private:
	vk::raii::Pipeline        mPipeline = nullptr;
	ref<const PipelineLayout> mLayout = {};
	PipelineInfo              mInfo = {};
	ShaderStageMap            mShaders = {};

public:
	inline       vk::raii::Pipeline& operator*()        { return mPipeline; }
	inline const vk::raii::Pipeline& operator*() const  { return mPipeline; }
	inline       vk::raii::Pipeline* operator->()       { return &mPipeline; }
	inline const vk::raii::Pipeline* operator->() const { return &mPipeline; }

	inline const auto& Layout() const { return mLayout; }
	inline const auto& GetShader(const vk::ShaderStageFlagBits stage) const { return mShaders.at(stage); }

	static ref<Pipeline> CreateCompute(const Device& device, const ref<const ShaderModule>& shader, const PipelineInfo& info = {}, const PipelineLayoutInfo& layoutInfo = {}, const DescriptorSetLayouts& descriptorSetLayouts = {});
	static ref<Pipeline> CreateCompute(const Device& device, const ref<const ShaderModule>& shader, const ref<PipelineLayout>& layout, const PipelineInfo& info = {});
};

inline vk::Extent3D GetDispatchDim(const vk::Extent3D& workgroupSize, const vk::Extent3D& extent) {
	return {
		(extent.width  + workgroupSize.width - 1)  / workgroupSize.width,
		(extent.height + workgroupSize.height - 1) / workgroupSize.height,
		(extent.depth  + workgroupSize.depth - 1)  / workgroupSize.depth };
}

}