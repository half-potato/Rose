#pragma once

#include <future>
#include <shared_mutex>

#include "ShaderModule.hpp"

namespace RoseEngine {

struct PipelineInfo {
	vk::PipelineShaderStageCreateFlags  mStageLayoutFlags;
	vk::PipelineLayoutCreateFlags       mLayoutFlags;
	vk::PipelineCreateFlags             mFlags;
	vk::DescriptorSetLayoutCreateFlags  mDescriptorSetLayoutFlags;
	NameMap<vk::DescriptorBindingFlags> mDescriptorBindingFlags;
	NameMap<std::vector<std::shared_ptr<vk::raii::Sampler>>> mImmutableSamplers;
};

using DescriptorSetLayouts = std::vector<std::shared_ptr<vk::raii::DescriptorSetLayout>>;
using ShaderStageMap = std::unordered_map<vk::ShaderStageFlagBits, std::shared_ptr<const ShaderModule>>;

struct PipelineLayout {
	vk::raii::PipelineLayout         mLayout = nullptr;
	PipelineInfo                     mInfo;
	DescriptorSetLayouts             mDescriptorSetLayouts;
	NameMap<ShaderDescriptorBinding> mDescriptors;
	NameMap<ShaderConstantBinding>   mUniforms;
	NameMap<vk::DeviceSize>          mUniformBufferSizes;
	NameMap<ShaderConstantBinding>   mPushConstants;

	inline       vk::raii::PipelineLayout& operator*()        { return mLayout; }
	inline const vk::raii::PipelineLayout& operator*() const  { return mLayout; }
	inline       vk::raii::PipelineLayout* operator->()       { return &mLayout; }
	inline const vk::raii::PipelineLayout* operator->() const { return &mLayout; }

	PipelineLayout() = default;
	PipelineLayout(PipelineLayout&&) = default;
	PipelineLayout(const Device& device, const ShaderStageMap& shaders, const PipelineInfo& info = {}, const DescriptorSetLayouts& descriptorSetLayouts = {});
};

struct Pipeline {
	vk::raii::Pipeline mPipeline = nullptr;
	std::shared_ptr<PipelineLayout> mLayout;
	ShaderStageMap mShaders;

	inline       vk::raii::Pipeline& operator*()        { return mPipeline; }
	inline const vk::raii::Pipeline& operator*() const  { return mPipeline; }
	inline       vk::raii::Pipeline* operator->()       { return &mPipeline; }
	inline const vk::raii::Pipeline* operator->() const { return &mPipeline; }
};

std::shared_ptr<Pipeline> CreateComputePipeline(const Device& device, const std::shared_ptr<const ShaderModule>& shader, const PipelineInfo& info = {}, const DescriptorSetLayouts& descriptorSetLayouts = {});

}