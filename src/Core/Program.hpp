#pragma once

#include <queue>

#include "Pipeline.hpp"
#include "TransientResourceCache.hpp"
#include "Buffer.hpp"
#include "Image.hpp"
#include "CommandContext.hpp"

namespace RoseEngine {

using BufferParameter = BufferView;
struct ImageParameter {
	ImageView              image;
	vk::ImageLayout        imageLayout;
	vk::AccessFlags        imageAccess;
	ref<vk::raii::Sampler> sampler;
};
using AccelerationStructureParameter = ref<vk::raii::AccelerationStructureKHR>;

// represents a uniform or push constant
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

using ShaderParameter = ParameterMap<
	std::monostate,
	ConstantParameter,
	BufferParameter,
	ImageParameter,
	AccelerationStructureParameter >;

template<typename T>
concept shader_parameter = one_of<T, ShaderParameter, ConstantParameter, BufferParameter, ImageParameter, AccelerationStructureParameter>;

class Program {
private:
	ref<const Pipeline> mPipeline = {};
	ShaderParameter mRootParameter;

	TransientResourceCache<ref<DescriptorSets>> mCachedDescriptorSets;
	NameMap<TransientResourceCache<BufferView>> mCachedUniformBuffers;

	inline ref<DescriptorSets> GetDescriptorSets(const CommandContext& context) {
		return mCachedDescriptorSets.pop_or_create(context.GetDevice(), [&]() {
			std::vector<vk::DescriptorSetLayout> setLayouts;
			for (const auto& l : mPipeline->Layout()->GetDescriptorSetLayouts())
				setLayouts.emplace_back(**l);
			return make_ref<DescriptorSets>(std::move(context.GetDevice().AllocateDescriptorSets(setLayouts)));
		});
	}

public:
	static ref<Program> Create(const Device& device, const std::filesystem::path& sourceFile, const std::string& entryPoint = "main");

	inline ShaderParameter&       RootParameter() { return mRootParameter; }
	inline const ShaderParameter& RootParameter() const { return mRootParameter; }

	template<std::convertible_to<ShaderParameter> ... Args>
	inline void SetEntryPointParameters(Args&&... entryPointArgs) {
		const auto& argBindings = mPipeline->GetShader(vk::ShaderStageFlagBits::eCompute)->EntryPointArguments();
		if (sizeof...(entryPointArgs) != argBindings.size())
			throw std::logic_error("Expected " + std::to_string(argBindings.size()) + " arguments, but got " + std::to_string(sizeof...(entryPointArgs)));

		size_t i = 0;
		for (const ShaderParameter arg : { ShaderParameter(entryPointArgs)... })
			mRootParameter[argBindings[i++]] = arg;
	}

	void BindParameters(CommandContext& context);

	void Dispatch(CommandContext& context, const vk::Extent3D threadCount) {
		context->bindPipeline(vk::PipelineBindPoint::eCompute, ***mPipeline);

		BindParameters(context);

		context.ExecuteBarriers();

		auto dim = GetDispatchDim(mPipeline->GetShader(vk::ShaderStageFlagBits::eCompute)->mWorkgroupSize, threadCount);
		context->dispatch(dim.width, dim.height, dim.depth);
	}

	template<std::convertible_to<ShaderParameter> ... Args>
	inline void Dispatch(CommandContext& context, uint32_t threadCount, Args&&... entryPointArgs) {
		if constexpr(sizeof...(entryPointArgs) > 0)
			SetEntryPointParameters(std::forward<Args>(entryPointArgs)...);
		Dispatch(context, vk::Extent3D{ threadCount, 1, 1 });
	}

	template<std::convertible_to<ShaderParameter> ... Args>
	inline void Dispatch(CommandContext& context, const vk::Extent2D threadCount, Args&&... entryPointArgs) {
		if constexpr(sizeof...(entryPointArgs) > 0)
			SetEntryPointParameters(std::forward<Args>(entryPointArgs)...);
		Dispatch(context, vk::Extent3D{ threadCount.width, threadCount.height, 1 });
	}

	template<std::convertible_to<ShaderParameter> ... Args>
	inline void Dispatch(CommandContext& context, const vk::Extent3D threadCount, Args&&... entryPointArgs) {
		if constexpr(sizeof...(entryPointArgs) > 0)
			SetEntryPointParameters(std::forward<Args>(entryPointArgs)...);
		Dispatch(context, threadCount);
	}


	inline ShaderParameter& operator[](const std::string& name) { return mRootParameter[name]; }
	template<typename... Args> inline void operator()(CommandContext& context, Args&&...args) { Dispatch(context, std::forward<Args>(args)...); }
};

}