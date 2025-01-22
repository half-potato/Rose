#include "Core/Instance.hpp"
#include "Core/WorkNode.hpp"

#include <iostream>

int main(int argc, const char** argv) {
	std::span args = { argv, (size_t)argc };

	using namespace RoseEngine;

	ref<Instance> instance = Instance::Create({}, { "VK_LAYER_KHRONOS_validation" });
	ref<Device>   device   = Device::Create(*instance, (*instance)->enumeratePhysicalDevices()[0]);

	std::vector<float> inputData = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
	auto dataCpu = Buffer::Create(*device, inputData, vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst);
	auto dataGpu = Buffer::Create(*device, dataCpu.size_bytes());
	auto dataGpu2 = Buffer::Create(*device, dataCpu.size_bytes());

	float scale  = 2.0f;
	float offset = 0.5f;
	uint32_t blurRadius = 2;

	ShaderParameter params;
	params["scale"] = scale;
	params["offset"] = offset;
	params["data"] = dataGpu;
	params["data2"] = dataGpu2;
	params["blurRadius"] = blurRadius;
	params["dataSize"] = (uint32_t)inputData.size();

	auto scaleOffset = Pipeline::CreateCompute(*device, ShaderModule::Create(*device, FindShaderPath("TestGraph.cs.slang"), "applyScaleOffset"));
	auto blur        = Pipeline::CreateCompute(*device, ShaderModule::Create(*device, FindShaderPath("TestGraph.cs.slang"), "blur"));

	ref<CommandContext> context = CommandContext::Create(device, vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer);
	context->Begin();

	context->Copy(dataCpu, dataGpu);
	context->Dispatch(*scaleOffset, (uint32_t)inputData.size(), params);
	context->Dispatch(*blur, (uint32_t)inputData.size(), params);
	context->Copy(dataGpu2, dataCpu);

	uint64_t value = context->Submit();
	device->Wait(value);

	std::vector<float> tmp(inputData.size());
	for (size_t i = 0; i < inputData.size(); i++) {
		tmp[i] = inputData[i] * scale + offset;
	}
	for (int i = 0; i < inputData.size(); i++) {
		float v = 0;
		float wsum = 0;
		for (int j = -(int)blurRadius; j <= (int)blurRadius; j++) {
			int p = i + j;
			if (p >= 0 && p < (int)inputData.size()) {
				float w = 1 / float(1 << (1 + abs(i)));
				wsum += w;
				v += tmp[p] * w;
			}
		}
		inputData[i] = wsum > 0 ? v/wsum : 0;
	}

	for (size_t i = 0; i < inputData.size(); i++) {
		if (inputData[i] != dataCpu[i]) {
			std::cout << "Mismatch at index " << i << ": " << inputData[i] << " != " << dataCpu[i] << std::endl;
			std::cout << "FAILURE" << std::endl;
			return EXIT_FAILURE;
		}
	}

	std::cout << "SUCCESS" << std::endl;
	return EXIT_SUCCESS;
}