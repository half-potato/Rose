#include <iostream>

#include "AppContext.hpp"
#include "Core/Program.hpp"

int main(int argc, char** argv) {
	std::span args = { argv, (size_t)argc };

	using namespace RoseEngine;

	Instance instance = Instance({}, { "VK_LAYER_KHRONOS_validation" });
	Device device = Device(instance, instance->enumeratePhysicalDevices()[0]);

	Program test = CreateProgram(device, "Test.slang");

	auto data = CreateBuffer(device, std::vector<float>{ 1, 2, 3, 4, 5 });

	std::shared_ptr<vk::raii::CommandBuffer> commandBuffer;

	test(*commandBuffer, { (uint32_t)data.size(), 1, 1 }, BufferParameter(data), ConstantParameter(2.f), ConstantParameter(0.5f));

	for (float f : data)
		std::cout << f << ", ";
	std::cout << std::endl;

	return EXIT_SUCCESS;
}