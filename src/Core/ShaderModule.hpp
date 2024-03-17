#pragma once

#ifndef __GNUC__
#include <stacktrace>
#endif

#include <iostream>
#include <source_location>

#include "Device.hpp"
#include "ParameterMap.hpp"

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

struct ShaderDescriptorBinding {
	vk::DescriptorType descriptorType = {};
	uint32_t           setIndex = 0;
	uint32_t           bindingIndex = 0;
	uint32_t           arraySize = 1;
	uint32_t           inputAttachmentIndex = {};
	bool               writable = false;

	inline bool operator==(const ShaderDescriptorBinding& rhs) const {
		return descriptorType == descriptorType
			&& setIndex == setIndex
			&& bindingIndex == bindingIndex
			&& arraySize == arraySize
			&& inputAttachmentIndex == inputAttachmentIndex
			&& writable == writable;
	}
	inline bool operator!=(const ShaderDescriptorBinding& rhs) const { return !operator==(rhs); }
};
struct ShaderConstantBinding {
	uint32_t offset = 0; // relative to parent
	uint32_t typeSize = 0;
	uint32_t setIndex = 0;
	uint32_t bindingIndex = 0;
	bool     pushConstant = false;

	inline bool operator==(const ShaderConstantBinding& rhs) const {
		return offset == offset && typeSize == typeSize && setIndex == rhs.setIndex && bindingIndex == rhs.bindingIndex && pushConstant == pushConstant;
	}
	inline bool operator!=(const ShaderConstantBinding& rhs) const { return !operator==(rhs); }
};

using ShaderParameterBinding = ParameterMap<std::monostate, ShaderDescriptorBinding, ShaderConstantBinding>;
using ShaderDefines = NameMap<std::string>;

struct ShaderModule {
	vk::raii::ShaderModule mModule = nullptr;
	size_t mSpirvHash = 0;

	std::chrono::file_clock::time_point mCompileTime = {};
	std::vector<std::filesystem::path>  mSourceFiles = {};

	vk::ShaderStageFlagBits mStage = {};

	// Only valid for compute shaders
	vk::Extent3D mWorkgroupSize = {};

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
		const std::vector<std::string>& compileArgs = {});

	inline       vk::raii::ShaderModule& operator*()        { return mModule; }
	inline const vk::raii::ShaderModule& operator*() const  { return mModule; }
	inline       vk::raii::ShaderModule* operator->()       { return &mModule; }
	inline const vk::raii::ShaderModule* operator->() const { return &mModule; }

	inline vk::ShaderStageFlagBits       Stage() const { return mStage; }
	inline vk::Extent3D                  WorkgroupSize() const { return mWorkgroupSize; }
	inline const ShaderParameterBinding& RootBinding() const { return mRootBinding; }
	inline const auto&                   EntryPointArguments() const { return mEntryPointArguments; }

	inline bool IsStale() const {
		for (const auto& dep : mSourceFiles)
			if (std::filesystem::last_write_time(dep) > mCompileTime)
				return true;
		return false;
	}
};

}
