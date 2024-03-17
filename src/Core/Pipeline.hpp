#pragma once

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
	ShaderParameterBinding   mRootBinding = {};
	DescriptorSetLayouts     mDescriptorSetLayouts;

public:
	static ref<PipelineLayout> Create(const Device& device, const ShaderStageMap& shaders, const PipelineLayoutInfo& info = {}, const DescriptorSetLayouts& descriptorSetLayouts = {});

	inline       vk::raii::PipelineLayout& operator*()        { return mLayout; }
	inline const vk::raii::PipelineLayout& operator*() const  { return mLayout; }
	inline       vk::raii::PipelineLayout* operator->()       { return &mLayout; }
	inline const vk::raii::PipelineLayout* operator->() const { return &mLayout; }

	inline const ShaderParameterBinding& RootBinding() const { return mRootBinding; }
	inline const DescriptorSetLayouts& GetDescriptorSetLayouts() const { return mDescriptorSetLayouts; }
};

struct Pipeline {
private:
	vk::raii::Pipeline        mPipeline = nullptr;
	ref<const PipelineLayout> mLayout = {};
	PipelineInfo              mInfo = {};
	ShaderStageMap            mShaders = {};

public:
	static ref<Pipeline> CreateCompute(const Device& device, const ref<const ShaderModule>& shader, const PipelineInfo& info = {}, const PipelineLayoutInfo& layoutInfo = {}, const DescriptorSetLayouts& descriptorSetLayouts = {});
	static ref<Pipeline> CreateCompute(const Device& device, const ref<const ShaderModule>& shader, const ref<PipelineLayout>& layout, const PipelineInfo& info = {});

	inline       vk::raii::Pipeline& operator*()        { return mPipeline; }
	inline const vk::raii::Pipeline& operator*() const  { return mPipeline; }
	inline       vk::raii::Pipeline* operator->()       { return &mPipeline; }
	inline const vk::raii::Pipeline* operator->() const { return &mPipeline; }

	inline const auto& Layout() const { return mLayout; }
	inline const auto& GetShader(const vk::ShaderStageFlagBits stage) const { return mShaders.at(stage); }
};

inline vk::Extent3D GetDispatchDim(const vk::Extent3D& workgroupSize, const vk::Extent3D& extent) {
	return {
		(extent.width  + workgroupSize.width - 1)  / workgroupSize.width,
		(extent.height + workgroupSize.height - 1) / workgroupSize.height,
		(extent.depth  + workgroupSize.depth - 1)  / workgroupSize.depth };
}

}