#pragma once

#include "Pipeline.hpp"
#include "ShaderParameterBlock.hpp"

namespace RoseEngine {

struct Program {
	std::shared_ptr<Pipeline> mPipeline;
	ShaderParameterBlock      mParameters;

	template<std::convertible_to<ShaderParameterValue> ... Args>
	inline void operator()(const vk::raii::CommandBuffer& commandBuffer, const vk::Extent3D& threadCount, Args... args) {
		const auto& shader = *mPipeline->mShaders.at(vk::ShaderStageFlagBits::eCompute);
		if (sizeof...(args) != shader.mEntryPointArguments.size())
			throw std::logic_error("Expected " + std::to_string(shader.mEntryPointArguments.size()) + " arguments, but got " + std::to_string(sizeof...(args)));

		size_t i = 0;
		for (const ShaderParameterValue arg : { ShaderParameterValue(args)... })
			mParameters(shader.mEntryPointArguments[i++]) = arg;

		commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, ***mPipeline);

		std::vector<vk::DescriptorSet> descriptorSets(mParameters.mDescriptorSets.size());
		std::ranges::transform(mParameters.mDescriptorSets, descriptorSets.begin(), [](const auto& ds) { return **ds; });
		commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, ***mPipeline->mLayout, 0, descriptorSets);

		for (const auto& [name, c] : shader.mPushConstants) {
			if (mParameters.Contains(name)) {
				if (const ConstantParameter* p = std::get_if<ConstantParameter>(&mParameters(name))) {
					commandBuffer.pushConstants<std::byte>(***mPipeline->mLayout, vk::ShaderStageFlagBits::eCompute, c.mOffset, *p);
				}
			}
		}

		auto dim = GetDispatchDim(shader.mWorkgroupSize, threadCount);
		commandBuffer.dispatch(dim.width, dim.height, dim.depth);
	}
};

Program CreateProgram(const Device& device, const std::string& sourceName, const std::string& entryPoint = "main");

}