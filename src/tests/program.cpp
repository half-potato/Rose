#include "Core/Instance.hpp"
#include "Core/Program.hpp"

#include <iostream>

int main(int argc, char** argv) {
	std::span args = { argv, (size_t)argc };

	using namespace RoseEngine;

	ref<Instance> instance = Instance::Create({}, { "VK_LAYER_KHRONOS_validation" });
	ref<Device>   device   = Device::Create(*instance, (*instance)->enumeratePhysicalDevices()[0]);

	std::vector<float> inputData = { 1, 2, 3, 4, 5 };
	auto dataCpu = Buffer::Create(*device, inputData, vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst);

	float scale   =  2.0f;
	float offset  =  0.5f;
	float scale2  =  3.0f;
	float offset2 = -0.5f;

	auto test = Program::Create(*device, FindShaderPath("Test.slang"));

	auto dataGpu = Buffer::Create(*device, dataCpu.size_bytes());
	auto& root = test->RootParameter();
	root["scale"] = scale;
	root["offset"] = offset;
	root["scale2"] = scale2;
	root["offset2"] = offset2;
	root["data"] = dataGpu;

	ref<CommandContext> context = CommandContext::Create(device, vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer);
	context->Begin();

	context->Copy(dataCpu, dataGpu);
	test->Dispatch(*context, (uint32_t)inputData.size());
	context->Copy(dataGpu, dataCpu);

	uint64_t value = context->Submit();
	device->Wait(value);

	for (size_t i = 0; i < inputData.size(); i++) {
		float f = inputData[i];
		f = f * scale + offset;
		f = f * scale2 + offset2;

		if (f != dataCpu[i]) {
			std::cout << "Mismatch at index" << i << ": " << f << " != " << dataCpu[i] << std::endl;
			std::cout << "FAILURE" << std::endl;
			return EXIT_FAILURE;
		}
	}

	std::cout << "SUCCESS" << std::endl;
	return EXIT_SUCCESS;
}