#include "CommandContext.hpp"
#include <iostream>

namespace RoseEngine {

void CommandContext::Begin() {
	if (!*mCommandPool) {
		mCommandPool = (*mDevice)->createCommandPool(vk::CommandPoolCreateInfo{
			.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
			.queueFamilyIndex = mQueueFamily });
	}

	if (!*mCommandBuffer) {
		auto commandBuffers = (*mDevice)->allocateCommandBuffers(vk::CommandBufferAllocateInfo{
			.commandPool = *mCommandPool,
			.level = vk::CommandBufferLevel::ePrimary,
			.commandBufferCount = 1 });
		mCommandBuffer = std::move(commandBuffers[0]);
	}

	if (mLastSubmit > 0)
		mDevice->Wait(mLastSubmit);

	mCommandBuffer.reset();
	mCommandBuffer.begin(vk::CommandBufferBeginInfo{});

	if (!mCache.mNewUploadBuffers.empty()) {
		for (auto& [usage, bufs] : mCache.mNewUploadBuffers) {
			for (auto& b : bufs)
				if (b.second.mBuffer && b.second.mBuffer.use_count() == 1)
					mCache.mUploadBuffers[usage].emplace_back(std::move(b));
		}
		mCache.mNewUploadBuffers.clear();
		for (auto& [usage, bufs] : mCache.mUploadBuffers)
			std::ranges::sort(bufs, [](const auto& lhs, const auto& rhs) { return lhs.first.size() < rhs.first.size(); });
	}

	if (!mCache.mNewDescriptorSets.empty()) {
		for (auto&[layout, sets] : mCache.mNewDescriptorSets)
			for (auto& s : sets)
				mCache.mDescriptorSets[layout].emplace_back(std::move(s));
		mCache.mNewDescriptorSets.clear();
	}
}

uint64_t CommandContext::Submit(
	const uint32_t queueIndex,
	const vk::ArrayProxy<const vk::Semaphore>&          signalSemaphores,
	const vk::ArrayProxy<const uint64_t>&               signalValues,
	const vk::ArrayProxy<const vk::Semaphore>&          waitSemaphores,
	const vk::ArrayProxy<const vk::PipelineStageFlags>& waitStages,
	const vk::ArrayProxy<const uint64_t>&               waitValues) {

	mCommandBuffer.end();

	vk::StructureChain<vk::SubmitInfo, vk::TimelineSemaphoreSubmitInfo> submitInfoChain = {};
	auto& submitInfo = submitInfoChain.get<vk::SubmitInfo>();
	auto& timelineSubmitInfo = submitInfoChain.get<vk::TimelineSemaphoreSubmitInfo>();

	submitInfo.setCommandBuffers(*mCommandBuffer);

	uint64_t signalValue = mDevice->IncrementTimelineSignal();

	std::vector<vk::Semaphore> semaphores(signalSemaphores.size() + 1);
	std::vector<uint64_t> values(signalValues.size() + 1);
	std::ranges::copy(signalSemaphores, semaphores.begin());
	std::ranges::copy(signalValues, values.begin());
	semaphores.back() = *mDevice->TimelineSemaphore();
	values.back() = signalValue;

	submitInfo.setSignalSemaphores(semaphores);
	timelineSubmitInfo.setSignalSemaphoreValues(values);

	submitInfo.setWaitSemaphores(waitSemaphores);
	submitInfo.setWaitDstStageMask(waitStages);
	timelineSubmitInfo.setWaitSemaphoreValues(waitValues);

	(*mDevice)->getQueue(mQueueFamily, queueIndex).submit( submitInfo );

	mLastSubmit = signalValue;

	return signalValue;
}

void CommandContext::AllocateDescriptorPool() {
	std::vector<vk::DescriptorPoolSize> poolSizes {
		vk::DescriptorPoolSize{ vk::DescriptorType::eSampler,              std::min(16384u, mDevice->Limits().maxDescriptorSetSamplers) },
		vk::DescriptorPoolSize{ vk::DescriptorType::eCombinedImageSampler, std::min(16384u, mDevice->Limits().maxDescriptorSetSampledImages) },
		vk::DescriptorPoolSize{ vk::DescriptorType::eInputAttachment,      std::min(16384u, mDevice->Limits().maxDescriptorSetInputAttachments) },
		vk::DescriptorPoolSize{ vk::DescriptorType::eSampledImage,         std::min(16384u, mDevice->Limits().maxDescriptorSetSampledImages) },
		vk::DescriptorPoolSize{ vk::DescriptorType::eStorageImage,         std::min(16384u, mDevice->Limits().maxDescriptorSetStorageImages) },
		vk::DescriptorPoolSize{ vk::DescriptorType::eUniformBuffer,        std::min(16384u, mDevice->Limits().maxDescriptorSetUniformBuffers) },
		vk::DescriptorPoolSize{ vk::DescriptorType::eUniformBufferDynamic, std::min(16384u, mDevice->Limits().maxDescriptorSetUniformBuffersDynamic) },
		vk::DescriptorPoolSize{ vk::DescriptorType::eStorageBuffer,        std::min(16384u, mDevice->Limits().maxDescriptorSetStorageBuffers) },
		vk::DescriptorPoolSize{ vk::DescriptorType::eStorageBufferDynamic, std::min(16384u, mDevice->Limits().maxDescriptorSetStorageBuffersDynamic) }
	};
	mCachedDescriptorPools.push_front((*mDevice)->createDescriptorPool(
		vk::DescriptorPoolCreateInfo{
			.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
			.maxSets = 8192 }
		.setPoolSizes(poolSizes)));
}

DescriptorSets CommandContext::AllocateDescriptorSets(const vk::ArrayProxy<const vk::DescriptorSetLayout>& layouts) {
	if (mCachedDescriptorPools.empty())
		AllocateDescriptorPool();

	std::vector<vk::raii::DescriptorSet> sets;
	try {
		sets = (*mDevice)->allocateDescriptorSets(vk::DescriptorSetAllocateInfo{ .descriptorPool = *mCachedDescriptorPools.front() }.setSetLayouts(layouts));
	} catch(vk::OutOfPoolMemoryError e) {
		AllocateDescriptorPool();
		sets = (*mDevice)->allocateDescriptorSets(vk::DescriptorSetAllocateInfo{ .descriptorPool = *mCachedDescriptorPools.front() }.setSetLayouts(layouts));
	}

	return sets;
}

ref<DescriptorSets> CommandContext::GetDescriptorSets(const PipelineLayout& pipelineLayout) {
	if (pipelineLayout.GetDescriptorSetLayouts().empty())
		return nullptr;

	ref<DescriptorSets> descriptorSets = nullptr;

	auto it = mCache.mDescriptorSets.find(**pipelineLayout);
	if (it != mCache.mDescriptorSets.end() && it->second.size() > 0) {
		descriptorSets = it->second.back();
		it->second.pop_back();
	}

	if (!descriptorSets) {
		std::vector<vk::DescriptorSetLayout> setLayouts;
		for (const auto& l : pipelineLayout.GetDescriptorSetLayouts())
			setLayouts.emplace_back(**l);
		descriptorSets = make_ref<DescriptorSets>(std::move(AllocateDescriptorSets(setLayouts)));
	}

	mCache.mNewDescriptorSets[**pipelineLayout].emplace_back(descriptorSets);

	return descriptorSets;
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
					if (v->size() > constantBinding->typeSize)
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
				} else if (const auto* descriptorBinding = paramBinding.get_if<ShaderDescriptorBinding>()) {
					if (descriptorBinding->descriptorType == vk::DescriptorType::eUniformBuffer || descriptorBinding->descriptorType == vk::DescriptorType::eStorageBuffer) {

						auto buffer = context.UploadData(*v, descriptorBinding->descriptorType == vk::DescriptorType::eUniformBuffer ? vk::BufferUsageFlagBits::eUniformBuffer : vk::BufferUsageFlagBits::eStorageBuffer);

						WriteBuffer(*descriptorBinding, arrayIndex, vk::DescriptorBufferInfo{
							.buffer = **buffer.mBuffer,
							.offset = buffer.mOffset,
							.range  = buffer.size() });
					} else
						std::cout << "Warning: Attempting to bind constant parameter to non-constant binding" << std::endl;
				} else
					std::cout << "Warning: Attempting to bind constant parameter to non-constant binding" << std::endl;
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

size_t GetDescriptorCount(const ShaderParameterBinding& param) {
	size_t count = param.holds_alternative<ShaderDescriptorBinding>() ? 1 : 0;
	if (const auto* b = param.get_if<ShaderConstantBinding>())
		if (!b->pushConstant)
			count = 1;
	for (const auto&[name, p] : param)
		count += GetDescriptorCount(p);
	return count;
}

void CommandContext::UpdateDescriptorSets(const DescriptorSets& descriptorSets, const ShaderParameter& rootParameter, const PipelineLayout& pipelineLayout) {
	if (pipelineLayout.GetDescriptorSetLayouts().empty())
		return;

	DescriptorSetWriter w;
	for (const auto& s : descriptorSets)
		w.descriptorSets.emplace_back(*s);
	w.descriptorInfos.reserve(GetDescriptorCount(pipelineLayout.RootBinding()));
	w.Write(*this, rootParameter, pipelineLayout.RootBinding());

	// upload uniforms and write uniform buffer descriptors

	for (const auto&[setBinding, data] : w.uniforms) {
		const auto [setIndex,bindingIndex] = setBinding;

		auto buffer = UploadData(data);

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
		(*mDevice)->updateDescriptorSets(w.writes, {});
}

void PushConstants(const CommandContext& context, const PipelineLayout& pipelineLayout, const ShaderParameter& parameter, const ShaderParameterBinding& binding, uint32_t constantOffset = 0) {
	for (const auto&[name, param] : parameter) {
		uint32_t arrayIndex = 0;

		const auto& paramBinding = binding.at(name);

		uint32_t offset = constantOffset;

		if (const auto* v = param.get_if<ConstantParameter>()) {
			if (const auto* constantBinding = paramBinding.get_if<ShaderConstantBinding>()) {
				if (!constantBinding->pushConstant)
					continue;
				if (v->size() > constantBinding->typeSize)
					std::cout << "Warning: Binding constant parameter of size " << v->size() << " to binding of size " << constantBinding->typeSize << std::endl;

				offset += constantBinding->offset;

				if (constantBinding->pushConstant)
					context->pushConstants<std::byte>(**pipelineLayout, pipelineLayout.StageMask(), offset, *v);
			}
		}

		PushConstants(context, pipelineLayout, param, paramBinding, offset);
	}
}

void CommandContext::PushConstants(const ShaderParameter& rootParameter, const PipelineLayout& pipelineLayout) const {
	RoseEngine::PushConstants(*this, pipelineLayout, rootParameter, pipelineLayout.RootBinding());
}

void CommandContext::BindParameters(const ShaderParameter& rootParameter, const PipelineLayout& pipelineLayout, ref<DescriptorSets> descriptorSets) {
	if (!descriptorSets) {
		descriptorSets = GetDescriptorSets(pipelineLayout);
		UpdateDescriptorSets(*descriptorSets, rootParameter, pipelineLayout);
	}

	if (descriptorSets) {
		std::vector<vk::DescriptorSet> vkDescriptorSets;
		for (const auto& ds : *descriptorSets)
			vkDescriptorSets.emplace_back(*ds);
		mCommandBuffer.bindDescriptorSets(pipelineLayout.StageMask() & vk::ShaderStageFlagBits::eCompute ? vk::PipelineBindPoint::eCompute : vk::PipelineBindPoint::eGraphics, **pipelineLayout, 0, vkDescriptorSets, {});
	}
}

}