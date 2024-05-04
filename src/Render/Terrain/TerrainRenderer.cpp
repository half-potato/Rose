#include "TerrainRenderer.hpp"
#include <Core/Gui.hpp>
#include <Render/Procedural/MathNode.hpp>
#include <portable-file-dialogs.h>
#include <iostream>

namespace RoseEngine {

void TerrainRenderer::Initialize(CommandContext& context) {
	if (std::filesystem::exists("GetDensity.bson")) {
		auto jsonData = ReadFile<std::vector<uint8_t>>("GetDensity.bson");
		json serialized = json::from_bson(jsonData);
		heightFunction = ProceduralFunction::Deserialize(serialized);
	} else {
		heightFunction = ProceduralFunction("GetDensity",
			// outputs
			NameMap<std::string>{ { "density", "float" } },
			// inputs
			NameMap<NodeOutputConnection>{ { "density",
				make_ref<MathNode>(MathNode::MathOp::eAdd,
					make_ref<MathNode>(MathNode::MathOp::eLength,
						NodeOutputConnection{ make_ref<ProceduralFunction::InputVariable>("position", "float3"), "position" }),
					make_ref<ExpressionNode>("1")) }
		});
	}
}
TerrainRenderer::~TerrainRenderer() {
	WriteFile("GetDensity.bson", json::to_bson(heightFunction.Serialize()));
	if (compiling && compileJob.valid()) {
		compileJob.wait();
	}
}

void TerrainRenderer::CreatePipelines(Device& device, vk::Format format) {
	if (compiling) return;

	compiling = true;

	compileJob = std::async(std::launch::async, [&,format]() -> std::tuple<ref<Pipeline>, ref<Pipeline>, vk::Format, size_t>{
		auto srcFile = FindShaderPath("DCTerrain.3d.slang");
		auto nodeSrcFile = FindShaderPath("OctVis.3d.slang");

		size_t nodeHash = heightFunction.Root().hash();
		if (nodeHash != nodeTreeHash)
			compiledHeightFunction = heightFunction.Compile("\n");

		if (nodeHash != nodeTreeHash || !mesher)
			mesher = make_ref<DualContourMesher>(device, compiledHeightFunction);

		ShaderDefines defs {
			{ "PROCEDURAL_NODE_SRC", compiledHeightFunction }
		};


		ref<const ShaderModule> vertexShader, fragmentShader;
		if (drawPipeline) {
			vertexShader   = drawPipeline->GetShader(vk::ShaderStageFlagBits::eVertex);
			fragmentShader = drawPipeline->GetShader(vk::ShaderStageFlagBits::eFragment);
		}
		if (!vertexShader || vertexShader->IsStale() || nodeHash != nodeTreeHash)
			vertexShader   = ShaderModule::Create(device, srcFile, "vertexMain", "sm_6_7", defs, {}, false);
		if (!fragmentShader || fragmentShader->IsStale() || nodeHash != nodeTreeHash)
			fragmentShader = ShaderModule::Create(device, srcFile, "fragmentMain", "sm_6_7", defs, {}, false);

		GraphicsPipelineInfo pipelineInfo {
			.vertexInputState = VertexInputDescription{
				{ vk::VertexInputBindingDescription{
					.binding = 0,
					.stride = sizeof(float3),
					.inputRate = vk::VertexInputRate::eVertex } },
				{ vk::VertexInputAttributeDescription{
					.location = 0,
					.binding  = 0,
					.format = vk::Format::eR32G32B32Sfloat,
					.offset = 0 } },
			},
			.inputAssemblyState = vk::PipelineInputAssemblyStateCreateInfo{
				.topology = vk::PrimitiveTopology::eTriangleList },
			.rasterizationState = vk::PipelineRasterizationStateCreateInfo{
				.depthClampEnable = false,
				.rasterizerDiscardEnable = false,
				.polygonMode = wire ? vk::PolygonMode::eLine : vk::PolygonMode::eFill,
				.cullMode = showBackfaces ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack,
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

		auto drawp = Pipeline::CreateGraphics(device, vertexShader, fragmentShader, pipelineInfo);


		ref<const ShaderModule> nodeVertexShader, nodeFragmentShader;
		if (drawNodePipeline) {
			nodeVertexShader   = drawNodePipeline->GetShader(vk::ShaderStageFlagBits::eVertex);
			nodeFragmentShader = drawNodePipeline->GetShader(vk::ShaderStageFlagBits::eFragment);
		}
		if (!nodeVertexShader || nodeVertexShader->IsStale())     nodeVertexShader   = ShaderModule::Create(device, nodeSrcFile, "vertexMain");
		if (!nodeFragmentShader || nodeFragmentShader->IsStale()) nodeFragmentShader = ShaderModule::Create(device, nodeSrcFile, "fragmentMain");

		GraphicsPipelineInfo nodePipelineInfo {
			.vertexInputState = VertexInputDescription{},
			.inputAssemblyState = vk::PipelineInputAssemblyStateCreateInfo{
				.topology = vk::PrimitiveTopology::eLineList },
			.rasterizationState = vk::PipelineRasterizationStateCreateInfo{
				.depthClampEnable = false,
				.rasterizerDiscardEnable = false,
				.polygonMode = vk::PolygonMode::eLine,
				.cullMode = vk::CullModeFlagBits::eNone,
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

		auto drawnodep = Pipeline::CreateGraphics(device, nodeVertexShader, nodeFragmentShader, nodePipelineInfo);

		return { drawp, drawnodep, format, nodeHash };
	});
}

bool TerrainRenderer::CheckCompileStatus(CommandContext& context) {
	if (!compiling || !compileJob.valid())
		return drawPipeline != nullptr;

	if (compileJob.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
		return false;
	}

	compiling = false;
	try {
		const auto[drawPipeline_, drawNodePipeline_, pipelineFormat_, nodeTreeHash_] = compileJob.get();
		if (nodeTreeHash_ != nodeTreeHash) {
			for (const auto& [n, m] : octreeMeshes)
				cachedMeshes.push(m.first, context.GetDevice().NextTimelineSignal());
			octreeMeshes = {};
			octreeRoot.Join();
		}
		context.GetDevice().Wait();
		drawPipeline = drawPipeline_;
		drawNodePipeline = drawNodePipeline_;
		pipelineFormat = pipelineFormat_;
		nodeTreeHash = nodeTreeHash_;
	} catch (std::exception e) {
		std::cout << e.what() << std::endl;
		return false;
	}
	return true;
}

void TerrainRenderer::InspectorWidget(CommandContext& context) {
	if (ImGui::CollapsingHeader("Terrain")) {
		ImGui::Indent();

		if (ImGui::CollapsingHeader("Dual contouring")) {
			bool meshDirty = false;
			meshDirty |= ImGui::DragFloat("Scale", &scale, .1f, 0, 100);
			meshDirty |= ImGui::DragScalarN("Grid size", ImGuiDataType_U32, &gridSize.x, 3);
			gridSize = glm::clamp(gridSize, uint3(2u), uint3(16384u));
			ImGui::Separator();
			meshDirty |= ImGui::DragScalar("Iterations", ImGuiDataType_U32, &dcIterations);
			meshDirty |= ImGui::DragFloat("Step size", &dcStepSize, .01f);
			if (meshDirty) {
				for (const auto& [n, m] : octreeMeshes)
					cachedMeshes.push(m.first, context.GetDevice().NextTimelineSignal());
				octreeMeshes = {};
				octreeRoot.Join();
			}
		}

		if (ImGui::CollapsingHeader("Rendering")) {
			ImGui::DragFloat("LoD split factor", &splitFactor, .1f);
			ImGui::DragScalar("Max depth", ImGuiDataType_U32, &maxDepth, 0.1f);

			if (ImGui::Checkbox("Wire", &wire))
				pipelineFormat = vk::Format::eUndefined;
			ImGui::SameLine();
			if (ImGui::Checkbox("Show backfaces", &showBackfaces))
				pipelineFormat = vk::Format::eUndefined;

			if (ImGui::DragFloat3("Light dir", &lightDir.x, 0.025f))
				lightDir = normalize(lightDir);
		}

		if (ImGui::CollapsingHeader("Height function")) {
			if (!compiling) {
				ImGui::TextUnformatted(compiledHeightFunction.c_str());
			}
		}

		ImGui::Unindent();
	}
}

void TerrainRenderer::NodeEditorWidget() {
	heightFunction.NodeEditorGui();
}

void TerrainRenderer::PreRender(CommandContext& context, const RenderData& renderData) {
	if (!drawPipeline
		|| renderData.renderTarget.GetImage()->Info().format != pipelineFormat
		|| ImGui::IsKeyPressed(ImGuiKey_F5, false))
		CreatePipelines(context.GetDevice(), renderData.renderTarget.GetImage()->Info().format);

	if (!CheckCompileStatus(context)) {
		ImGui::OpenPopup("Compiling shaders");
		return;
	}

	std::vector<float3> nodeAabbsCpu;

	// generate terrain mesh
	{
		{
			std::unordered_set<OctreeNode::NodeId> toMerge;
			const Transform octToWorld = Transform::Scale(float3(2*scale)) * Transform::Translate(float3(-.5f));
			octreeRoot.Enumerate([&](OctreeNode& n) {
				const float3 wMin = octToWorld * n.GetMin();
				const float3 wMax = octToWorld * n.GetMax();

				const float4 h0 = renderData.view * float4(wMin, 1.f);
				const float4 h1 = renderData.view * float4(wMax, 1.f);
				const float3 p0 = float3(h0)/h0.w;
				const float3 p1 = float3(h1)/h1.w;

				float3 localMin(FLT_MAX);
				float3 localMax(FLT_MIN);
				for (uint i = 0; i < 8; i++) {
					const float3 p = lerp(p0, p1, float3(i&1, (i>>1)&1, (i>>2)&1));
					localMin = glm::min(localMin, p);
					localMax = glm::max(localMax, p);
				}
				const bool containsCamera =
					(localMin.x < 0 && localMin.y < 0 && localMin.z < 0) &&
					(localMax.x > 0 && localMax.y > 0 && localMax.z > 0);
				const bool shouldSplit = containsCamera || length(localMax - localMin) > splitFactor*max(abs(localMax.z), abs(localMin.z));

				if (shouldSplit && n.IsLeaf() && n.GetId().depth < maxDepth) {
					// destroy leaf data for node
					if (auto it = octreeMeshes.find(n.GetId()); it != octreeMeshes.end()) {
						cachedMeshes.push(it->second.first, context.GetDevice().NextTimelineSignal());
						octreeMeshes.erase(it);
					}
					n.Split();
				} else if (!shouldSplit && !n.IsLeaf() || n.GetId().depth > maxDepth) {
					// destroy leaf data under node
					n.Enumerate([&](auto& l) {
						if (auto it = octreeMeshes.find(l.GetId()); it != octreeMeshes.end()) {
							cachedMeshes.push(it->second.first, context.GetDevice().NextTimelineSignal());
							octreeMeshes.erase(it);
						}
					});
					n.Join();
				}

				if (n.IsLeaf()) {
					nodeAabbsCpu.emplace_back(wMin);
					nodeAabbsCpu.emplace_back(wMax);
				}
			});
		}

		auto generateMesh = [&](auto& mesh, const float3 cellMin, const float3 cellMax) {
			mesher->GenerateMesh(context, mesh, gridSize, 2 * scale * (cellMax - cellMin)/float3(gridSize-1u), 2 * scale * (cellMin - float3(0.5f)), {
				.optimizerIterations = dcIterations,
				.optimizerStepSize   = dcStepSize,
				.indirectDispatchGroupSize = 256
			});

			context.AddBarrier(mesh.vertices, Buffer::ResourceState{
				.stage = vk::PipelineStageFlagBits2::eVertexInput,
				.access = vk::AccessFlagBits2::eVertexAttributeRead,
				.queueFamily = context.QueueFamily(),
			});
			context.AddBarrier(mesh.triangles, Buffer::ResourceState{
				.stage = vk::PipelineStageFlagBits2::eIndexInput,
				.access = vk::AccessFlagBits2::eIndexRead,
				.queueFamily = context.QueueFamily(),
			});
			context.AddBarrier(mesh.drawIndirectArgs, Buffer::ResourceState{
				.stage = vk::PipelineStageFlagBits2::eDrawIndirect,
				.access = vk::AccessFlagBits2::eIndirectCommandRead,
				.queueFamily = context.QueueFamily(),
			});
			context.AddBarrier(mesh.dispatchIndirectArgs, Buffer::ResourceState{
				.stage = vk::PipelineStageFlagBits2::eDrawIndirect,
				.access = vk::AccessFlagBits2::eIndirectCommandRead,
				.queueFamily = context.QueueFamily(),
			});
		};

		// generate meshes at leaf nodes
		octreeRoot.EnumerateLeaves([&](auto& n) {
			if (!octreeMeshes.contains(n.GetId())) {
				// create mesh for new leaves
				octreeMeshes[n.GetId()] = std::make_pair(
					cachedMeshes.pop_or_create(context.GetDevice(), [&](){ return DualContourMesher::ContourMesh(context.GetDevice(), gridSize); }),
					true
				);
			}

			auto& [mesh, meshDirty] = octreeMeshes.at(n.GetId());
			if (meshDirty) {
				generateMesh(mesh, n.GetMin(), n.GetMax());
				meshDirty = false;
			}
		});
	}

	// create descriptor sets for draw
	{
		ShaderParameter params = {};
		params["worldToCamera"] = renderData.view;
		params["projection"]    = renderData.projection;
		params["lightDir"]      = lightDir;

		descriptorSets = context.GetDescriptorSets(*drawPipeline->Layout());
		context.UpdateDescriptorSets(*descriptorSets, params, *drawPipeline->Layout());
	}

	{
		ShaderParameter params = {};
		params["worldToCamera"] = renderData.view;
		params["projection"]    = renderData.projection;
		params["aabbs"]         = nodeAabbsCpu;

		nodeDescriptorSets = context.GetDescriptorSets(*drawNodePipeline->Layout());
		context.UpdateDescriptorSets(*nodeDescriptorSets, params, *drawNodePipeline->Layout());
	}
}

void TerrainRenderer::Render(CommandContext& context, const RenderData& renderData) {
	if (drawPipeline && descriptorSets) {
		context->bindPipeline(vk::PipelineBindPoint::eGraphics, ***drawPipeline);
		context.BindDescriptors(*drawPipeline->Layout(), *descriptorSets);
		uint32_t numNodes = 0;
		octreeRoot.EnumerateLeaves([&](auto& n) {
			numNodes++;
			const auto& mesh = octreeMeshes.at(n.GetId()).first;
			context->bindVertexBuffers(0, **mesh.vertices.mBuffer, mesh.vertices.mOffset);
			context->bindIndexBuffer(**mesh.triangles.mBuffer, mesh.triangles.mOffset, vk::IndexType::eUint32);
			context->drawIndexedIndirect(**mesh.drawIndirectArgs.mBuffer, mesh.drawIndirectArgs.mOffset, 1, sizeof(VkDrawIndexedIndirectCommand));
		});

		context->bindPipeline(vk::PipelineBindPoint::eGraphics, ***drawNodePipeline);
		context.BindDescriptors(*drawNodePipeline->Layout(), *nodeDescriptorSets);
		context->draw(24, numNodes, 0, 0);
	}
}

}