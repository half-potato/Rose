#include "ShaderModule.hpp"
#include "Hash.hpp"

#include <slang/slang.h>
#include <portable-file-dialogs.h>

#include <stdio.h>
#include <stdlib.h>

namespace RoseEngine {

ShaderModule::ShaderModule(
	const Device& device,
	const std::filesystem::path& sourceFile,
	const std::string& entryPoint,
	const std::string& profile,
	const ShaderDefines& defines,
	const std::vector<std::string>& compileArgs) {

	if (!std::filesystem::exists(sourceFile))
		throw std::runtime_error(sourceFile.string() + " does not exist");

	slang::IGlobalSession* session;
	slang::createGlobalSession(&session);

	slang::ICompileRequest* request;
	int targetIndex, entryPointIndex;
	do { // loop to allow user to retry compilation (e.g. after fixing an error)
		session->createCompileRequest(&request);

		// process compile args

		std::vector<const char*> args;
		for (const std::string& arg : compileArgs) args.emplace_back(arg.c_str());
		if (SLANG_FAILED(request->processCommandLineArguments(args.data(), args.size())))
			std::cerr << "Warning: Failed to process compile arguments while compiling " << sourceFile.stem() << "/" << entryPoint << std::endl;

		// process defines

		targetIndex = request->addCodeGenTarget(SLANG_SPIRV);
		for (const auto&[n,d] : defines)
			request->addPreprocessorDefine(n.c_str(), d.c_str());

		// process include paths

		for (const auto dir : GetDefaultSearchPaths())
			request->addSearchPath(dir.string().c_str());

		const int translationUnitIndex = request->addTranslationUnit(SLANG_SOURCE_LANGUAGE_SLANG, nullptr);
		request->addTranslationUnitSourceFile(translationUnitIndex, sourceFile.string().c_str());

		entryPointIndex = request->addEntryPoint(translationUnitIndex, entryPoint.c_str(), SLANG_STAGE_NONE);
		request->setTargetProfile(targetIndex, session->findProfile(profile.c_str()));
		//request->setTargetFloatingPointMode(targetIndex, SLANG_FLOATING_POINT_MODE_FAST);
		request->setTargetMatrixLayoutMode(targetIndex, SLANG_MATRIX_LAYOUT_COLUMN_MAJOR);

		// compile

		SlangResult r = request->compile();
		std::cout << "Compiled " << sourceFile << "/" << entryPoint;
		for (const auto&[d,v] : defines)
			std::cout << " -D" << d << "=" << v;
		const char* msg = request->getDiagnosticOutput();
		std::cout << std::endl;
		if (msg) std::cout << msg;
		if (SLANG_FAILED(r)) {
			pfd::message n("Shader compilation failed", "Retry?", pfd::choice::yes_no);
			if (n.result() == pfd::button::yes) {
				continue;
			} else
				throw std::runtime_error(msg);
		}
		break;
	} while (true);

	mCompileTime = std::chrono::file_clock::now();

	// get spirv binary
	{
		slang::IBlob* blob;
		SlangResult r = request->getEntryPointCodeBlob(entryPointIndex, targetIndex, &blob);
		// error check is disabled because it seems to fail on warnings
		/*
		if (SLANG_FAILED(r)) {
			std::stringstream stream;
			stream << "facility 0x" << std::setfill('0') << std::setw(4) << std::hex << SLANG_GET_RESULT_FACILITY(r);
			stream << ", result 0x" << std::setfill('0') << std::setw(4) << std::hex << SLANG_GET_RESULT_CODE(r);
			const std::string msg = stream.str();
			cerr << "Error: Failed to get code blob for " << resourceName() << ": " << msg << endl;
			throw runtime_error(msg);
		}
		*/

		std::span spirv{(const uint32_t*)blob->getBufferPointer(), blob->getBufferSize()/sizeof(uint32_t)};

		mSpirvHash = HashRange(spirv);
		mModule = vk::raii::ShaderModule(*device, vk::ShaderModuleCreateInfo({}, spirv));
		blob->Release();
	}

	device.SetDebugName(*mModule, sourceFile.stem().string() + "/" + entryPoint);

	const int depCount = request->getDependencyFileCount();
	mSourceFiles.reserve(depCount + 1);
	mSourceFiles.emplace_back(sourceFile);
	for(int dep = 0; dep < depCount; dep++) {
		mSourceFiles.emplace_back(spGetDependencyFilePath(request, dep));
	}

	// reflection
	{
		slang::ShaderReflection* shaderReflection = (slang::ShaderReflection*)request->getReflection();

		static const std::unordered_map<SlangBindingType, vk::DescriptorType> descriptorTypeMap = {
			{ SLANG_BINDING_TYPE_SAMPLER, vk::DescriptorType::eSampler },
			{ SLANG_BINDING_TYPE_TEXTURE, vk::DescriptorType::eSampledImage},
			{ SLANG_BINDING_TYPE_CONSTANT_BUFFER, vk::DescriptorType::eUniformBuffer },
			{ SLANG_BINDING_TYPE_TYPED_BUFFER, vk::DescriptorType::eUniformTexelBuffer },
			{ SLANG_BINDING_TYPE_RAW_BUFFER, vk::DescriptorType::eStorageBuffer },
			{ SLANG_BINDING_TYPE_COMBINED_TEXTURE_SAMPLER, vk::DescriptorType::eCombinedImageSampler },
			{ SLANG_BINDING_TYPE_INPUT_RENDER_TARGET, vk::DescriptorType::eInputAttachment },
			{ SLANG_BINDING_TYPE_INLINE_UNIFORM_DATA, vk::DescriptorType::eInlineUniformBlock },
			{ SLANG_BINDING_TYPE_RAY_TRACING_ACCELERATION_STRUCTURE, vk::DescriptorType::eAccelerationStructureKHR },
			{ SLANG_BINDING_TYPE_MUTABLE_TETURE, vk::DescriptorType::eStorageImage },
			{ SLANG_BINDING_TYPE_MUTABLE_TYPED_BUFFER, vk::DescriptorType::eStorageTexelBuffer },
			{ SLANG_BINDING_TYPE_MUTABLE_RAW_BUFFER, vk::DescriptorType::eStorageBuffer },
		};
		static const std::unordered_map<SlangStage, vk::ShaderStageFlagBits> stageMap = {
			{ SLANG_STAGE_VERTEX, vk::ShaderStageFlagBits::eVertex },
			{ SLANG_STAGE_HULL, vk::ShaderStageFlagBits::eTessellationControl },
			{ SLANG_STAGE_DOMAIN, vk::ShaderStageFlagBits::eTessellationEvaluation },
			{ SLANG_STAGE_GEOMETRY, vk::ShaderStageFlagBits::eGeometry },
			{ SLANG_STAGE_FRAGMENT, vk::ShaderStageFlagBits::eFragment },
			{ SLANG_STAGE_COMPUTE, vk::ShaderStageFlagBits::eCompute },
			{ SLANG_STAGE_RAY_GENERATION, vk::ShaderStageFlagBits::eRaygenKHR },
			{ SLANG_STAGE_INTERSECTION, vk::ShaderStageFlagBits::eIntersectionKHR },
			{ SLANG_STAGE_ANY_HIT, vk::ShaderStageFlagBits::eAnyHitKHR },
			{ SLANG_STAGE_CLOSEST_HIT, vk::ShaderStageFlagBits::eClosestHitKHR },
			{ SLANG_STAGE_MISS, vk::ShaderStageFlagBits::eMissKHR },
			{ SLANG_STAGE_CALLABLE, vk::ShaderStageFlagBits::eCallableKHR },
			{ SLANG_STAGE_MESH, vk::ShaderStageFlagBits::eMeshNV },
		};

		mStage = stageMap.at(shaderReflection->getEntryPointByIndex(0)->getStage());
		if (mStage == vk::ShaderStageFlagBits::eCompute) {
			SlangUInt sz[3];
			shaderReflection->getEntryPointByIndex(0)->getComputeThreadGroupSize(3, &sz[0]);
			mWorkgroupSize = vk::Extent3D{ (uint32_t)sz[0], (uint32_t)sz[1], (uint32_t)sz[2] };
		}

		std::function<void(const uint32_t, const std::string, slang::VariableLayoutReflection*, const uint32_t)> ReflectParameter;
		ReflectParameter = [&](const uint32_t setIndex, const std::string baseName, slang::VariableLayoutReflection* parameter, const uint32_t bindingIndexOffset) {
			slang::TypeReflection* type = parameter->getType();
			slang::TypeLayoutReflection* typeLayout = parameter->getTypeLayout();

			const std::string name = baseName + parameter->getName();
			const uint32_t bindingIndex = bindingIndexOffset + parameter->getBindingIndex();

			if (type->getFieldCount() == 0) {
				if (parameter->getCategory() == slang::ParameterCategory::Uniform) {
					std::string descriptorName = "$Globals" + std::to_string(setIndex);
					ShaderConstantBinding b{ (uint32_t)parameter->getOffset(), (uint32_t)typeLayout->getSize(), descriptorName };
					mUniforms.emplace(name, b);
					if (!mUniformBufferSizes.contains(descriptorName))
						mUniformBufferSizes.emplace(descriptorName, 0);
					mUniformBufferSizes[descriptorName] = std::max<size_t>(mUniformBufferSizes[descriptorName], 16*((b.mOffset + b.mTypeSize + 15)/16));
					if (!mDescriptors.contains(descriptorName))
						mDescriptors.emplace(descriptorName, ShaderDescriptorBinding(setIndex, 0, vk::DescriptorType::eUniformBuffer, {}, 0, false));
				} else {
					const vk::DescriptorType descriptorType = descriptorTypeMap.at((SlangBindingType)typeLayout->getBindingRangeType(0));
					std::vector<uint32_t> arraySize;
					if (typeLayout->getKind() == slang::TypeReflection::Kind::Array)
						arraySize.emplace_back((uint32_t)typeLayout->getTotalArrayElementCount());
					auto access = type->getResourceAccess();
					bool writable = (access == SLANG_RESOURCE_ACCESS_WRITE) || (access == SLANG_RESOURCE_ACCESS_READ_WRITE) || (access == SLANG_RESOURCE_ACCESS_APPEND);
					mDescriptors.emplace(name, ShaderDescriptorBinding(setIndex, bindingIndex, descriptorType, arraySize, 0, writable));
				}
			} else {
				for (uint32_t i = 0; i < type->getFieldCount(); i++)
					ReflectParameter(setIndex, name + ".", typeLayout->getFieldByIndex(i), bindingIndex);
			}
		};

		auto ReflectParameterOuter = [&](slang::VariableLayoutReflection* parameter, uint32_t bindingOffset = 0) {
			slang::ParameterCategory     category   = parameter->getCategory();
			slang::TypeReflection*       type       = parameter->getType();
			slang::TypeLayoutReflection* typeLayout = parameter->getTypeLayout();

			switch (category) {
			default:
				std::cerr << "Warning: Unsupported resource category: " << category << std::endl;
				break;

			case slang::ParameterCategory::None:
				break;

			case slang::ParameterCategory::Uniform:
				ReflectParameter(0, "", parameter, 0);
				break;

			case slang::ParameterCategory::PushConstantBuffer:
				for (uint32_t i = 0; i < typeLayout->getElementTypeLayout()->getFieldCount(); i++) {
					slang::VariableLayoutReflection* param_i = typeLayout->getElementTypeLayout()->getFieldByIndex(i);
					mPushConstants.emplace(param_i->getVariable()->getName(), ShaderConstantBinding{
						(uint32_t)param_i->getOffset(),
						(uint32_t)param_i->getTypeLayout()->getSize(),
						{} });
				}
				break;

			case slang::ParameterCategory::SubElementRegisterSpace:
			case slang::ParameterCategory::RegisterSpace:
				for (uint32_t i = 0; i < type->getElementType()->getFieldCount(); i++)
					ReflectParameter(parameter->getBindingIndex(), std::string(parameter->getName()) + ".", typeLayout->getElementTypeLayout()->getFieldByIndex(i), 0);
				break;

			case slang::ParameterCategory::DescriptorTableSlot: {
				const SlangBindingType bindingType = (SlangBindingType)typeLayout->getBindingRangeType(0);
				const vk::DescriptorType descriptorType = descriptorTypeMap.at(bindingType);
				std::vector<uint32_t> arraySize;
				if (typeLayout->getKind() == slang::TypeReflection::Kind::Array)
					arraySize.emplace_back((uint32_t)typeLayout->getTotalArrayElementCount());
				auto access = type->getResourceAccess();
				bool writable = (access == SLANG_RESOURCE_ACCESS_WRITE) || (access == SLANG_RESOURCE_ACCESS_READ_WRITE) || (access == SLANG_RESOURCE_ACCESS_APPEND);
				mDescriptors.emplace(parameter->getName(), ShaderDescriptorBinding(parameter->getBindingSpace(), parameter->getBindingIndex() + bindingOffset, descriptorType, arraySize, 0, writable));
				break;
			}
			}
		};

		for (uint32_t parameter_index = 0; parameter_index < shaderReflection->getParameterCount(); parameter_index++) {
			ReflectParameterOuter( shaderReflection->getParameterByIndex(parameter_index) );
		}

		uint32_t bindingOffset = 0;
		if (mDescriptors.contains("$Globals0"))
			bindingOffset = 1;

		for (uint32_t parameter_index = 0; parameter_index < shaderReflection->getEntryPointByIndex(0)->getParameterCount(); parameter_index++) {
			auto parameter = shaderReflection->getEntryPointByIndex(0)->getParameterByIndex(parameter_index);

			if (parameter->getCategory() != slang::ParameterCategory::None)
				mEntryPointArguments.emplace_back(parameter->getName());

			if (parameter->getCategory() == slang::ParameterCategory::Uniform) {
				mPushConstants.emplace(parameter->getVariable()->getName(), ShaderConstantBinding{
					(uint32_t)parameter->getOffset(),
					(uint32_t)parameter->getTypeLayout()->getSize(),
					{} });
			} else if (parameter->getCategory() != slang::ParameterCategory::Uniform)
				ReflectParameterOuter( parameter, bindingOffset );
		}
	}

	request->Release();
	session->Release();
}

}