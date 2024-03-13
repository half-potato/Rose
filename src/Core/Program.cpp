#include "Program.hpp"

namespace RoseEngine {

Program CreateProgram(const Device& device, const std::string& sourceName, const std::string& entryPoint) {
	Program program;

	std::shared_ptr<ShaderModule> shader = std::make_shared<ShaderModule>(device, FindShaderPath(sourceName), entryPoint);

	auto pipeline = CreateComputePipeline(device, shader);

	return program;
}

}