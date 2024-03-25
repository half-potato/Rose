#include "TerrainRenderer.hpp"
#include <Core/Gui.hpp>

namespace RoseEngine {

void TerrainRenderer::Initialize(CommandContext& context) {
	cbt = make_ref<ConcurrentBinaryTree>();
	cbt->Initialize(context);
}

void TerrainRenderer::CreatePipelines(Device& device, vk::Format format) {
	cbt->CreatePipelines(device);

	auto rasterSrc = FindShaderPath("Terrain.3d.slang");

	ref<const ShaderModule> vertexShader, fragmentShader;
	if (drawPipeline) {
		vertexShader   = drawPipeline->GetShader(vk::ShaderStageFlagBits::eVertex);
		fragmentShader = drawPipeline->GetShader(vk::ShaderStageFlagBits::eFragment);
	}
	if (!vertexShader || vertexShader->IsStale())
		vertexShader   = ShaderModule::Create(device, rasterSrc, "vertexMain");
	if (!fragmentShader || fragmentShader->IsStale())
		fragmentShader = ShaderModule::Create(device, rasterSrc, "fragmentMain");

	GraphicsPipelineInfo pipelineInfo {
		.vertexInputState = VertexInputDescription{},
		.inputAssemblyState = vk::PipelineInputAssemblyStateCreateInfo{
			.topology = vk::PrimitiveTopology::eTriangleList },
		.rasterizationState = vk::PipelineRasterizationStateCreateInfo{
			.depthClampEnable = false,
			.rasterizerDiscardEnable = false,
			.polygonMode = vk::PolygonMode::eFill,
			.cullMode = vk::CullModeFlagBits::eNone,
			.frontFace = vk::FrontFace::eCounterClockwise,
			.depthBiasEnable = false },
		.multisampleState = vk::PipelineMultisampleStateCreateInfo{},
		.depthStencilState = vk::PipelineDepthStencilStateCreateInfo{
			.depthTestEnable = false,
			.depthWriteEnable = true,
			.depthCompareOp = vk::CompareOp::eLess,
			.depthBoundsTestEnable = false,
			.stencilTestEnable = false },
		.viewports = { vk::Viewport{} },
		.scissors = { vk::Rect2D{} },
		.colorBlendState = ColorBlendState{
			.attachments = std::vector<vk::PipelineColorBlendAttachmentState>(2, vk::PipelineColorBlendAttachmentState{
				.blendEnable         = false,
				.srcColorBlendFactor = vk::BlendFactor::eZero,
				.dstColorBlendFactor = vk::BlendFactor::eOne,
				.colorBlendOp        = vk::BlendOp::eAdd,
				.srcAlphaBlendFactor = vk::BlendFactor::eZero,
				.dstAlphaBlendFactor = vk::BlendFactor::eOne,
				.alphaBlendOp        = vk::BlendOp::eAdd,
				.colorWriteMask      = vk::ColorComponentFlags{vk::FlagTraits<vk::ColorComponentFlagBits>::allFlags} }) },
		.dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor },
		.dynamicRenderingState = DynamicRenderingState{
			.colorFormats = { format, vk::Format::eR32G32B32A32Uint },
			.depthFormat = { vk::Format::eD32Sfloat } } };
	drawPipeline = Pipeline::CreateGraphics(device, vertexShader, fragmentShader, pipelineInfo);

	pipelineFormat = format;
}

void TerrainRenderer::PreRender(CommandContext& context, const GBuffer& gbuffer, const Transform& view, const Transform& projection) {
	if (!drawPipeline
		|| gbuffer.renderTarget.GetImage()->Info().format != pipelineFormat
		|| ImGui::IsKeyPressed(ImGuiKey_F5, false))
		CreatePipelines(context.GetDevice(), gbuffer.renderTarget.GetImage()->Info().format);

	drawIndirectArgs = cachedDrawArgs.pop_or_create(context.GetDevice(), [&]() {
		auto buf = Buffer::Create( context.GetDevice(), sizeof(uint4), vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eStorageBuffer );
		context.GetDevice().SetDebugName( **buf.mBuffer, "Terrain Indirect Draw Args" );
		return buf;
	});
	cachedDrawArgs.push(drawIndirectArgs, context.GetDevice().NextTimelineSignal());

	drawParameters = cbt->Update(context, drawIndirectArgs);

	drawParameters["transform"] = Transform::Identity();
	descriptorSets = context.GetDescriptorSets(*drawPipeline->Layout());
	context.UpdateDescriptorSets(*descriptorSets, drawParameters, *drawPipeline->Layout());
}

void TerrainRenderer::InspectorGui() {
	ImGui::Checkbox("Use CPU", &cbt->useCpu);
	Gui::ScalarField("Depth", &cbt->maxDepth);
	auto[size,unit] = FormatBytes(cbt->GetBuffer(0).size());
	ImGui::LabelText("Tree info", "Size: %llu %s (%llu nodes)", size, unit, cbt->NodeCount());
}

void TerrainRenderer::Render(CommandContext& context) {
	context->bindPipeline(vk::PipelineBindPoint::eGraphics, ***drawPipeline);
	context.BindDescriptors(*drawPipeline->Layout(), *descriptorSets);
	context.PushConstants(*drawPipeline->Layout(), drawParameters);
	context->drawIndirect(**drawIndirectArgs.mBuffer, drawIndirectArgs.mOffset, 1, sizeof(uint4));
}

}