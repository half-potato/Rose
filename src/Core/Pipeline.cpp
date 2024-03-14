#include "Pipeline.hpp"

#include <iostream>
#include <map>
#include <optional>

namespace RoseEngine {

ref<PipelineLayout> PipelineLayout::Create(const Device& device, const ShaderStageMap& shaders, const PipelineLayoutInfo& info, const DescriptorSetLayouts& descriptorSetLayouts) {
	auto layout = make_ref<PipelineLayout>();
	layout->mInfo = info;

	// combine descriptors and push constants from all shader stages
	using DescriptorBindingData = std::tuple<
		vk::DescriptorSetLayoutBinding,
		std::optional<vk::DescriptorBindingFlags>,
		std::vector<vk::Sampler> >;
	std::vector<std::map<uint32_t/*binding index*/, DescriptorBindingData>> bindingData;

	uint32_t pushConstantRangeBegin = std::numeric_limits<uint32_t>::max();
	uint32_t pushConstantRangeEnd = 0;
	vk::ShaderStageFlags pushConstantStages = vk::ShaderStageFlags{0};

	auto& bindings = layout->mBindings;

	for (const auto&[stage, shader] : shaders) {
		// push constants

		auto& shaderBindings = shader->Bindings();

		if (!shaderBindings.pushConstants.empty()) {
			pushConstantStages |= stage;
			for (const auto& [id, p] : shaderBindings.pushConstants) {
				pushConstantRangeBegin = std::min(pushConstantRangeBegin, p.offset);
				pushConstantRangeEnd   = std::max(pushConstantRangeEnd  , p.offset + p.typeSize);
				if (auto it = bindings.pushConstants.find(id); it != bindings.pushConstants.end()) {
					if (it->second.offset != p.offset || it->second.typeSize != p.typeSize)
						std::cerr << "Warning: Pipeline push constant " << id << " is specified with different offsets/sizes between shaders" << std::endl;
				} else
					bindings.pushConstants.emplace(id, p);
			}
		}

		// uniforms

		for (const auto&[name,size] :  shaderBindings.uniformBufferSizes) {
			if (!bindings.uniformBufferSizes.contains(name))
				bindings.uniformBufferSizes[name] = std::max<size_t>(bindings.uniformBufferSizes[name], size);
			else
				bindings.uniformBufferSizes[name] = size;
		}

		for (const auto& [id, b] : shaderBindings.uniforms) {
			if (auto it = bindings.uniforms.find(id); it != bindings.uniforms.end()) {
				if (it->second.offset != b.offset || it->second.typeSize != b.typeSize || it->second.parentDescriptor != b.parentDescriptor)
					std::cerr << "Warning: Pipeline uniform " << id << " is specified with different offsets/sizes/sets between shaders" << std::endl;
			} else
				bindings.uniforms.emplace(id, b);
		}

		// descriptors

		for (const auto&[id, binding] : shaderBindings.descriptors) {
			bindings.descriptors[id] = binding;

			// compute total array size
			uint32_t descriptorCount = 1;
			for (const uint32_t v : binding.arraySize)
				descriptorCount *= v;

			// get binding flags from layout info
			std::optional<vk::DescriptorBindingFlags> flags;
			if (auto b_it = info.descriptorBindingFlags.find(id); b_it != info.descriptorBindingFlags.end())
				flags = b_it->second;

			// get immutable samplers from layout info
			std::vector<vk::Sampler> samplers;
			if (auto s_it = info.immutableSamplers.find(id); s_it != info.immutableSamplers.end()) {
				samplers.resize(s_it->second.size());
				std::ranges::transform(s_it->second, samplers.begin(), [](const ref<vk::raii::Sampler>& s){ return **s; });
			}

			// increase set count if needed
			if (binding.setIndex >= bindingData.size())
				bindingData.resize(binding.setIndex + 1);

			// copy binding data

			auto& setBindingData = bindingData[binding.setIndex];

			if (auto it = setBindingData.find(binding.bindingIndex); it != setBindingData.end()){
				auto&[setLayoutBinding, flags, samplers] = it->second;
				if (setLayoutBinding.descriptorType != binding.descriptorType) throw std::logic_error("Shader modules contain descriptors of different types at the same binding");
				if (setLayoutBinding.descriptorCount != descriptorCount)       throw std::logic_error("Shader modules contain descriptors with different counts at the same binding");
				setLayoutBinding.stageFlags |= stage;
			} else
				setBindingData.emplace(binding.bindingIndex, DescriptorBindingData{
					vk::DescriptorSetLayoutBinding{
						.binding = binding.bindingIndex,
						.descriptorType = binding.descriptorType,
						.descriptorCount = descriptorCount,
						.stageFlags = stage },
					flags,
					samplers });
		}
	}

	// create DescriptorSetLayouts

	layout->mDescriptorSetLayouts = descriptorSetLayouts;
	layout->mDescriptorSetLayouts.resize(bindingData.size());
	for (uint32_t i = 0; i < bindingData.size(); i++) {
		if (layout->mDescriptorSetLayouts[i]) continue;
		std::vector<vk::DescriptorSetLayoutBinding> layoutBindings;
		std::vector<vk::DescriptorBindingFlags> bindingFlags;
		bool hasFlags = false;
		for (const auto&[bindingIndex, binding_] : bindingData[i]) {
			const auto&[binding, flag, samplers] = binding_;
			if (flag) hasFlags = true;
			bindingFlags.emplace_back(flag ? *flag : vk::DescriptorBindingFlags{});

			auto& b = layoutBindings.emplace_back(binding);
			if (!samplers.empty())
				b.setImmutableSamplers(samplers);
		}

		vk::DescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo = {};
		bindingFlagsInfo.setBindingFlags(bindingFlags);
		vk::DescriptorSetLayoutCreateInfo createInfo = {};
		createInfo.flags = info.descriptorSetLayoutFlags;
		createInfo.setBindings(layoutBindings);
		if (hasFlags) createInfo.setPNext(&bindingFlagsInfo);
		layout->mDescriptorSetLayouts[i] = make_ref<vk::raii::DescriptorSetLayout>(std::move(device->createDescriptorSetLayout(createInfo)));
	}

	// create pipelinelayout from descriptors and pushconstants

	std::vector<vk::PushConstantRange> pushConstantRanges;
	if (pushConstantStages != vk::ShaderStageFlags{0})
		pushConstantRanges.emplace_back(pushConstantStages, pushConstantRangeBegin, pushConstantRangeEnd - pushConstantRangeBegin);

	std::vector<vk::DescriptorSetLayout> vklayouts;
	for (const auto& ds : layout->mDescriptorSetLayouts)
		vklayouts.emplace_back(**ds);
	vk::PipelineLayoutCreateInfo createInfo = {};
	createInfo.flags = info.flags;
	createInfo.setSetLayouts(vklayouts);
	createInfo.setPushConstantRanges(pushConstantRanges);
	layout->mLayout = device->createPipelineLayout(createInfo);

	return layout;
}

ref<Pipeline> Pipeline::CreateCompute(const Device& device, const ref<const ShaderModule>& shader, const PipelineInfo& info, const PipelineLayoutInfo& layoutInfo, const DescriptorSetLayouts& descriptorSetLayouts) {
	ref<Pipeline> pipeline = make_ref<Pipeline>();
	pipeline->mShaders = { { shader->mStage, shader } };
	pipeline->mLayout = PipelineLayout::Create(device, pipeline->mShaders, layoutInfo, descriptorSetLayouts);
	pipeline->mInfo = info;
	pipeline->mPipeline = device->createComputePipeline(device.PipelineCache(), vk::ComputePipelineCreateInfo{
		.flags = info.flags,
		.stage = vk::PipelineShaderStageCreateInfo{
			.flags = info.stageFlags,
			.stage = vk::ShaderStageFlagBits::eCompute,
			.module = ***shader,
			.pName = "main" },
		.layout = ***pipeline->mLayout });
	return pipeline;
}

ref<Pipeline> Pipeline::CreateCompute(const Device& device, const ref<const ShaderModule>& shader, const ref<PipelineLayout>& layout, const PipelineInfo& info) {
	ref<Pipeline> pipeline = make_ref<Pipeline>();
	pipeline->mLayout = layout;
	pipeline->mInfo = info;
	pipeline->mShaders = { { shader->mStage, shader } };
	pipeline->mPipeline = device->createComputePipeline(device.PipelineCache(), vk::ComputePipelineCreateInfo{
		.flags = info.flags,
		.stage = vk::PipelineShaderStageCreateInfo{
			.flags = info.stageFlags,
			.stage = vk::ShaderStageFlagBits::eCompute,
			.module = ***shader,
			.pName = "main" },
		.layout = ***layout });
	return pipeline;
}

}