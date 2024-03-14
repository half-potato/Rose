#include <iostream>

#include "Program.hpp"

namespace RoseEngine {

ref<Program> Program::Create(const Device& device, const std::filesystem::path& sourceFile, const std::string& entryPoint) {
	ref<Program> program = make_ref<Program>();
	auto shader = ShaderModule::Create(device, sourceFile, entryPoint);
	program->mPipeline = Pipeline::CreateCompute(device, shader);
	return program;
}

void Program::WriteUniformBufferDescriptors(CommandContext& context, const DescriptorSets& descriptorSets) {
	const auto& bindings = mPipeline->Layout()->Bindings();
	if (bindings.uniforms.empty()) return;

	// copy constants from mGlobalParameters to contiguous buffers

	NameMap<std::vector<std::byte>> uniformBufferData;
	for (const auto&[name, size] : bindings.uniformBufferSizes)
		uniformBufferData[name].resize(size);

	for (const auto& [name, binding] : bindings.uniforms) {
		auto it = mGlobalParameters.find(name);
		if (it == mGlobalParameters.end()) {
			std::cout << "Warning: unspecified uniform: " << name << std::endl;
			continue;
		}

		size_t offset = binding.offset;
		for (uint32_t i = 0; i < it->second.size(); i++) {
			if (const auto* param = std::get_if<ConstantParameter>(&it->second[i])) {
				std::byte* dst = uniformBufferData.at(binding.parentDescriptor).data() + offset;
				std::ranges::copy(*param, dst);
				offset += param->size();
			} else {
				std::cout << "Warning: invalid parameter type: " << name << std::endl;
			}
		}
		if (binding.typeSize != offset)
			std::cout << "Warning: invalid uniform size: " << name << std::endl;
	}

	// upload uniforms and write uniform buffer descriptors

	std::vector<vk::DescriptorBufferInfo> descriptorInfos;
	std::vector<vk::WriteDescriptorSet> writes;
	descriptorInfos.reserve(uniformBufferData.size());
	writes.reserve(uniformBufferData.size());

	for (const auto&[name, data] : uniformBufferData) {
		auto hostBuffer = mCachedUniformBuffers[name + "/Host"].pop_or_create(context.GetDevice(), [&](){ return CreateBuffer(
			context.GetDevice(),
			data); });

		auto buffer = mCachedUniformBuffers[name].pop_or_create(context.GetDevice(), [&](){ return CreateBuffer(
			context.GetDevice(),
			data.size(),
			vk::BufferUsageFlagBits::eUniformBuffer|vk::BufferUsageFlagBits::eTransferDst,
			vk::MemoryPropertyFlagBits::eDeviceLocal,
			VMA_ALLOCATION_CREATE_STRATEGY_MIN_TIME_BIT); });

		context.Copy(hostBuffer, buffer);

		vk::DescriptorBufferInfo& info = descriptorInfos.emplace_back(vk::DescriptorBufferInfo{
			.buffer = **buffer.mBuffer,
			.offset = buffer.mOffset,
			.range  = buffer.size() });

		const auto& binding = bindings.descriptors.at(name);
		vk::WriteDescriptorSet& w = writes.emplace_back(vk::WriteDescriptorSet{
			.dstSet = *descriptorSets[binding.setIndex],
			.dstBinding = binding.bindingIndex,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = vk::DescriptorType::eUniformBuffer });

		w.setBufferInfo(info);
	}

	if (!writes.empty()) context.GetDevice()->updateDescriptorSets(writes, {});
}

void Program::WriteDescriptors(const Device& device, const DescriptorSets& descriptorSets) {
	union DescriptorInfo {
		vk::DescriptorBufferInfo buffer;
		vk::DescriptorImageInfo image;
		vk::WriteDescriptorSetAccelerationStructureKHR accelerationStructure;
	};
	std::vector<DescriptorInfo> descriptorInfos;
	std::vector<vk::WriteDescriptorSet> writes;
	descriptorInfos.reserve(mGlobalParameters.size());
	writes.reserve(mGlobalParameters.size());

	const auto& bindings = mPipeline->Layout()->Bindings();

	for (const auto& [name, binding] : bindings.descriptors) {
		auto it = mGlobalParameters.find(name);
		if (it == mGlobalParameters.end()) {
			std::cout << "Warning: unspecified descriptor " << name << std::endl;
			continue;
		}
		const auto& paramArray = it->second;

		size_t arraySize = 1;
		for (auto s : binding.arraySize) arraySize *= s;
		if (paramArray.size() > arraySize) {
			std::cout << "Warning: binding array of size " << paramArray.size() << " to descriptor array of size " << arraySize << std::endl;
		}

		for (uint32_t arrayIndex = 0; arrayIndex < std::min(arraySize, paramArray.size()); arrayIndex++) {
			const auto& param = paramArray[arrayIndex];

			// write descriptor

			vk::WriteDescriptorSet& w = writes.emplace_back(vk::WriteDescriptorSet{
				.dstSet = *descriptorSets[binding.setIndex],
				.dstBinding = binding.bindingIndex,
				.dstArrayElement = arrayIndex,
				.descriptorCount = 1,
				.descriptorType = binding.descriptorType
			});
			DescriptorInfo& info = descriptorInfos.emplace_back(DescriptorInfo{});

			if (const auto* v = std::get_if<BufferParameter>(&param)) {
				const auto& buffer = *v;
				if (buffer.empty()) continue;
				info.buffer = vk::DescriptorBufferInfo{
					.buffer = **buffer.mBuffer,
					.offset = buffer.mOffset,
					.range  = buffer.size() };
				w.setBufferInfo(info.buffer);
			} else if (const auto* v = std::get_if<ImageParameter>(&param)) {
				const auto& [image, layout, accessFlags, sampler] = *v;
				if (!image && !sampler) continue;
				info.image = vk::DescriptorImageInfo{
					.sampler     = sampler ? **sampler : nullptr,
					.imageView   = image ? *image : nullptr,
					.imageLayout = layout };
				w.setImageInfo(info.image);
			} else if (const auto* v = std::get_if<AccelerationStructureParameter>(&param)) {
				const auto& as = *v;
				if (!as) continue;
				info.accelerationStructure = vk::WriteDescriptorSetAccelerationStructureKHR{}
					.setAccelerationStructures(**as);
				w.pNext = &info;
			}
		}
	}

	if (!writes.empty()) device->updateDescriptorSets(writes, {});
}

void Program::Dispatch(CommandContext& context, const vk::Extent3D threadCount) {
	mGlobalParameters.AddParameters(mGlobalParameters);

	auto descriptorSets = GetDescriptorSets(context);

	WriteDescriptors(context.GetDevice(), *descriptorSets);
	WriteUniformBufferDescriptors(context, *descriptorSets);

	context.ExecuteBarriers();

	context->bindPipeline(vk::PipelineBindPoint::eCompute, ***mPipeline);

	std::vector<vk::DescriptorSet> vkDescriptorSets;
	for (const auto& ds : *descriptorSets)
		vkDescriptorSets.emplace_back(*ds);
	context->bindDescriptorSets(vk::PipelineBindPoint::eCompute, ***mPipeline->Layout(), 0, vkDescriptorSets, {});

	for (const auto& [name, binding] : mPipeline->Layout()->Bindings().pushConstants) {
		auto it = mGlobalParameters.find(name);
		if (it == mGlobalParameters.end()) {
			std::cout << "Warning: unspecified push constant: " << name << std::endl;
			continue;
		}

		size_t offset = binding.offset;
		for (uint32_t i = 0; i < it->second.size(); i++) {
			if (const auto* param = std::get_if<ConstantParameter>(&it->second[i])) {
				context->pushConstants<std::byte>(***mPipeline->Layout(), vk::ShaderStageFlagBits::eCompute, offset, *param);
				offset += param->size();
			} else {
				std::cout << "Warning: invalid parameter type: " << name << std::endl;
			}
		}
		if (binding.typeSize != offset)
			std::cout << "Warning: invalid push constant size: " << name << std::endl;
	}

	auto dim = GetDispatchDim(mPipeline->GetShader(vk::ShaderStageFlagBits::eCompute)->mWorkgroupSize, threadCount);
	context->dispatch(dim.width, dim.height, dim.depth);
}

}