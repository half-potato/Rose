#include <iostream>
#include <span>

#include "Core/Instance.hpp"
#include "Core/Program.hpp"

int main(int argc, char** argv) {
	std::span args = { argv, (size_t)argc };

	using namespace RoseEngine;

	Instance instance = Instance({}, { "VK_LAYER_KHRONOS_validation" });

	ref<Device> device = make_ref<Device>(instance, instance->enumeratePhysicalDevices()[0]);

	ref<CommandContext> context = CommandContext::Create(device);

	auto data = CreateBuffer(*device, std::vector<float>{ 1, 2, 3, 4, 5 }, vk::BufferUsageFlagBits::eTransferSrc|vk::BufferUsageFlagBits::eTransferDst);

	ConstantParameter scale = 2.f;
	ConstantParameter offset = 0.5f;
	ConstantParameter scale2 = -1.f;

	std::cout << "expecting: ";
	for (float f : data)
		std::cout << (f*scale.get<float>() + offset.get<float>()) * scale2.get<float>() << ", ";
	std::cout << std::endl;

	auto test = Program::Create(*device, FindShaderPath("Test.slang"));
	test->Parameter("scale2") = scale2;

	BufferParameter dataGpu = CreateBuffer(*device, data.size_bytes());
	context->Copy(data, dataGpu);
	test->Dispatch(*context, (uint32_t)data.size(), dataGpu, scale, offset);
	context->Copy(dataGpu, data);
	device->Wait(context->Submit());

	std::cout << "got: ";
	for (float f : data)
		std::cout << f << ", ";
	std::cout << std::endl;

	return EXIT_SUCCESS;
}