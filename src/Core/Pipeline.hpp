#pragma once

#include <optional>

#include "ShaderModule.hpp"

namespace RoseEngine {

struct PipelineLayoutInfo {
	vk::PipelineLayoutCreateFlags                flags = {};
	vk::DescriptorSetLayoutCreateFlags           descriptorSetLayoutFlags = {};
	NameMap<vk::DescriptorBindingFlags>          descriptorBindingFlags = {};
	NameMap<std::vector<ref<vk::raii::Sampler>>> immutableSamplers = {};
};

using DescriptorSetLayouts = std::vector<ref<vk::raii::DescriptorSetLayout>>;

class PipelineLayout {
private:
	vk::raii::PipelineLayout mLayout = nullptr;
	vk::ShaderStageFlags     mStageMask = (vk::ShaderStageFlagBits)0;
	vk::PipelineStageFlags2  mPipelineStageMask = (vk::PipelineStageFlags2)0;
	PipelineLayoutInfo       mInfo = {};
	ShaderParameterBinding   mRootBinding = {};
	DescriptorSetLayouts     mDescriptorSetLayouts = {};

public:
	static ref<PipelineLayout> Create(const Device& device, const vk::ArrayProxy<const ref<const ShaderModule>>& shaders, const PipelineLayoutInfo& info = {}, const DescriptorSetLayouts& descriptorSetLayouts = {});

	inline       vk::raii::PipelineLayout& operator*()        { return mLayout; }
	inline const vk::raii::PipelineLayout& operator*() const  { return mLayout; }
	inline       vk::raii::PipelineLayout* operator->()       { return &mLayout; }
	inline const vk::raii::PipelineLayout* operator->() const { return &mLayout; }

	inline const ShaderParameterBinding& RootBinding() const { return mRootBinding; }
	inline const DescriptorSetLayouts&   GetDescriptorSetLayouts() const { return mDescriptorSetLayouts; }
	inline       vk::ShaderStageFlags    ShaderStageMask() const { return mStageMask; }
	inline       vk::PipelineStageFlags2 PipelineStageMask() const { return mPipelineStageMask; }
};


struct ComputePipelineInfo {
	vk::PipelineCreateFlags            flags = {};
	vk::PipelineShaderStageCreateFlags stageFlags = {};
};

struct VertexInputDescription {
	std::vector<vk::VertexInputBindingDescription>   bindings;
	std::vector<vk::VertexInputAttributeDescription> attributes;
};
struct ColorBlendState {
	vk::PipelineColorBlendStateCreateFlags             flags = {};
	bool                                               logicOpEnable = false;
	vk::LogicOp                                        logicOp = vk::LogicOp::eClear;
	std::vector<vk::PipelineColorBlendAttachmentState> attachments;
	std::array<float,4>                                blendConstants = { 1, 1, 1, 1 };
};
struct DynamicRenderingState {
	uint32_t                viewMask = 0;
	std::vector<vk::Format> colorFormats = {};
	vk::Format              depthFormat = vk::Format::eUndefined;
	vk::Format              stencilFormat = vk::Format::eUndefined;
};
struct GraphicsPipelineInfo {
	vk::PipelineCreateFlags                                 flags      = {};
	vk::PipelineShaderStageCreateFlags                      stageFlags = {};
	std::optional<VertexInputDescription>                   vertexInputState   = std::nullopt;
	std::optional<vk::PipelineInputAssemblyStateCreateInfo> inputAssemblyState = std::nullopt;
	std::optional<vk::PipelineTessellationStateCreateInfo>  tessellationState  = std::nullopt;
	std::optional<vk::PipelineRasterizationStateCreateInfo> rasterizationState = std::nullopt;
	std::optional<vk::PipelineMultisampleStateCreateInfo>   multisampleState   = std::nullopt;
	std::optional<vk::PipelineDepthStencilStateCreateInfo>  depthStencilState  = std::nullopt;
	std::vector<vk::Viewport>                               viewports = {};
	std::vector<vk::Rect2D>                                 scissors = {};
	std::optional<ColorBlendState>                          colorBlendState = {};
	std::vector<vk::DynamicState>                           dynamicStates = {};
	std::optional<DynamicRenderingState>                    dynamicRenderingState = {};
	vk::RenderPass                                          renderPass = {};
	uint32_t                                                subpassIndex = 0;
};

class Pipeline {
private:
	vk::raii::Pipeline        mPipeline = nullptr;
	ref<const PipelineLayout> mLayout = {};
	std::vector<ref<const ShaderModule>> mShaders = {};

public:
	static ref<Pipeline> CreateCompute(const Device& device, const ref<const ShaderModule>& shader, const ComputePipelineInfo& info = {}, const PipelineLayoutInfo& layoutInfo = {}, const DescriptorSetLayouts& descriptorSetLayouts = {});
	static ref<Pipeline> CreateCompute(const Device& device, const ref<const ShaderModule>& shader, const ref<PipelineLayout>& layout, const ComputePipelineInfo& info = {});
	static ref<Pipeline> CreateGraphics(const Device& device, const ref<const ShaderModule>& vertexShader, const ref<const ShaderModule>& fragmentShader, const GraphicsPipelineInfo& info = {}, const PipelineLayoutInfo& layoutInfo = {}, const DescriptorSetLayouts& descriptorSetLayouts = {});

	inline       vk::raii::Pipeline& operator*()        { return mPipeline; }
	inline const vk::raii::Pipeline& operator*() const  { return mPipeline; }
	inline       vk::raii::Pipeline* operator->()       { return &mPipeline; }
	inline const vk::raii::Pipeline* operator->() const { return &mPipeline; }

	inline const ref<const PipelineLayout>& Layout() const { return mLayout; }
	inline const ref<const ShaderModule>& GetShader() const { return *mShaders.begin(); }
	inline const ref<const ShaderModule>& GetShader(const vk::ShaderStageFlagBits stage) const {
		return *std::ranges::find(mShaders, stage, &ShaderModule::Stage);
	}
};

void PrintBinding(const ShaderParameterBinding& binding, uint32_t depth = 0);

inline uint3 GetDispatchDim(const uint3 workgroupSize, const uint3 extent) {
	return (extent + workgroupSize - uint3(1)) / workgroupSize;
}

}