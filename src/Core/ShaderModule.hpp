#pragma once

#ifndef __GNUC__
#include <stacktrace>
#endif

#include <source_location>

#include "Device.hpp"
#include "MathTypes.hpp"
#include "ParameterMap.hpp"

namespace RoseEngine {

#define FindShaderPath(name) (std::filesystem::path(std::source_location::current().file_name()).parent_path() / name)

struct ShaderDescriptorBinding {
	vk::DescriptorType descriptorType = {};
	uint32_t           setIndex = 0;
	uint32_t           bindingIndex = 0;
	uint32_t           arraySize = 1;
	uint32_t           inputAttachmentIndex = {};
	bool               writable = false;

	bool operator==(const ShaderDescriptorBinding& rhs) const = default;
	bool operator!=(const ShaderDescriptorBinding& rhs) const = default;
};
struct ShaderConstantBinding {
	uint32_t offset = 0; // relative to parent
	uint32_t typeSize = 0;
	uint32_t setIndex = 0;
	uint32_t bindingIndex = 0;
	bool     pushConstant = false;

	bool operator==(const ShaderConstantBinding& rhs) const = default;
	bool operator!=(const ShaderConstantBinding& rhs) const = default;
};
struct ShaderVertexAttributeBinding {
	uint32_t location = 0;
	std::string semantic = {};
	uint32_t semanticIndex = {};

	bool operator==(const ShaderVertexAttributeBinding& rhs) const = default;
	bool operator!=(const ShaderVertexAttributeBinding& rhs) const = default;
};

using ShaderParameterBinding = ParameterMap<
	std::monostate,
	ShaderDescriptorBinding,
	ShaderConstantBinding,
	ShaderVertexAttributeBinding >;
using ShaderDefines = NameMap<std::string>;

class ShaderModule {
private:
	vk::raii::ShaderModule mModule = nullptr;
	size_t mSpirvHash = 0;

	std::string mEntryPointName = {};

	std::chrono::file_clock::time_point mCompileTime = {};
	std::vector<std::filesystem::path>  mSourceFiles = {};

	vk::ShaderStageFlagBits mStage = {};

	// Only valid for compute shaders
	uint3 mWorkgroupSize = {};

	std::vector<std::string> mEntryPointArguments = {};
	NameMap<vk::DeviceSize>  mUniformBufferSizes = {};
	ShaderParameterBinding   mRootBinding = {};

public:
	static ref<ShaderModule> Create(
		const Device& device,
		const std::filesystem::path& sourceFile,
		const std::string& entryPoint = "main",
		const std::string& profile = "sm_6_7",
		const ShaderDefines& defines = {},
		const std::vector<std::string>& compileArgs = {},
		const bool allowRetry = true);

	inline       vk::raii::ShaderModule& operator*()        { return mModule; }
	inline const vk::raii::ShaderModule& operator*() const  { return mModule; }
	inline       vk::raii::ShaderModule* operator->()       { return &mModule; }
	inline const vk::raii::ShaderModule* operator->() const { return &mModule; }

	inline vk::ShaderStageFlagBits       Stage() const { return mStage; }
	inline uint3                         WorkgroupSize() const { return mWorkgroupSize; }
	inline const ShaderParameterBinding& RootBinding() const { return mRootBinding; }
	inline const auto&                   EntryPointArguments() const { return mEntryPointArguments; }
	inline const std::string&            EntryPointName() const { return mEntryPointName; }
	inline const auto&                   SourceFiles() const { return mSourceFiles; }

	inline bool IsStale() const {
		for (const auto& dep : mSourceFiles)
			if (std::filesystem::exists(dep) && std::filesystem::last_write_time(dep) > mCompileTime)
				return true;
		return false;
	}
};

}
