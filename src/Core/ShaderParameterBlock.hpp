#pragma once

#include <variant>
#include <iostream>

#include "Buffer.hpp"
#include "Image.hpp"
#include "Pipeline.hpp"

namespace RoseEngine {

// represents a loose constant (eg a float or vector)
class ConstantParameter : public std::vector<std::byte> {
public:
	template<typename T>
	inline ConstantParameter(const T& value) {
		resize(sizeof(value));
		*reinterpret_cast<T*>(data()) = value;
	}

	ConstantParameter() = default;
	ConstantParameter(ConstantParameter&&) = default;
	ConstantParameter(const ConstantParameter&) = default;
	ConstantParameter& operator=(ConstantParameter&&) = default;
	ConstantParameter& operator=(const ConstantParameter&) = default;

	template<typename T>
	inline T& get() {
		if (empty())
			resize(sizeof(T));
		return *reinterpret_cast<T*>(data());
	}

	template<typename T>
	inline const T& get() const {
		return *reinterpret_cast<const T*>(data());
	}


	template<typename T>
	inline T& operator=(const T& value) {
		resize(sizeof(value));
		return *reinterpret_cast<T*>(data()) = value;
	}
};

using BufferParameter = BufferView;
struct ImageParameter {
	ImageView mImage;
	vk::ImageLayout mImageLayout;
	vk::AccessFlags mImageAccessFlags;
	std::shared_ptr<vk::raii::Sampler> mSampler;
};
using AccelerationStructureParameter = std::shared_ptr<vk::raii::AccelerationStructureKHR>;

using ShaderParameterValue = std::variant<
	ConstantParameter,
	BufferParameter,
	ImageParameter,
	AccelerationStructureParameter >;

struct ShaderParameterBlock {
	std::shared_ptr<PipelineLayout> mLayout;
	PairMap<ShaderParameterValue, std::string, uint32_t> mParameters;
	std::vector<std::shared_ptr<vk::raii::DescriptorSet>> mDescriptorSets;

	inline auto begin() const  { return mParameters.begin(); }
	inline auto end() const    { return mParameters.end(); }
	inline size_t size() const { return mParameters.size(); }

	inline bool                        Contains  (const std::string& id, const uint32_t arrayIndex = 0) const { return mParameters.contains(std::make_pair(id, arrayIndex)); }
	inline const ShaderParameterValue& operator()(const std::string& id, const uint32_t arrayIndex = 0) const { return mParameters.at(std::make_pair(id, arrayIndex)); }
	inline       ShaderParameterValue& operator()(const std::string& id, const uint32_t arrayIndex = 0)       { return mParameters[std::make_pair(id, arrayIndex)]; }
	inline const ShaderParameterValue& operator()(const std::pair<std::string, uint32_t>& key) const { return mParameters.at(key); }
	inline       ShaderParameterValue& operator()(const std::pair<std::string, uint32_t>& key)       { return mParameters[key]; }

	inline ShaderParameterBlock& SetParameters(const ShaderParameterBlock& params) {
		for (const auto&[key, val] : params)
			operator()(key) = val;
		return *this;
	}
	inline ShaderParameterBlock& SetParameters(const std::string& id, const ShaderParameterBlock& params) {
		for (const auto&[key, val] : params)
			operator()(id + "." + key.first, key.second) = val;
		return *this;
	}

	void AllocateDescriptorSets(Device& device) {
		vk::raii::DescriptorSets sets = nullptr;
		std::vector<vk::DescriptorSetLayout> layouts;
		for (const auto& l : mLayout->mDescriptorSetLayouts)
			layouts.emplace_back(**l);

		try {
			const std::shared_ptr<vk::raii::DescriptorPool>& descriptorPool = device.GetDescriptorPool();
			sets = vk::raii::DescriptorSets(*device, vk::DescriptorSetAllocateInfo(**descriptorPool, layouts));
		} catch(vk::OutOfPoolMemoryError e) {
			const std::shared_ptr<vk::raii::DescriptorPool>& descriptorPool = device.AllocateDescriptorPool();
			sets = vk::raii::DescriptorSets(*device, vk::DescriptorSetAllocateInfo(**descriptorPool, layouts));
		}

		mDescriptorSets.resize(sets.size());
		for (uint32_t i = 0; i < sets.size(); i++)
			mDescriptorSets[i] = std::make_shared<vk::raii::DescriptorSet>(std::move(sets[i]));
	}

	inline void WriteDescriptors(const Device& device) {
		union DescriptorInfo {
			vk::DescriptorBufferInfo buffer;
			vk::DescriptorImageInfo image;
			vk::WriteDescriptorSetAccelerationStructureKHR accelerationStructure;
		};

		std::vector<DescriptorInfo> descriptorInfos;
		std::vector<vk::WriteDescriptorSet> writes;
		descriptorInfos.reserve(mParameters.size());
		writes.reserve(mParameters.size());

		std::unordered_map<std::string, std::vector<std::byte>> uniformData;
		for (const auto&[name,size] : mLayout->mUniformBufferSizes)
			uniformData[name].resize(size);

		for (const auto& [id_index, param] : mParameters) {
			const auto& [name, arrayIndex] = id_index;

			// check if param is a constant/uniform

			if (const auto* v = std::get_if<ConstantParameter>(&param)) {
				if (auto it = mLayout->mUniforms.find(name); it != mLayout->mUniforms.end()) {
					if (it->second.mTypeSize != v->size())
						std::cout << "Warning: Writing type size mismatch at " << name << "[" << arrayIndex << "]" << std::endl;

					auto& u = uniformData.at(it->second.mParentDescriptor);
					std::memcpy(u.data() + it->second.mOffset, v->data(), std::min<size_t>(v->size(), it->second.mTypeSize));
				}
				continue;
			}

			// check if param is a descriptor

			auto it = mLayout->mDescriptors.find(name);
			if (it == mLayout->mDescriptors.end())
				continue;

			const ShaderDescriptorBinding& binding = it->second;

			// write descriptor

			vk::WriteDescriptorSet& w = writes.emplace_back(vk::WriteDescriptorSet(**mDescriptorSets[binding.mSet], binding.mBinding, arrayIndex, 1, binding.mDescriptorType));
			DescriptorInfo& info = descriptorInfos.emplace_back(DescriptorInfo{});

			if (const auto* v = std::get_if<BufferParameter>(&param)) {
				const auto& buffer = *v;
				if (!buffer) continue;
				info.buffer = vk::DescriptorBufferInfo(**buffer.GetBuffer(), buffer.Offset(), buffer.SizeBytes());
				w.setBufferInfo(info.buffer);
			} else if (const auto* v = std::get_if<ImageParameter>(&param)) {
				const auto& [image, layout, accessFlags, sampler] = *v;
				if (!image && !sampler) continue;
				info.image = vk::DescriptorImageInfo(sampler ? **sampler : nullptr, image ? *image : nullptr, layout);
				w.setImageInfo(info.image);
			} else if (const auto* v = std::get_if<AccelerationStructureParameter>(&param)) {
				if (!*v) continue;
				info.accelerationStructure = vk::WriteDescriptorSetAccelerationStructureKHR(***v);
				w.descriptorCount = info.accelerationStructure.accelerationStructureCount;
				w.pNext = &info;
			}
		}

		// uniform buffers
		if (!uniformData.empty()) {
			for (const auto&[name,data] : uniformData) {
				auto hostBuf = CreateBuffer(device, data);
				auto buf = std::make_shared<Buffer>(device,
					vk::BufferUsageFlagBits::eUniformBuffer|vk::BufferUsageFlagBits::eTransferDst,
					vk::MemoryPropertyFlagBits::eDeviceLocal,
					VMA_ALLOCATION_CREATE_STRATEGY_MIN_TIME_BIT);

				commandBuffer.Copy(hostBuf, buf);

				buf.SetState(vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite);

				commandBuffer.Barrier(buf,
					pipeline.GetShader(vk::ShaderStageFlagBits::eCompute) ? vk::PipelineStageFlagBits::eComputeShader : vk::PipelineStageFlagBits::eVertexShader,
					vk::AccessFlagBits::eUniformRead);

				vk::WriteDescriptorSet& w = writes.emplace_back(vk::WriteDescriptorSet(**mDescriptorSets[pipeline.GetDescriptors().at(name).mSet], 0, 0, 1, vk::DescriptorType::eUniformBuffer));
				DescriptorInfo& info = descriptorInfos.emplace_back(DescriptorInfo{});
				info.buffer = vk::DescriptorBufferInfo(**buf.GetBuffer(), buf.Offset(), buf.SizeBytes());
				w.setBufferInfo(info.buffer);
			}
		}

		if (!writes.empty()) {
			device->updateDescriptorSets(writes, {});
		}
	}
};

}