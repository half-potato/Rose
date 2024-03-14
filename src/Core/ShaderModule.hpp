#pragma once

#ifndef __GNUC__
#include <stacktrace>
#endif

#include <source_location>

#include "Device.hpp"

namespace RoseEngine {

inline std::vector<std::filesystem::path> GetDefaultSearchPaths() {
	const char* file = std::source_location::current().file_name();
	auto core = std::filesystem::path( file ).parent_path();
	auto src  = core.parent_path();
	return {
		src,
		src.parent_path() / "thirdparty"
	};
}

#ifdef __GNUC__

#define FindShaderPath(name) (std::filesystem::path(std::source_location::current().file_name()).parent_path() / name)

#else

inline std::filesystem::path FindShaderPath(const std::string& name) {
	// search in the folder of the caller's source file
	const std::string& callerSrc = std::stacktrace::current()[1].source_file();
	return std::filesystem::path(callerSrc).parent_path() / name;
}

#endif

using ShaderDefines = NameMap<std::string>;

struct ShaderDescriptorBinding {
	uint32_t setIndex = 0;
	uint32_t bindingIndex = 0;
	vk::DescriptorType descriptorType = {};
	std::vector<uint32_t> arraySize = {};
	uint32_t inputAttachmentIndex = {};
	bool writable = false;
};
struct ShaderConstantBinding {
	uint32_t offset = 0;
	uint32_t typeSize = 0;
	std::string parentDescriptor = {};
};
struct ShaderVariable {
	uint32_t location = 0;
	vk::Format format = {};
	std::string semantic = {};
	uint32_t semanticIndex = 0;
};

struct ShaderParameterBindings {
	NameMap<ShaderDescriptorBinding> descriptors = {};
	NameMap<ShaderConstantBinding>   uniforms = {};
	NameMap<vk::DeviceSize>          uniformBufferSizes = {};
	NameMap<ShaderConstantBinding>   pushConstants = {};
	NameMap<ShaderVariable>          inputVariables = {};
	NameMap<ShaderVariable>          outputVariables = {};
	std::vector<std::string>         entryPointArguments = {};
};

struct ShaderModule {
	vk::raii::ShaderModule mModule = nullptr;
	size_t mSpirvHash = 0;

	std::chrono::file_clock::time_point mCompileTime = {};
	std::vector<std::filesystem::path>  mSourceFiles = {};

	vk::ShaderStageFlagBits mStage = {};

	// Only valid for compute shaders
	vk::Extent3D mWorkgroupSize = {};

	ShaderParameterBindings mBindings;

public:
	inline       vk::raii::ShaderModule& operator*()        { return mModule; }
	inline const vk::raii::ShaderModule& operator*() const  { return mModule; }
	inline       vk::raii::ShaderModule* operator->()       { return &mModule; }
	inline const vk::raii::ShaderModule* operator->() const { return &mModule; }

	inline vk::ShaderStageFlagBits Stage() const { return mStage; }
	inline vk::Extent3D            WorkgroupSize() const { return mWorkgroupSize; }
	inline const auto&             Bindings() const { return mBindings; }

	inline bool IsStale() const {
		for (const auto& dep : mSourceFiles)
			if (std::filesystem::last_write_time(dep) > mCompileTime)
				return true;
		return false;
	}

	static ref<ShaderModule> Create(
		const Device& device,
		const std::filesystem::path& sourceFile,
		const std::string& entryPoint = "main",
		const std::string& profile = "sm_6_7",
		const ShaderDefines& defines = {},
		const std::vector<std::string>& compileArgs = {});
};

}
