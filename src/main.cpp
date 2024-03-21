#include <iostream>

#include "Core/Instance.hpp"
#include "Core/Program.hpp"
#include "Core/Window.hpp"

int main(int argc, char** argv) {
	std::span args = { argv, (size_t)argc };

	using namespace RoseEngine;

	std::vector<std::string> instanceExtensions;
	for (const auto& e : Window::RequiredInstanceExtensions()) instanceExtensions.emplace_back(e);
	ref<Instance> instance = Instance::Create(instanceExtensions, { "VK_LAYER_KHRONOS_validation" });

	ref<Window> window = Window::Create(*instance, "Rose", uint2(1920, 1080));

	auto [physicalDevice, presentQueueFamily] = Window::FindSupportedDevice(**instance);
	ref<Device> device = Device::Create(*instance, physicalDevice);

	auto test = Program::Create(*device, FindShaderPath("Test.slang"));

	while (window->IsOpen()) {
		window->PollEvents();

		ref<CommandContext> context = CommandContext::Create(device);

		auto data    = Buffer::Create(*device, std::vector<float>{ 1, 2, 3, 4, 5 }, vk::BufferUsageFlagBits::eTransferSrc|vk::BufferUsageFlagBits::eTransferDst);
		auto dataGpu = Buffer::Create(*device, data.size_bytes());

		float scale   =  2.0f;
		float offset  =  0.5f;
		float scale2  =  3.0f;
		float offset2 = -0.5f;
		float scale3  = -1.f;
		float offset3 =  0.5f;
		float scale4  =  0.25f;
		float offset4 = -1.0f;

		std::cout << "expecting: ";
		for (float f : data) {
			f = f * scale + offset;
			f = f * scale2 + offset2;
			f = f * scale3 + offset3;
			f = f * scale4 + offset4;
			std::cout << f << ", ";
		}
		std::cout << std::endl;

		auto& root = test->RootParameter();
		root["scale"] = scale;
		root["offset"] = offset;
		root["scale2"] = scale2;
		root["offset2"] = offset2;
		root["gBlock"]["scale3"] = scale3;
		root["gBlock"]["offset3"] = offset3;
		root["scale4"] = scale4;
		root["offset4"] = offset4;
		root["data"] = dataGpu;

		context->Copy(data, dataGpu);
		test->Dispatch(*context, (uint32_t)data.size());
		context->Copy(dataGpu, data);
		device->Wait(context->Submit());

		std::cout << "got      : ";
		for (float f : data)
			std::cout << f << ", ";
		std::cout << std::endl;
	}

	return EXIT_SUCCESS;
}