#include "Pipeline.hpp"

#include <iostream>
#include <map>
#include <optional>

namespace RoseEngine {

struct PipelineBindings {
	// combine descriptors and push constants from all shader stages
	using DescriptorBindingData = std::tuple<
		vk::DescriptorSetLayoutBinding,
		std::optional<vk::DescriptorBindingFlags>,
		std::vector<vk::Sampler> >;
	std::vector<std::map<uint32_t/*binding index*/, DescriptorBindingData>> bindingData;

	uint32_t pushConstantRangeBegin = std::numeric_limits<uint32_t>::max();
	uint32_t pushConstantRangeEnd = 0;
	vk::ShaderStageFlags pushConstantStages = vk::ShaderStageFlags{0};

	// inserts bindings in shaderBinding into pipelineBinding
	void AddBindings(ShaderParameterBinding& pipelineBinding, const ShaderParameterBinding& shaderBinding, const vk::ShaderStageFlagBits stage, const PipelineLayoutInfo& info, uint32_t constantOffset = 0) {
		for (const auto&[name, subBinding] : shaderBinding) {
			auto it = pipelineBinding.find(name);
			const bool hasBinding = it != pipelineBinding.end();
			if (!hasBinding)
				pipelineBinding[name] = subBinding;

			uint32_t offset = constantOffset;

			if (const auto* binding = subBinding.get_if<ShaderConstantBinding>()) {
				if (hasBinding) {
					const ShaderParameterBinding& bp = (*it).second;
					const auto* b = bp.get_if<ShaderConstantBinding>();
					if (!b || *b != *binding) {
						throw std::logic_error("Binding appears multiple times, but is not the same");
					}
				}

				offset += binding->offset;

				if (binding->pushConstant) {
					pushConstantRangeBegin = std::min(pushConstantRangeBegin, offset);
					pushConstantRangeEnd   = std::max(pushConstantRangeEnd  , offset + binding->typeSize);
					pushConstantStages |= stage;
				} else {
					if (binding->setIndex >= bindingData.size())
						bindingData.resize(binding->setIndex + 1);
					auto& setBindingData = bindingData[binding->setIndex];

					if (auto it = setBindingData.find(binding->bindingIndex); it != setBindingData.end()) {
						auto&[setLayoutBinding, flags, samplers] = it->second;
						if (setLayoutBinding.descriptorType  != vk::DescriptorType::eUniformBuffer)
							throw std::logic_error("Shader modules contain different descriptors at the same binding index");
						setLayoutBinding.stageFlags |= stage;
					} else
						setBindingData.emplace(binding->bindingIndex, DescriptorBindingData{
							vk::DescriptorSetLayoutBinding{
								.binding         = binding->bindingIndex,
								.descriptorType  = vk::DescriptorType::eUniformBuffer,
								.descriptorCount = 1,
								.stageFlags      = stage },
							{},
							{} });
				}
			} else if (const auto* binding = subBinding.get_if<ShaderDescriptorBinding>()) {
				if (hasBinding) {
					const ShaderParameterBinding& bp = (*it).second;
					const auto* b = bp.get_if<ShaderDescriptorBinding>();
					if (!b || *b != *binding) {
						throw std::logic_error("Binding appears multiple times, but is not the same");
					}
					continue;
				}

				// get binding flags from layout info
				std::optional<vk::DescriptorBindingFlags> flags;
				if (auto b_it = info.descriptorBindingFlags.find(name); b_it != info.descriptorBindingFlags.end())
					flags = b_it->second;

				// get immutable samplers from layout info
				std::vector<vk::Sampler> samplers;
				if (auto s_it = info.immutableSamplers.find(name); s_it != info.immutableSamplers.end()) {
					samplers.resize(s_it->second.size());
					std::ranges::transform(s_it->second, samplers.begin(), [](const ref<vk::raii::Sampler>& s){ return **s; });
				}

				// increase set count if needed
				if (binding->setIndex >= bindingData.size())
					bindingData.resize(binding->setIndex + 1);
				auto& setBindingData = bindingData[binding->setIndex];

				if (auto it = setBindingData.find(binding->bindingIndex); it != setBindingData.end()) {
					auto&[setLayoutBinding, flags, samplers] = it->second;
					if (setLayoutBinding.descriptorType  != binding->descriptorType ||
						setLayoutBinding.descriptorCount != binding->arraySize)
						throw std::logic_error("Shader modules contain different descriptors at the same binding index");
					setLayoutBinding.stageFlags |= stage;
				} else {
					setBindingData.emplace(binding->bindingIndex, DescriptorBindingData{
						vk::DescriptorSetLayoutBinding{
							.binding         = binding->bindingIndex,
							.descriptorType  = binding->descriptorType,
							.descriptorCount = binding->arraySize,
							.stageFlags      = stage },
						flags,
						samplers });
				}
			}

			AddBindings(pipelineBinding[name], subBinding, stage, info, offset);
		}
	};
};

inline void PrintBinding(const ShaderParameterBinding& binding, uint32_t depth = 0) {
	std::cout << " ";
	if (const auto* c = binding.get_if<ShaderConstantBinding>()) {
		std::cout << " " << c->setIndex << "." << c->bindingIndex << " ";
		if (c->pushConstant) std::cout << "Push";
		std::cout << "Constant";
		std::cout << " " << c->offset << " + " << c->typeSize;
	} else if (const auto* c = binding.get_if<ShaderDescriptorBinding>()) {
		std::cout << c->setIndex << "." << c->bindingIndex << " " << vk::to_string(c->descriptorType);
	}
	std::cout << std::endl;
	for (const auto& [name, subBinding] : binding) {
		std::cout << std::string(depth, '\t') << name;// << std::endl;
		PrintBinding(subBinding, depth + 1);
	}
}

ref<PipelineLayout> PipelineLayout::Create(const Device& device, const ShaderStageMap& shaders, const PipelineLayoutInfo& info, const DescriptorSetLayouts& descriptorSetLayouts) {
	auto layout = make_ref<PipelineLayout>();
	layout->mInfo = info;

	layout->mRootBinding = ShaderParameterBinding{};

	PipelineBindings bindings = {};

	for (const auto&[stage, shader] : shaders)
		bindings.AddBindings(layout->mRootBinding, shader->RootBinding(), stage, info);

	//PrintBinding(layout->mRootBinding);

	// create DescriptorSetLayouts

	layout->mDescriptorSetLayouts = descriptorSetLayouts;
	layout->mDescriptorSetLayouts.resize(bindings.bindingData.size());
	for (uint32_t i = 0; i < bindings.bindingData.size(); i++) {
		if (layout->mDescriptorSetLayouts[i]) continue;
		std::vector<vk::DescriptorSetLayoutBinding> layoutBindings;
		std::vector<vk::DescriptorBindingFlags>     bindingFlags;
		bool hasFlags = false;
		for (const auto&[bindingIndex, binding_] : bindings.bindingData[i]) {
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
	if (bindings.pushConstantStages != vk::ShaderStageFlags{0})
		pushConstantRanges.emplace_back(bindings.pushConstantStages, bindings.pushConstantRangeBegin, bindings.pushConstantRangeEnd - bindings.pushConstantRangeBegin);

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