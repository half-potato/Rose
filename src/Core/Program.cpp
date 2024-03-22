#include <iostream>

#include "Program.hpp"

namespace RoseEngine {

ref<Program> Program::Create(const Device& device, const std::filesystem::path& sourceFile, const std::string& entryPoint) {
	ref<Program> program = make_ref<Program>();
	auto shader = ShaderModule::Create(device, sourceFile, entryPoint);
	program->mPipeline = Pipeline::CreateCompute(device, shader);
	return program;
}

struct DescriptorSetWriter {
	union DescriptorInfo {
		vk::DescriptorBufferInfo buffer;
		vk::DescriptorImageInfo image;
		vk::WriteDescriptorSetAccelerationStructureKHR accelerationStructure;
	};
	std::vector<DescriptorInfo> descriptorInfos;
	std::vector<vk::WriteDescriptorSet> writes;

	PairMap<std::vector<std::byte>, uint32_t, uint32_t> uniforms;
	std::vector<std::pair<uint32_t, std::span<const std::byte, std::dynamic_extent>>> pushConstants;

	std::vector<vk::DescriptorSet> descriptorSets;

	vk::WriteDescriptorSet WriteDescriptor(const ShaderDescriptorBinding& binding, uint32_t arrayIndex) {
		return vk::WriteDescriptorSet{
			.dstSet = descriptorSets[binding.setIndex],
			.dstBinding = binding.bindingIndex,
			.dstArrayElement = arrayIndex,
			.descriptorCount = 1,
			.descriptorType = binding.descriptorType };
	}
	void WriteBuffer(const ShaderDescriptorBinding& binding, uint32_t arrayIndex, const vk::DescriptorBufferInfo& data) {
		vk::WriteDescriptorSet& w = writes.emplace_back(WriteDescriptor(binding, arrayIndex));
		DescriptorInfo& info = descriptorInfos.emplace_back(DescriptorInfo{});
		info.buffer = data;
		w.setBufferInfo(info.buffer);
	}
	void WriteImage(const ShaderDescriptorBinding& binding, uint32_t arrayIndex, const vk::DescriptorImageInfo& data) {
		vk::WriteDescriptorSet& w = writes.emplace_back(WriteDescriptor(binding, arrayIndex));
		DescriptorInfo& info = descriptorInfos.emplace_back(DescriptorInfo{});
		info.image = data;
		w.setImageInfo(info.image);
	}
	void WriteAccelerationStructure(const ShaderDescriptorBinding& binding, uint32_t arrayIndex, const vk::WriteDescriptorSetAccelerationStructureKHR& data) {
		vk::WriteDescriptorSet& w = writes.emplace_back(WriteDescriptor(binding, arrayIndex));
		DescriptorInfo& info = descriptorInfos.emplace_back(DescriptorInfo{});
		info.accelerationStructure = data;
		w.setPNext(&info);
	}

	void Write(CommandContext& context, const ShaderParameter& parameter, const ShaderParameterBinding& binding, uint32_t constantOffset = 0) {
		for (const auto&[name, param] : parameter) {
			uint32_t arrayIndex = 0;

			bool isArrayElement = std::ranges::all_of(name, [](char c){ return std::isdigit(c); });
			if (isArrayElement) {
				arrayIndex = std::stoi(name);
				if (const auto* desc = binding.get_if<ShaderDescriptorBinding>()) {
					if (arrayIndex >= desc->arraySize) {
						std::cout << "Warning array index " << arrayIndex << " which is out of bounds for array size " << desc->arraySize << std::endl;
					}
				}
			}
			const auto& paramBinding = isArrayElement ? binding : binding.at(name);

			uint32_t offset = constantOffset;

			if (param.holds_alternative<std::monostate>()) {

			} else if (const auto* v = param.get_if<ConstantParameter>()) {
				if (const auto* constantBinding = paramBinding.get_if<ShaderConstantBinding>()) {
					if (v->size() != constantBinding->typeSize)
						std::cout << "Warning: Binding constant parameter of size " << v->size() << " to binding of size " << constantBinding->typeSize << std::endl;

					offset += constantBinding->offset;

					if (constantBinding->pushConstant) {
						pushConstants.emplace_back(offset, *v);
					} else {
						auto& u = uniforms[{constantBinding->setIndex, constantBinding->bindingIndex}];
						if (offset + v->size() > u.size())
							u.resize(offset + v->size());
						std::memcpy(u.data() + offset, v->data(), v->size());
					}
				} else {
					std::cout << "Warning: Attempting to bind constant parameter to non-constant binding" << std::endl;
				}
			} else {
				if (const auto* descriptorBinding = paramBinding.get_if<ShaderDescriptorBinding>()) {
					if (const auto* v = param.get_if<BufferParameter>()) {
						const auto& buffer = *v;
						if (buffer.empty()) continue;
						WriteBuffer(*descriptorBinding, arrayIndex, vk::DescriptorBufferInfo{
							.buffer = **buffer.mBuffer,
							.offset = buffer.mOffset,
							.range  = buffer.size() });
					} else if (const auto* v = param.get_if<ImageParameter>()) {
						const auto& [image, layout, accessFlags, sampler] = *v;
						if (!image && !sampler) continue;
						WriteImage(*descriptorBinding, arrayIndex, vk::DescriptorImageInfo{
							.sampler     = sampler ? **sampler : nullptr,
							.imageView   = image   ? *image    : nullptr,
							.imageLayout = layout });
					} else if (const auto* v = param.get_if<AccelerationStructureParameter>()) {
						const auto& as = *v;
						if (!as) continue;
						WriteAccelerationStructure(*descriptorBinding, arrayIndex, vk::WriteDescriptorSetAccelerationStructureKHR{}.setAccelerationStructures(**as));
					}
				} else {
					std::cout << "Warning: Attempting to bind descriptor parameter to non-descriptor binding" << std::endl;
				}
			}

			Write(context, param, paramBinding, offset);
		}
	}
};

size_t GetDescriptorCount(const ShaderParameter& param) {
	size_t count = 1;
	for (const auto&[name, p] : param)
		count += GetDescriptorCount(p);
	return count;
}

void Program::BindParameters(CommandContext& context) {
	auto descriptorSets = GetDescriptorSets(context);

	DescriptorSetWriter w;
	for (const auto& s : *descriptorSets)
		w.descriptorSets.emplace_back(*s);
	w.descriptorInfos.reserve(GetDescriptorCount(mRootParameter));
	w.Write(context, mRootParameter, mPipeline->Layout()->RootBinding());

	// upload uniforms and write uniform buffer descriptors

	for (const auto&[setBinding, data] : w.uniforms) {
		const auto [setIndex,bindingIndex] = setBinding;

		auto buffer = context.UploadData(data);

		w.WriteBuffer(
			ShaderDescriptorBinding{
				.descriptorType = vk::DescriptorType::eUniformBuffer,
				.setIndex = setIndex,
				.bindingIndex = bindingIndex },
			0,
			vk::DescriptorBufferInfo{
				.buffer = **buffer.mBuffer,
				.offset = buffer.mOffset,
				.range  = buffer.size() });
	}

	if (!w.writes.empty())
		context.GetDevice()->updateDescriptorSets(w.writes, {});

	for (const auto&[offset, data] : w.pushConstants)
		context->pushConstants<std::byte>(***mPipeline->Layout(), vk::ShaderStageFlagBits::eCompute, offset, data);

	std::vector<vk::DescriptorSet> vkDescriptorSets;
	for (const auto& ds : *descriptorSets)
		vkDescriptorSets.emplace_back(*ds);
	context->bindDescriptorSets(vk::PipelineBindPoint::eCompute, ***mPipeline->Layout(), 0, vkDescriptorSets, {});
}

}