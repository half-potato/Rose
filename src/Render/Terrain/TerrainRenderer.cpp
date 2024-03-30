#include <iostream>
#include "TerrainRenderer.hpp"
#include <Core/Gui.hpp>
#include <Render/Procedural/MathNode.hpp>

namespace RoseEngine {

void TerrainRenderer::Initialize(CommandContext& context) {
	cbt = ConcurrentBinaryTree::Create(context, 20, 6);

	NodeOutputConnection posInput{ make_ref<ProceduralInputNode>("position", "float3"), "position" };

	nodeTree = make_ref<ProceduralNodeTree>("height", "float",
		make_ref<MathNode>(MathNode::MathOp::eAdd,
			make_ref<MathNode>(MathNode::MathOp::eLength, posInput),
			make_ref<ExpressionNode>("1")));
}

void TerrainRenderer::CreatePipelines(Device& device, vk::Format format) {
	auto srcFile = FindShaderPath("Terrain.3d.slang");

	size_t nodeHash = nodeTree->Root().hash();

	std::string nodeSrc = nodeTree->compile("");

	ShaderDefines defs {
		{ "CBT_HEAP_BUFFER_COUNT", std::to_string(cbt->ArraySize()) },
		{ "PROCEDURAL_NODE_SRC", nodeSrc },
	};

	if (!subdividePipeline || subdividePipeline->GetShader(vk::ShaderStageFlagBits::eCompute)->IsStale() || nodeHash != nodeTreeHash)
		subdividePipeline = Pipeline::CreateCompute(device, ShaderModule::Create(device, srcFile, "Subdivide", "sm_6_7", defs));

	ref<const ShaderModule> vertexShader, fragmentShader;
	if (drawPipeline) {
		vertexShader   = drawPipeline->GetShader(vk::ShaderStageFlagBits::eVertex);
		fragmentShader = drawPipeline->GetShader(vk::ShaderStageFlagBits::eFragment);
	}
	if (!vertexShader || vertexShader->IsStale() || nodeHash != nodeTreeHash)
		vertexShader   = ShaderModule::Create(device, srcFile, "vertexMain", "sm_6_7", defs);
	if (!fragmentShader || fragmentShader->IsStale()|| nodeHash != nodeTreeHash)
		fragmentShader = ShaderModule::Create(device, srcFile, "fragmentMain", "sm_6_7", defs);

	GraphicsPipelineInfo pipelineInfo {
		.vertexInputState = VertexInputDescription{},
		.inputAssemblyState = vk::PipelineInputAssemblyStateCreateInfo{
			.topology = vk::PrimitiveTopology::eTriangleList },
		.rasterizationState = vk::PipelineRasterizationStateCreateInfo{
			.depthClampEnable = false,
			.rasterizerDiscardEnable = false,
			.polygonMode = wire ? vk::PolygonMode::eLine : vk::PolygonMode::eFill,
			.cullMode = wire ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack,
			.frontFace = vk::FrontFace::eCounterClockwise,
			.depthBiasEnable = false },
		.multisampleState = vk::PipelineMultisampleStateCreateInfo{},
		.depthStencilState = vk::PipelineDepthStencilStateCreateInfo{
			.depthTestEnable = true,
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
	nodeTreeHash = nodeHash;
}

void TerrainRenderer::InspectorGui(CommandContext& context) {
	if (ImGui::CollapsingHeader("Concurrent binary tree")) {
		uint32_t depth = cbt->MaxDepth();
		bool square = cbt->Square();
		bool changed = false;
		changed |= Gui::ScalarField("Depth", &depth);
		changed |= ImGui::Checkbox("Square", &square);
		if (changed && depth > 5 && depth < 38) {
			context.GetDevice().Wait();
			cbt = ConcurrentBinaryTree::Create(context, depth, 6, square);
		}

		auto[size,unit] = FormatBytes(cbt->GetBuffer(0).size());
		ImGui::LabelText("Size: ", "%llu %s", size, unit);
	}

	if (ImGui::CollapsingHeader("Rendering")) {
		Gui::ScalarField("Target triangle size", &targetTriangleSize, 1.f, 2000.f, .1f);
		if (ImGui::Checkbox("Wire", &wire))
			pipelineFormat = vk::Format::eUndefined;

		if (ImGui::DragFloat3("Light dir", &lightDir.x, 0.025f))
			lightDir = normalize(lightDir);
	}
}

void TerrainRenderer::PreRender(CommandContext& context, const GBuffer& gbuffer, const Transform& view, const Transform& projection) {
	if (!drawPipeline
		|| gbuffer.renderTarget.GetImage()->Info().format != pipelineFormat
		|| ImGui::IsKeyPressed(ImGuiKey_F5, false))
		CreatePipelines(context.GetDevice(), gbuffer.renderTarget.GetImage()->Info().format);

	auto indirectArgs = cachedIndirectArgs.pop_or_create(context.GetDevice(), [&]() {
		auto buf1 = Buffer::Create(context.GetDevice(), sizeof(uint4) * cbt->ArraySize(), vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eStorageBuffer);
		auto buf2 = Buffer::Create(context.GetDevice(), sizeof(uint4) * cbt->ArraySize(), vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eStorageBuffer);
		context.GetDevice().SetDebugName( **buf1.mBuffer, "Terrain Indirect Args" );
		context.GetDevice().SetDebugName( **buf2.mBuffer, "Terrain Indirect Args" );
		return std::pair{ buf1, buf2 };
	});
	cachedIndirectArgs.push(indirectArgs, context.GetDevice().NextTimelineSignal());
	auto[dispatchArgs, drawArgs] = indirectArgs;

	ShaderParameter params = cbt->GetShaderParameter();
	params["worldToCamera"] = view;
	params["projection"] = projection;
	params["lightDir"] = lightDir;
	params["targetSize"] = targetTriangleSize;
	params["screenSize"] = uint2(gbuffer.renderTarget.Extent());

	// subdivision
	{
		cbt->WriteIndirectDispatchArgs(context, dispatchArgs, subdividePipeline->GetShader(vk::ShaderStageFlagBits::eCompute)->WorkgroupSize().x);
		context.AddBarrier(dispatchArgs, Buffer::ResourceState{
			.stage  = vk::PipelineStageFlagBits2::eDrawIndirect,
			.access = vk::AccessFlagBits2::eIndirectCommandRead,
			.queueFamily = context.QueueFamily() });
		context.ExecuteBarriers();

		params["split"]  = split ? 1u : 0u;
		split = !split;

		context->bindPipeline(vk::PipelineBindPoint::eCompute, ***subdividePipeline);
		context.BindParameters(*subdividePipeline->Layout(), params);

		params["cbtID"]  = split ? 1u : 0u;
		for (uint32_t i = 0; i < cbt->ArraySize(); i++) {
			context->pushConstants<uint32_t>(***subdividePipeline->Layout(), vk::ShaderStageFlagBits::eCompute, 0, i);
			context->dispatchIndirect(**dispatchArgs.mBuffer, dispatchArgs.mOffset + i*sizeof(uint4));
		}
	}

	cbt->Build(context);

	// write indirect draw args

	cbt->WriteIndirectDrawArgs(context, drawArgs);
	context.AddBarrier(drawArgs, Buffer::ResourceState{
		.stage = vk::PipelineStageFlagBits2::eDrawIndirect,
		.access = vk::AccessFlagBits2::eIndirectCommandRead,
		.queueFamily = context.QueueFamily() });
	drawIndirectArgs = drawArgs;

	// create descriptor sets for draw

	descriptorSets = context.GetDescriptorSets(*drawPipeline->Layout());
	context.UpdateDescriptorSets(*descriptorSets, params, *drawPipeline->Layout());
}

void TerrainRenderer::Render(CommandContext& context) {
	context->bindPipeline(vk::PipelineBindPoint::eGraphics, ***drawPipeline);
	context.BindDescriptors(*drawPipeline->Layout(), *descriptorSets);
	for (uint32_t i = 0; i < cbt->ArraySize(); i++) {
		context->pushConstants<uint32_t>(***drawPipeline->Layout(), vk::ShaderStageFlagBits::eVertex|vk::ShaderStageFlagBits::eFragment, 0, i);
		context->drawIndirect(**drawIndirectArgs.mBuffer, drawIndirectArgs.mOffset + i*sizeof(uint4), 1, sizeof(uint4));
	}
}

void TerrainRenderer::NodeGui() {
	nodeTree->NodeGui();
}

}