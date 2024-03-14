#pragma once

#include <queue>

#include "Pipeline.hpp"
#include "ShaderParameters.hpp"
#include "CommandContext.hpp"

namespace RoseEngine {

template<typename T>
struct ResourceCache  {
	std::queue<std::pair<T, uint64_t>> mResources;

	inline void push(T&& resource, uint64_t counterValue)      { mResources.push(std::make_pair(resource, counterValue)); }
	inline void push(const T& resource, uint64_t counterValue) { mResources.push(std::make_pair(resource, counterValue)); }

	inline T pop() {
		T tmp = std::move(mResources.front().first);
		mResources.pop();
		return tmp;
	}

	inline T pop_or_create(const Device& device, auto ctor) {
		T tmp = empty(device) ? ctor() : pop();
		push(tmp, device.NextTimelineCounterValue());
		return tmp;
	}

	inline bool empty(const Device& device) {
		if (mResources.empty()) return true;
		return mResources.front().second < device.TimelineSemaphore().getCounterValue();
	}
};

class Program {
private:
	ref<const Pipeline> mPipeline = {};
	ShaderParameters mGlobalParameters;

	ResourceCache<ref<DescriptorSets>> mCachedDescriptorSets;
	NameMap<ResourceCache<BufferView>> mCachedUniformBuffers;

	inline ref<DescriptorSets> GetDescriptorSets(const CommandContext& context) {
		return mCachedDescriptorSets.pop_or_create(context.GetDevice(), [&]() {
			std::vector<vk::DescriptorSetLayout> setLayouts;
			for (const auto& l : mPipeline->Layout()->GetDescriptorSetLayouts())
				setLayouts.emplace_back(**l);
			return make_ref<DescriptorSets>(std::move(context.GetDevice().AllocateDescriptorSets(setLayouts)));
		});
	}

	void WriteDescriptors(const Device& device, const DescriptorSets& descriptorSets);
	void WriteUniformBufferDescriptors(CommandContext& context, const DescriptorSets& descriptorSets);

public:
	inline ShaderParameters& GlobalParameters() { return mGlobalParameters; }
	inline const ShaderParameters& GlobalParameters() const { return mGlobalParameters; }

	inline auto& Parameter(const std::string& name) { return mGlobalParameters[name]; }
	inline const auto& Parameter(const std::string& name) const { return mGlobalParameters.at(name); }

	template<std::convertible_to<ShaderParameterValue> ... Args>
	inline void SetEntryPointArguments(Args&&... entryPointArgs) {
		const auto& shader = *mPipeline->GetShader(vk::ShaderStageFlagBits::eCompute);
		const auto& bindings = shader.Bindings();
		if (sizeof...(entryPointArgs) != bindings.entryPointArguments.size())
			throw std::logic_error("Expected " + std::to_string(bindings.entryPointArguments.size()) + " arguments, but got " + std::to_string(sizeof...(entryPointArgs)));

		size_t i = 0;
		for (const ShaderParameterValue arg : { ShaderParameterValue(entryPointArgs)... })
			mGlobalParameters[bindings.entryPointArguments[i++]] = arg;
	}

	void Dispatch(CommandContext& context, const vk::Extent3D threadCount);

	template<std::convertible_to<ShaderParameterValue> ... Args>
	inline void Dispatch(CommandContext& context, uint32_t threadCount, Args&&... entryPointArgs) {
		SetEntryPointArguments(std::forward<Args>(entryPointArgs)...);
		Dispatch(context, vk::Extent3D{ threadCount, 1, 1 });
	}

	template<std::convertible_to<ShaderParameterValue> ... Args>
	inline void Dispatch(CommandContext& context, const vk::Extent2D threadCount, Args&&... entryPointArgs) {
		SetEntryPointArguments(std::forward<Args>(entryPointArgs)...);
		Dispatch(context, vk::Extent3D{ threadCount.width, threadCount.height, 1 });
	}

	template<std::convertible_to<ShaderParameterValue> ... Args>
	inline void Dispatch(CommandContext& context, const vk::Extent3D threadCount, Args&&... entryPointArgs) {
		SetEntryPointArguments(std::forward<Args>(entryPointArgs)...);
		Dispatch(context, threadCount);
	}

	static ref<Program> Create(const Device& device, const std::filesystem::path& sourceFile, const std::string& entryPoint = "main");
};



}