#include <iostream>

#include <Rose/Core/Instance.hpp>
#include <Rose/Core/Window.hpp>
#include <Rose/Core/CommandContext.hpp>
#include <Rose/Scene/Mesh.hpp>

using namespace RoseEngine;

class App {
public:
	ref<Instance>       instance  = nullptr;
	ref<Device>         device    = nullptr;
	ref<Window>         window    = nullptr;
	ref<Swapchain>      swapchain = nullptr;
	ref<CommandContext> context   = nullptr;

	vk::raii::Semaphore commandSignalSemaphore = nullptr;

	uint32_t presentQueueFamily = 0;

	Mesh mesh = {};
	MeshLayout meshLayout = {};
	ref<Pipeline> pipeline = {};

	inline App(std::span<const char*, std::dynamic_extent> args) {
		std::vector<std::string> instanceExtensions;
		for (const auto& e : Window::RequiredInstanceExtensions())
			instanceExtensions.emplace_back(e);

		instance = Instance::Create(instanceExtensions, { "VK_LAYER_KHRONOS_validation" });

		vk::raii::PhysicalDevice physicalDevice = nullptr;
		std::tie(physicalDevice, presentQueueFamily) = Window::FindSupportedDevice(**instance);
		device = Device::Create(*instance, physicalDevice, { VK_KHR_SWAPCHAIN_EXTENSION_NAME });

		window    = Window::Create(*instance, "Rose", uint2(1920, 1080));
		swapchain = Swapchain::Create(device, *window->GetSurface());
		context   = CommandContext::Create(device, presentQueueFamily);

		commandSignalSemaphore = (*device)->createSemaphore(vk::SemaphoreCreateInfo(vk::SemaphoreCreateInfo{}));

		context->Begin();

		mesh = Mesh {
			.indexBuffer = context->UploadData(std::vector<uint16_t>{ 0, 1, 2, 1, 3, 2 }, vk::BufferUsageFlagBits::eIndexBuffer),
			.indexSize = sizeof(uint16_t),
			.topology = vk::PrimitiveTopology::eTriangleList };
		mesh.vertexAttributes[MeshVertexAttributeType::ePosition].emplace_back(
			context->UploadData(std::vector<float3>{
					float3(-.25f, -.25f, 0), float3(.25f, -.25f, 0),
					float3(-.25f,  .25f, 0), float3(.25f,  .25f, 0),
				}, vk::BufferUsageFlagBits::eVertexBuffer),
			MeshVertexAttributeLayout{
				.stride = sizeof(float3),
				.format = vk::Format::eR32G32B32Sfloat,
				.offset = 0,
				.inputRate = vk::VertexInputRate::eVertex });
		mesh.vertexAttributes[MeshVertexAttributeType::eColor].emplace_back(
			context->UploadData(std::vector<float3>{
					float3(0.5f, 0.5f, 0), float3(1.0f, 0.5f, 0),
					float3(0.5f, 1.0f, 0), float3(1.0f, 1.0f, 0),
				}, vk::BufferUsageFlagBits::eVertexBuffer),
			MeshVertexAttributeLayout{
				.stride = sizeof(float3),
				.format = vk::Format::eR32G32B32Sfloat,
				.offset = 0,
				.inputRate = vk::VertexInputRate::eVertex });

		context->Submit();

	}
	inline ~App() {
		device->Wait();
		(*device)->waitIdle();
	}

	inline bool CreateSwapchain() {
		device->Wait();
		if (!swapchain->Recreate(*window->GetSurface(), { presentQueueFamily }))
			return false; // Window unavailable (minimized?)

		ref<const ShaderModule> vertexShader, fragmentShader;
		if (pipeline) {
			vertexShader   = pipeline->GetShader(vk::ShaderStageFlagBits::eVertex);
			fragmentShader = pipeline->GetShader(vk::ShaderStageFlagBits::eFragment);
		}
		if (!vertexShader || vertexShader->IsStale()) {
			vertexShader   = ShaderModule::Create(*device, FindShaderPath("Mesh.3d.slang"), "vertexMain");
			meshLayout = mesh.GetLayout(*vertexShader);
		}
		if (!fragmentShader || fragmentShader->IsStale())
			fragmentShader = ShaderModule::Create(*device, FindShaderPath("Mesh.3d.slang"), "fragmentMain");

		// get vertex buffer bindings from the mesh layout

		GraphicsPipelineInfo pipelineInfo {
			.vertexInputState = VertexInputDescription{
				.bindings = meshLayout.bindings,
				.attributes = meshLayout.attributes },
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
				.depthCompareOp = vk::CompareOp::eGreater,
				.depthBoundsTestEnable = false,
				.stencilTestEnable = false },
			.viewports = { vk::Viewport{} },
			.scissors = { vk::Rect2D{} },
			.colorBlendState = ColorBlendState{
				.attachments = { vk::PipelineColorBlendAttachmentState{
					.blendEnable         = false,
					.srcColorBlendFactor = vk::BlendFactor::eZero,
					.dstColorBlendFactor = vk::BlendFactor::eOne,
					.colorBlendOp        = vk::BlendOp::eAdd,
					.srcAlphaBlendFactor = vk::BlendFactor::eZero,
					.dstAlphaBlendFactor = vk::BlendFactor::eOne,
					.alphaBlendOp        = vk::BlendOp::eAdd,
					.colorWriteMask      = vk::ColorComponentFlags{vk::FlagTraits<vk::ColorComponentFlagBits>::allFlags} } } },
			.dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor },
			.dynamicRenderingState = DynamicRenderingState{
				.colorFormats = { swapchain->GetFormat().format },
				.depthFormat = {} } };
		pipeline = Pipeline::CreateGraphics(*device, { vertexShader, fragmentShader }, pipelineInfo);

		return true;
	}

	inline void Render(const ImageView& renderTarget) {
		context->ClearColor(renderTarget, vk::ClearColorValue{std::array<float,4>{ .5f, .7f, 1.f, 1.f }});

		context->AddBarrier(renderTarget, Image::ResourceState{
			.layout = vk::ImageLayout::eColorAttachmentOptimal,
			.stage  = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			.access =  vk::AccessFlagBits2::eColorAttachmentRead|vk::AccessFlagBits2::eColorAttachmentWrite,
			.queueFamily = presentQueueFamily
		});
		context->ExecuteBarriers();

		vk::RenderingAttachmentInfo attachment {
			.imageView = *renderTarget,
			.imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
			.resolveMode = vk::ResolveModeFlagBits::eNone,
			.resolveImageView = {},
			.resolveImageLayout = vk::ImageLayout::eUndefined,
			.loadOp  = vk::AttachmentLoadOp::eLoad,
			.storeOp = vk::AttachmentStoreOp::eStore,
			.clearValue = vk::ClearValue{vk::ClearColorValue{std::array<float,4>{}}} };
		vk::RenderingInfo renderInfo {
			.renderArea = vk::Rect2D{ vk::Offset2D{0, 0}, vk::Extent2D{ renderTarget.Extent().x, renderTarget.Extent().y } },
			.layerCount = 1,
			.viewMask = 0 };
		renderInfo.setColorAttachments(attachment);
		(*context)->beginRendering(renderInfo);

		(*context)->setViewport(0, vk::Viewport{ 0, 0, (float)renderTarget.Extent().x, (float)renderTarget.Extent().y, 0, 1 });
		(*context)->setScissor(0, vk::Rect2D{ vk::Offset2D{0, 0}, vk::Extent2D{ renderTarget.Extent().x, renderTarget.Extent().y } } );

		(*context)->bindPipeline(vk::PipelineBindPoint::eGraphics, ***pipeline);

		mesh.Bind(*context, meshLayout);
		(*context)->drawIndexed(mesh.indexBuffer.size_bytes() / sizeof(uint16_t), 1, 0, 0, 0);

		(*context)->endRendering();
		renderTarget.SetState(Image::ResourceState{
			.layout = vk::ImageLayout::eColorAttachmentOptimal,
			.stage  = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			.access = vk::AccessFlagBits2::eColorAttachmentWrite,
			.queueFamily = presentQueueFamily });
	}

	inline void DoFrame() {
		context->Begin();

		Render(swapchain->CurrentImage());

		context->AddBarrier(swapchain->CurrentImage(), Image::ResourceState{
			.layout = vk::ImageLayout::ePresentSrcKHR,
			.stage  = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			.access = vk::AccessFlagBits2::eNone,
			.queueFamily = presentQueueFamily });
		context->ExecuteBarriers();
		context->Submit(0,
			*commandSignalSemaphore, (size_t)0,
			swapchain->ImageAvailableSemaphore(),
			(vk::PipelineStageFlags)vk::PipelineStageFlagBits::eColorAttachmentOutput,
			(size_t)0);

		swapchain->Present(*(*device)->getQueue(presentQueueFamily, 0), *commandSignalSemaphore);
	}

	inline void Run() {
		while (true) {
			Window::PollEvents();
			if (!window->IsOpen())
				break;

			if (swapchain->Dirty() || window->GetExtent() != swapchain->Extent()) {
				if (!CreateSwapchain())
					continue;
			}

			if (swapchain->AcquireImage())
				DoFrame();
		}
	}
};

int main(int argc, const char** argv) {
	App app(std::span { argv, (size_t)argc });
	app.Run();
	std::cout << "SUCCESS" << std::endl;
	return EXIT_SUCCESS;
}