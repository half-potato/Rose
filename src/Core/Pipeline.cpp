#include "Pipeline.hpp"

#include <iostream>
#include <map>
#include <optional>

namespace RoseEngine {

PipelineLayout::PipelineLayout(const Device& device, const ShaderStageMap& shaders, const PipelineInfo& info, const DescriptorSetLayouts& descriptorSetLayouts) {
	mInfo = info;
	mDescriptorSetLayouts = descriptorSetLayouts;

	// combine descriptors and push constants from all shader stages
	using DescriptorBindingData = std::tuple<vk::DescriptorSetLayoutBinding, std::optional<vk::DescriptorBindingFlags>, std::vector<vk::Sampler>>;
	std::vector<std::map<uint32_t/*binding index*/, DescriptorBindingData>> bindings;

	uint32_t pushConstantRangeBegin = std::numeric_limits<uint32_t>::max();
	uint32_t pushConstantRangeEnd = 0;
	vk::ShaderStageFlags pushConstantStages = vk::ShaderStageFlags{0};

	for (const auto&[stage, shader] : shaders) {
		// push constants

		if (!shader->mPushConstants.empty()) {
			pushConstantStages |= stage;
			for (const auto& [id, p] : shader->mPushConstants) {
				pushConstantRangeBegin = std::min(pushConstantRangeBegin, p.mOffset);
				pushConstantRangeEnd   = std::max(pushConstantRangeEnd  , p.mOffset + p.mTypeSize);
				if (auto it = mPushConstants.find(id); it != mPushConstants.end()) {
					if (it->second.mOffset != p.mOffset || it->second.mTypeSize != p.mTypeSize)
						std::cerr << "Warning: Pipeline push constant " << id << " is specified with different offsets/sizes between shaders" << std::endl;
				} else
					mPushConstants.emplace(id, p);
			}
		}

		// uniforms

		for (const auto&[name,size] :  shader->mUniformBufferSizes) {
			if (!mUniformBufferSizes.contains(name))
				mUniformBufferSizes.emplace(name, 0);
			mUniformBufferSizes[name] = std::max<size_t>(mUniformBufferSizes[name], size);
		}

		for (const auto& [id, b] : shader->mUniforms) {
			if (auto it = mUniforms.find(id); it != mUniforms.end()) {
				if (it->second.mOffset != b.mOffset || it->second.mTypeSize != b.mTypeSize || it->second.mParentDescriptor != b.mParentDescriptor)
					std::cerr << "Warning: Pipeline uniform " << id << " is specified with different offsets/sizes/sets between shaders" << std::endl;
			} else
				mUniforms.emplace(id, b);
		}

		// descriptors

		for (const auto&[id, binding] : shader->mDescriptors) {
			mDescriptors[id] = binding;

			// compute total array size
			uint32_t descriptorCount = 1;
			for (const uint32_t v : binding.mArraySize)
				descriptorCount *= v;

			// get binding flags from mInfo
			std::optional<vk::DescriptorBindingFlags> flags;
			if (auto b_it = info.mDescriptorBindingFlags.find(id); b_it != info.mDescriptorBindingFlags.end())
				flags = b_it->second;

			// get immutable samplers from mInfo
			std::vector<vk::Sampler> samplers;
			if (auto s_it = info.mImmutableSamplers.find(id); s_it != info.mImmutableSamplers.end()) {
				samplers.resize(s_it->second.size());
				std::ranges::transform(s_it->second, samplers.begin(), [](const std::shared_ptr<vk::raii::Sampler>& s){ return **s; });
			}

			// increase set count if needed
			if (binding.mSet >= bindings.size())
				bindings.resize(binding.mSet + 1);

			// copy bindings

			auto& setBindings = bindings[binding.mSet];

			auto it = setBindings.find(binding.mBinding);
			if (it == setBindings.end())
				it = setBindings.emplace(binding.mBinding,
					std::tuple{
						vk::DescriptorSetLayoutBinding(
							binding.mBinding,
							binding.mDescriptorType,
							descriptorCount,
							shader->mStage, {}),
						flags,
						samplers }).first;
			else {
				auto&[setLayoutBinding, flags, samplers] = it->second;

				if (setLayoutBinding.descriptorType != binding.mDescriptorType)
					throw std::logic_error("Shader modules contain descriptors of different types at the same binding");
				if (setLayoutBinding.descriptorCount != descriptorCount)
					throw std::logic_error("Shader modules contain descriptors with different counts at the same binding");

				setLayoutBinding.stageFlags |= shader->mStage;
			}
		}
	}

	// create DescriptorSetLayouts

	mDescriptorSetLayouts.resize(bindings.size());
	for (uint32_t i = 0; i < bindings.size(); i++) {
		if (mDescriptorSetLayouts[i]) continue;
		std::vector<vk::DescriptorSetLayoutBinding> layoutBindings;
		std::vector<vk::DescriptorBindingFlags> bindingFlags;
		bool hasFlags = false;
		for (const auto&[bindingIndex, binding_] : bindings[i]) {
			const auto&[binding, flag, samplers] = binding_;
			if (flag) hasFlags = true;
			bindingFlags.emplace_back(flag ? *flag : vk::DescriptorBindingFlags{});

			auto& b = layoutBindings.emplace_back(binding);
			if (!samplers.empty())
				b.setImmutableSamplers(samplers);
		}

		vk::DescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo(bindingFlags);
		mDescriptorSetLayouts[i] = std::make_shared<vk::raii::DescriptorSetLayout>(*device, vk::DescriptorSetLayoutCreateInfo(info.mDescriptorSetLayoutFlags, layoutBindings, hasFlags ? &bindingFlagsInfo : nullptr));
	}

	// create pipelinelayout from descriptors and pushconstants

	std::vector<vk::PushConstantRange> pushConstantRanges;
	if (pushConstantStages != vk::ShaderStageFlags{0})
		pushConstantRanges.emplace_back(pushConstantStages, pushConstantRangeBegin, pushConstantRangeEnd - pushConstantRangeBegin);

	std::vector<vk::DescriptorSetLayout> vklayouts;
	for (const auto& ds : mDescriptorSetLayouts)
		vklayouts.emplace_back(**ds);
	mLayout = vk::raii::PipelineLayout(*device, vk::PipelineLayoutCreateInfo(info.mLayoutFlags, vklayouts, pushConstantRanges));
}

std::shared_ptr<Pipeline> CreateComputePipeline(const Device& device, const std::shared_ptr<const ShaderModule>& shader, const PipelineInfo& info, const DescriptorSetLayouts& descriptorSetLayouts) {
	std::shared_ptr<Pipeline> pipeline = std::make_shared<Pipeline>();
	pipeline->mShaders = { { shader->mStage, shader } };
	pipeline->mLayout = std::make_shared<PipelineLayout>(device, pipeline->mShaders, info, descriptorSetLayouts);
	pipeline->mPipeline = vk::raii::Pipeline(*device, device.mPipelineCache, vk::ComputePipelineCreateInfo(
		info.mFlags,
		vk::PipelineShaderStageCreateInfo(info.mStageLayoutFlags, vk::ShaderStageFlagBits::eCompute, ***shader, "main"),
		***pipeline->mLayout));

	return pipeline;
}

}