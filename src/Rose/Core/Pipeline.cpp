#include "Pipeline.hpp"

#include <iostream>
#include <map>
#include <optional>

//#define LOG_SHADER_REFLECTION

namespace RoseEngine {

struct PipelineBindings {
	// combine descriptors and push constants from all shader stages
	using DescriptorBindingData = std::tuple<
		vk::DescriptorSetLayoutBinding,
		std::optional<vk::DescriptorBindingFlags>,
		std::vector<vk::Sampler> >;
	std::vector<std::map<uint32_t/*binding index*/, DescriptorBindingData>> bindingData = {};

	uint32_t pushConstantRangeBegin = std::numeric_limits<uint32_t>::max();
	uint32_t pushConstantRangeEnd = 0;
	vk::ShaderStageFlags pushConstantStages = vk::ShaderStageFlags{0};

	// inserts bindings in shaderBinding into pipelineBinding
	void AddBindings(ShaderParameterBinding& pipelineBinding, const ShaderParameterBinding& shaderBinding, const vk::ShaderStageFlagBits stage, const PipelineLayoutInfo& info, uint32_t constantOffset = 0, const std::string parentName = "") {
		for (const auto&[id, subBinding] : shaderBinding) {
			auto it = pipelineBinding.find(id);
			const bool hasBinding = it != pipelineBinding.end();
			if (!hasBinding) {
				pipelineBinding[id] = subBinding.raw_variant();  // dont include sub parameters
			}

			// all bindings should have string ids
			std::string name = std::get<std::string>(id);

			std::string fullName;
			if (parentName == "")
				fullName = name;
			else
				fullName = parentName + "." + name;

			uint32_t offset = constantOffset;

			if (const auto* binding = subBinding.get_if<ShaderConstantBinding>()) {
				if (hasBinding) {
					const ShaderParameterBinding& bp = it->second;
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
						if (setLayoutBinding.descriptorType != vk::DescriptorType::eUniformBuffer)
							throw std::logic_error("Shader modules contain different descriptors at the same binding index " + std::to_string(binding->setIndex) + "." + std::to_string(setLayoutBinding.binding));
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
					const ShaderParameterBinding& bp = it->second;
					const auto* b = bp.get_if<ShaderDescriptorBinding>();
					if (!b || *b != *binding) {
						throw std::logic_error("Binding appears multiple times, but is not the same");
					}
				}

				// get binding flags from layout info
				std::optional<vk::DescriptorBindingFlags> flags;
				if (auto b_it = info.descriptorBindingFlags.find(fullName); b_it != info.descriptorBindingFlags.end())
					flags = b_it->second;

				// get immutable samplers from layout info
				std::vector<vk::Sampler> samplers;
				if (auto s_it = info.immutableSamplers.find(fullName); s_it != info.immutableSamplers.end()) {
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
						throw std::logic_error("Shader modules contain different descriptors at the same binding index " + std::to_string(binding->setIndex) + "." + std::to_string(setLayoutBinding.binding));
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

			AddBindings(pipelineBinding[id], subBinding, stage, info, offset, fullName);
		}
	};
};

void PrintBinding(const ShaderParameterBinding& binding, uint32_t depth) {
	if (const auto* c = binding.get_if<ShaderStructBinding>()) {
		if (c->arraySize > 1) {
			std::cout << "[" << c->arraySize << "]";
		}
		std::cout << " descriptor stride: " << c->descriptorStride;
		std::cout << " uniform stride: " << c->uniformStride;
	} else if (const auto* c = binding.get_if<ShaderConstantBinding>()) {
		if (c->arraySize > 1) std::cout << "[" << c->arraySize << "]";
		std::cout << " " << c->setIndex << "." << c->bindingIndex << " ";
		if (c->pushConstant) std::cout << "Push";
		std::cout << "Constant";
		std::cout << " " << c->typeSize << "B at " << c->offset << "B";
	} else if (const auto* c = binding.get_if<ShaderDescriptorBinding>()) {
		if (c->arraySize > 1) std::cout << "[" << c->arraySize << "]";
		std::cout << " " << c->setIndex << "." << c->bindingIndex << " " << vk::to_string(c->descriptorType);
	} else if (const auto* c = binding.get_if<ShaderVertexAttributeBinding>()) {
		std::cout << " : " << c->semantic << c->semanticIndex << " location = " << c->location;
	}
	std::cout << std::endl;
	for (const auto& [name, subBinding] : binding) {
		std::cout << std::string(depth, '\t') << name;// << std::endl;
		PrintBinding(subBinding, depth + 1);
	}
}

ref<PipelineLayout> PipelineLayout::Create(const Device& device, const vk::ArrayProxy<const ref<const ShaderModule>>& shaders, const PipelineLayoutInfo& info, const DescriptorSetLayouts& descriptorSetLayouts) {
	auto layout = make_ref<PipelineLayout>();
	layout->mInfo = info;
	layout->mRootBinding = ShaderParameterBinding{};

	PipelineBindings bindings = {};

	auto GetPipelineStage = [](auto stage) -> vk::PipelineStageFlagBits2 {
		switch (stage) {
			default: return (vk::PipelineStageFlagBits2)0;
			case vk::ShaderStageFlagBits::eVertex:                 return vk::PipelineStageFlagBits2::eVertexShader;
			case vk::ShaderStageFlagBits::eTessellationControl:    return vk::PipelineStageFlagBits2::eTessellationControlShader;
			case vk::ShaderStageFlagBits::eTessellationEvaluation: return vk::PipelineStageFlagBits2::eTessellationEvaluationShader;
			case vk::ShaderStageFlagBits::eGeometry:               return vk::PipelineStageFlagBits2::eGeometryShader;
			case vk::ShaderStageFlagBits::eFragment:               return vk::PipelineStageFlagBits2::eFragmentShader;
			case vk::ShaderStageFlagBits::eCompute:                return vk::PipelineStageFlagBits2::eComputeShader;
			case vk::ShaderStageFlagBits::eRaygenKHR:              return vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
			case vk::ShaderStageFlagBits::eAnyHitKHR:              return vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
			case vk::ShaderStageFlagBits::eClosestHitKHR:          return vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
			case vk::ShaderStageFlagBits::eMissKHR:                return vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
			case vk::ShaderStageFlagBits::eIntersectionKHR:        return vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
		}
	};

	layout->mStageMask         = (vk::ShaderStageFlagBits)0;
	layout->mPipelineStageMask = (vk::PipelineStageFlagBits2)0;
	for (const auto& shader : shaders) {
		bindings.AddBindings(layout->mRootBinding, shader->RootBinding(), shader->Stage(), info);
		layout->mStageMask |= shader->Stage();
		layout->mPipelineStageMask |= GetPipelineStage(shader->Stage());
	}

	#ifdef LOG_SHADER_REFLECTION
	PrintBinding(layout->mRootBinding);
	#endif

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
		device.SetDebugName(**layout->mDescriptorSetLayouts[i], shaders.front()->SourceFiles()[0].filename().string() + ":" + shaders.front()->EntryPointName() + ":" + std::to_string(i));
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
	device.SetDebugName(*layout->mLayout, shaders.front()->SourceFiles()[0].filename().string() + ":" + shaders.front()->EntryPointName());

	return layout;
}

ref<Pipeline> Pipeline::CreateCompute(const Device& device, const ref<const ShaderModule>& shader, const ref<PipelineLayout>& layout, const ComputePipelineInfo& info) {
	ref<Pipeline> pipeline = make_ref<Pipeline>();
	pipeline->mLayout = layout;
	pipeline->mShaders = { shader };
	pipeline->mPipeline = device->createComputePipeline(device.PipelineCache(), vk::ComputePipelineCreateInfo{
		.flags = info.flags,
		.stage = vk::PipelineShaderStageCreateInfo{
			.flags = info.stageFlags,
			.stage = vk::ShaderStageFlagBits::eCompute,
			.module = ***shader,
			.pName = "main" },
		.layout = ***layout });
	device.SetDebugName(***pipeline, shader->SourceFiles()[0].stem().string() + ":" + shader->EntryPointName());
	return pipeline;
}

ref<Pipeline> Pipeline::CreateCompute(const Device& device, const ref<const ShaderModule>& shader, const ComputePipelineInfo& info, const PipelineLayoutInfo& layoutInfo, const DescriptorSetLayouts& descriptorSetLayouts) {
	auto layout = PipelineLayout::Create(device, shader, layoutInfo, descriptorSetLayouts);
	return CreateCompute(device, shader, layout, info);
}


ref<Pipeline> Pipeline::CreateGraphics(const Device& device, const vk::ArrayProxy<const ref<const ShaderModule>>& shaders, const GraphicsPipelineInfo& info, const PipelineLayoutInfo& layoutInfo, const DescriptorSetLayouts& descriptorSetLayouts) {
	// Pipeline constructor creates mLayout, mDescriptorSetLayouts, and mDescriptorMap

	// create pipeline
	ref<Pipeline> pipeline = make_ref<Pipeline>();
	pipeline->mLayout = PipelineLayout::Create(device, shaders, layoutInfo, descriptorSetLayouts);
	pipeline->mShaders.resize(shaders.size());
	std::ranges::copy(shaders, pipeline->mShaders.begin());

	std::string name;

	std::vector<vk::PipelineShaderStageCreateInfo> stages;
	for (const auto& shader : shaders) {
		name += shader->SourceFiles()[0].stem().string() + ":" + shader->EntryPointName();
		stages.emplace_back(vk::PipelineShaderStageCreateInfo{
			.flags = info.stageFlags,
			.stage = shader->Stage(),
			.module = ***shader,
			.pName = "main" });
		}

	vk::PipelineRenderingCreateInfo dynamicRenderingState = {};
	if (info.dynamicRenderingState) {
		dynamicRenderingState = vk::PipelineRenderingCreateInfo{
			.viewMask = info.dynamicRenderingState->viewMask,
			.depthAttachmentFormat = info.dynamicRenderingState->depthFormat,
			.stencilAttachmentFormat = info.dynamicRenderingState->stencilFormat };
		dynamicRenderingState.setColorAttachmentFormats(info.dynamicRenderingState->colorFormats);
	}

	vk::PipelineVertexInputStateCreateInfo vertexInputState = {};
	if (info.vertexInputState) {
		vertexInputState.setVertexBindingDescriptions(info.vertexInputState->bindings);
		vertexInputState.setVertexAttributeDescriptions(info.vertexInputState->attributes);
	}

	vk::PipelineViewportStateCreateInfo viewportState = {};
	viewportState.setViewports(info.viewports);
	viewportState.setScissors(info.scissors);

	vk::PipelineColorBlendStateCreateInfo colorBlendState = {};
	if (info.colorBlendState) {
		colorBlendState = vk::PipelineColorBlendStateCreateInfo{
			.logicOpEnable   = info.colorBlendState->logicOpEnable,
			.logicOp         = info.colorBlendState->logicOp,
			.blendConstants  = info.colorBlendState->blendConstants };
		colorBlendState.setAttachments(info.colorBlendState->attachments);
	}

	vk::PipelineDynamicStateCreateInfo dynamicState = {};
	dynamicState.setDynamicStates(info.dynamicStates);

	vk::GraphicsPipelineCreateInfo createInfo = {
		.pNext               = info.dynamicRenderingState.has_value() ? &dynamicRenderingState : nullptr,
		.flags               = info.flags,
		.pVertexInputState   = info.vertexInputState.has_value()   ? &vertexInputState : nullptr,
		.pInputAssemblyState = info.inputAssemblyState.has_value() ? &info.inputAssemblyState.value() : nullptr,
		.pTessellationState  = info.tessellationState.has_value()  ? &info.tessellationState.value()  : nullptr,
		.pViewportState      = &viewportState,
		.pRasterizationState = info.rasterizationState.has_value() ? &info.rasterizationState.value() : nullptr,
		.pMultisampleState   = info.multisampleState.has_value()   ? &info.multisampleState.value()   : nullptr,
		.pDepthStencilState  = info.depthStencilState.has_value()  ? &info.depthStencilState.value()  : nullptr,
		.pColorBlendState    = info.colorBlendState.has_value()    ? &colorBlendState                 : nullptr,
		.pDynamicState       = &dynamicState,
		.layout              = ***pipeline->mLayout,
		.renderPass          = info.renderPass,
		.subpass             = info.subpassIndex };
	createInfo.setStages(stages);
	pipeline->mPipeline = device->createGraphicsPipeline(device.PipelineCache(), createInfo);
	device.SetDebugName(***pipeline, name);

	return pipeline;
}

}