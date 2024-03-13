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
	uint32_t mSet;
	uint32_t mBinding;
	vk::DescriptorType mDescriptorType;
	std::vector<uint32_t> mArraySize;
	uint32_t mInputAttachmentIndex;
	bool mWritable;
};
struct ShaderConstantBinding {
	uint32_t mOffset;
	uint32_t mTypeSize;
	std::string mParentDescriptor;
};
struct ShaderVariable {
	uint32_t mLocation;
	vk::Format mFormat;
	std::string mSemantic;
	uint32_t mSemanticIndex;
};

struct ShaderModule {
	vk::raii::ShaderModule mModule = nullptr;
	size_t mSpirvHash = 0;

	std::chrono::file_clock::time_point mCompileTime;
	std::vector<std::filesystem::path>  mSourceFiles;

	vk::ShaderStageFlagBits mStage;

	// Only valid for compute shaders
	vk::Extent3D mWorkgroupSize;

	NameMap<ShaderDescriptorBinding> mDescriptors;
	NameMap<ShaderConstantBinding>   mUniforms;
	NameMap<vk::DeviceSize>          mUniformBufferSizes;
	NameMap<ShaderConstantBinding>   mPushConstants;
	NameMap<ShaderVariable>          mInputVariables;
	NameMap<ShaderVariable>          mOutputVariables;
	std::vector<std::string>         mEntryPointArguments;

	inline       vk::raii::ShaderModule& operator*()        { return mModule; }
	inline const vk::raii::ShaderModule& operator*() const  { return mModule; }
	inline       vk::raii::ShaderModule* operator->()       { return &mModule; }
	inline const vk::raii::ShaderModule* operator->() const { return &mModule; }

	ShaderModule() = default;
	ShaderModule(ShaderModule&&) = default;
	ShaderModule(
		const Device& device,
		const std::filesystem::path& sourceFile,
		const std::string& entryPoint = "main",
		const std::string& profile = "sm_6_7",
		const ShaderDefines& defines = {},
		const std::vector<std::string>& compileArgs = {});

	inline bool IsStale() const {
		for (const auto& dep : mSourceFiles)
			if (std::filesystem::last_write_time(dep) > mCompileTime)
				return true;
		return false;
	}
};

}
