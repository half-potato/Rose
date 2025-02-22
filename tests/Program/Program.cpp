#include <Rose/Core/Instance.hpp>
#include <Rose/Core/CommandContext.hpp>

#include <iostream>

namespace SlangShader {
	#include "Program.cs.slang.h"
}

int main(int argc, const char** argv) {
	std::span args = { argv, (size_t)argc };

	using namespace RoseEngine;

	std::vector<float> inputData = { 1, 2, 3, 4, 5 };
	std::vector<float> outputData(inputData.size());

	float scale   =  2.0f;
	float offset  =  0.5f;
	float scale2  =  3.0f;
	float offset2 = -0.5f;
	uint32_t dataSize = (uint32_t)inputData.size();

	ref<Instance> instance = Instance::Create({}, { "VK_LAYER_KHRONOS_validation" });
	ref<Device>   device   = Device::Create(*instance, (*instance)->enumeratePhysicalDevices()[0]);

	// run on gpu
	{
		auto dataCpu = Buffer::Create(*device, inputData, vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst);
		auto dataGpu = Buffer::Create(*device, dataCpu.size_bytes());

		auto test = Pipeline::CreateCompute(*device, ShaderModule::Create(*device, FindShaderPath("Program.cs.slang"), "testMain"));

		ShaderParameter params;
		params["scale"] = scale;
		params["offset"] = offset;
		params["scale2"] = scale2;
		params["offset2"] = offset2;
		params["data"] = dataGpu;
		params["dataSize"] = dataSize;

		ref<CommandContext> context = CommandContext::Create(device, vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer);
		context->Begin();

		context->Copy(dataCpu, dataGpu);
		context->Dispatch(*test, (uint32_t)inputData.size(), params);
		context->Copy(dataGpu, dataCpu);

		context->Submit();
		device->Wait();

		std::ranges::copy(dataCpu, outputData.begin());
	}

	// run on cpu
	{
		SlangShader::SLANG_ParameterGroup_PushConstants_0 pushConstants {
			.scale_0 = scale,
			.offset_0 = offset,
		};
		SlangShader::GlobalParams_0 globals {
			.PushConstants_0 = &pushConstants,
			.scale2_0 = scale2,
			.offset2_0 = offset2,
			.dataSize_0 = dataSize,
			.data_0 = {
				.data = inputData.data(),
				.count = dataSize
			},
		};

		{
			SlangShader::ComputeVaryingInput shaderInput {
				.startGroupID = { 0, 0, 0 },
				.endGroupID = { (dataSize+31)/32, 1, 1 }
			};
			SlangShader::testMain(&shaderInput, nullptr, &globals);
		}
	}

	for (size_t i = 0; i < inputData.size(); i++) {
		float expected = inputData[i];
		float value = outputData[i];
		float error = abs(value - expected);
		if (abs(expected) > 1e-9)
			error /= abs(expected);
		if (error >= 1e-6) {
			std::cout << "Mismatch at index " << i << ": got " << value << ", expected " << expected << std::endl;
			std::cout << "FAILURE" << std::endl;
			return EXIT_FAILURE;
		}
	}

	std::cout << "SUCCESS" << std::endl;
	return EXIT_SUCCESS;
}