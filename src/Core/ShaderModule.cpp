#include "ShaderModule.hpp"
#include "Hash.hpp"

#include <slang/slang.h>
#include <portable-file-dialogs.h>

#include <stdio.h>
#include <stdlib.h>
#include <span>

namespace RoseEngine {

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

void ReflectParameter(ShaderParameterBindings& bindings, const uint32_t setIndex, const std::string baseName, slang::VariableLayoutReflection* parameter, const uint32_t bindingIndexOffset) {
	slang::TypeReflection* type = parameter->getType();
	slang::TypeLayoutReflection* typeLayout = parameter->getTypeLayout();

	const std::string name = baseName + parameter->getName();
	const uint32_t bindingIndex = bindingIndexOffset + parameter->getBindingIndex();

	if (type->getFieldCount() == 0) {
		if (parameter->getCategory() == slang::ParameterCategory::Uniform) {
			std::string descriptorName = "$Globals" + std::to_string(setIndex);
			ShaderConstantBinding b{ (uint32_t)parameter->getOffset(), (uint32_t)typeLayout->getSize(), descriptorName };
			bindings.uniforms.emplace(name, b);
			if (!bindings.uniformBufferSizes.contains(descriptorName))
				bindings.uniformBufferSizes.emplace(descriptorName, 0);
			bindings.uniformBufferSizes[descriptorName] = std::max<size_t>(bindings.uniformBufferSizes[descriptorName], 16*((b.offset + b.typeSize + 15)/16));
			if (!bindings.descriptors.contains(descriptorName))
				bindings.descriptors.emplace(descriptorName, ShaderDescriptorBinding{
					.setIndex = setIndex,
					.descriptorType = vk::DescriptorType::eUniformBuffer
				});
		} else {
			std::vector<uint32_t> arraySize;
			if (typeLayout->getKind() == slang::TypeReflection::Kind::Array)
				arraySize.emplace_back((uint32_t)typeLayout->getTotalArrayElementCount());
			const auto access = type->getResourceAccess();
			bindings.descriptors.emplace(name, ShaderDescriptorBinding{
				.setIndex       = setIndex,
				.bindingIndex   = bindingIndex,
				.descriptorType = descriptorTypeMap.at((SlangBindingType)typeLayout->getBindingRangeType(0)),
				.arraySize      = arraySize,
				.writable       = (access == SLANG_RESOURCE_ACCESS_WRITE) || (access == SLANG_RESOURCE_ACCESS_READ_WRITE) || (access == SLANG_RESOURCE_ACCESS_APPEND)
			});
		}
	} else {
		for (uint32_t i = 0; i < type->getFieldCount(); i++)
			ReflectParameter(bindings, setIndex, name + ".", typeLayout->getFieldByIndex(i), bindingIndex);
	}
};

void ReflectBindings(ShaderParameterBindings& bindings, slang::ShaderReflection* shaderReflection) {
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
			ReflectParameter(bindings, 0, "", parameter, 0);
			break;

		case slang::ParameterCategory::PushConstantBuffer:
			for (uint32_t i = 0; i < typeLayout->getElementTypeLayout()->getFieldCount(); i++) {
				slang::VariableLayoutReflection* param_i = typeLayout->getElementTypeLayout()->getFieldByIndex(i);
				bindings.pushConstants.emplace(param_i->getVariable()->getName(), ShaderConstantBinding{
					(uint32_t)param_i->getOffset(),
					(uint32_t)param_i->getTypeLayout()->getSize(),
					{} });
			}
			break;

		case slang::ParameterCategory::SubElementRegisterSpace:
		case slang::ParameterCategory::RegisterSpace:
			for (uint32_t i = 0; i < type->getElementType()->getFieldCount(); i++)
				ReflectParameter(bindings, parameter->getBindingIndex(), std::string(parameter->getName()) + ".", typeLayout->getElementTypeLayout()->getFieldByIndex(i), 0);
			break;

		case slang::ParameterCategory::DescriptorTableSlot: {
			std::vector<uint32_t> arraySize;
			if (typeLayout->getKind() == slang::TypeReflection::Kind::Array)
				arraySize.emplace_back((uint32_t)typeLayout->getTotalArrayElementCount());
			auto access = type->getResourceAccess();
			bindings.descriptors.emplace(parameter->getName(), ShaderDescriptorBinding{
				.setIndex       = parameter->getBindingSpace(),
				.bindingIndex   = parameter->getBindingIndex() + bindingOffset,
				.descriptorType = descriptorTypeMap.at( (SlangBindingType)typeLayout->getBindingRangeType(0) ),
				.arraySize      = arraySize,
				.writable       = (access == SLANG_RESOURCE_ACCESS_WRITE) || (access == SLANG_RESOURCE_ACCESS_READ_WRITE) || (access == SLANG_RESOURCE_ACCESS_APPEND)
			});
			break;
		}
		}
	};

	for (uint32_t parameter_index = 0; parameter_index < shaderReflection->getParameterCount(); parameter_index++)
		ReflectParameterOuter( shaderReflection->getParameterByIndex(parameter_index) );

	uint32_t bindingOffset = 0;
	if (bindings.descriptors.contains("$Globals0"))
		bindingOffset = 1;

	for (uint32_t parameter_index = 0; parameter_index < shaderReflection->getEntryPointByIndex(0)->getParameterCount(); parameter_index++) {
		auto parameter = shaderReflection->getEntryPointByIndex(0)->getParameterByIndex(parameter_index);
		if (parameter->getCategory() != slang::ParameterCategory::None)
			bindings.entryPointArguments.emplace_back(parameter->getName());
		if (parameter->getCategory() == slang::ParameterCategory::Uniform) {
			// uniforms in the entry point arguments are converted to push constants by slang
			bindings.pushConstants.emplace(parameter->getVariable()->getName(), ShaderConstantBinding{
				(uint32_t)parameter->getOffset(),
				(uint32_t)parameter->getTypeLayout()->getSize(),
				{} });
		} else {
			ReflectParameterOuter( parameter, bindingOffset );
		}
	}
}

ref<ShaderModule> ShaderModule::Create(
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

	auto shader = make_ref<ShaderModule>();
	shader->mCompileTime = std::chrono::file_clock::now();

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

		shader->mSpirvHash = HashRange(spirv);
		shader->mModule = device->createShaderModule(vk::ShaderModuleCreateInfo{}.setCode(spirv));
		blob->Release();
	}

	device.SetDebugName(*shader->mModule, sourceFile.stem().string() + "/" + entryPoint);

	const int depCount = request->getDependencyFileCount();
	shader->mSourceFiles.reserve(depCount + 1);
	shader->mSourceFiles.emplace_back(sourceFile);
	for(int dep = 0; dep < depCount; dep++)
		shader->mSourceFiles.emplace_back(spGetDependencyFilePath(request, dep));

	// reflection
	{
		slang::ShaderReflection* shaderReflection = (slang::ShaderReflection*)request->getReflection();

		switch (shaderReflection->getEntryPointByIndex(0)->getStage()) {
			case SLANG_STAGE_VERTEX:         shader->mStage = vk::ShaderStageFlagBits::eVertex; break;
			case SLANG_STAGE_HULL:           shader->mStage = vk::ShaderStageFlagBits::eTessellationControl; break;
			case SLANG_STAGE_DOMAIN:         shader->mStage = vk::ShaderStageFlagBits::eTessellationEvaluation; break;
			case SLANG_STAGE_GEOMETRY:       shader->mStage = vk::ShaderStageFlagBits::eGeometry; break;
			case SLANG_STAGE_FRAGMENT:       shader->mStage = vk::ShaderStageFlagBits::eFragment; break;
			case SLANG_STAGE_COMPUTE:        shader->mStage = vk::ShaderStageFlagBits::eCompute; break;
			case SLANG_STAGE_RAY_GENERATION: shader->mStage = vk::ShaderStageFlagBits::eRaygenKHR; break;
			case SLANG_STAGE_INTERSECTION:   shader->mStage = vk::ShaderStageFlagBits::eIntersectionKHR; break;
			case SLANG_STAGE_ANY_HIT:        shader->mStage = vk::ShaderStageFlagBits::eAnyHitKHR; break;
			case SLANG_STAGE_CLOSEST_HIT:    shader->mStage = vk::ShaderStageFlagBits::eClosestHitKHR; break;
			case SLANG_STAGE_MISS:           shader->mStage = vk::ShaderStageFlagBits::eMissKHR; break;
			case SLANG_STAGE_CALLABLE:       shader->mStage = vk::ShaderStageFlagBits::eCallableKHR; break;
			case SLANG_STAGE_MESH:           shader->mStage = vk::ShaderStageFlagBits::eMeshNV; break;
			default: throw std::runtime_error("Unsupported shader stage");
		};

		if (shader->mStage == vk::ShaderStageFlagBits::eCompute) {
			SlangUInt sz[3];
			shaderReflection->getEntryPointByIndex(0)->getComputeThreadGroupSize(3, &sz[0]);
			shader->mWorkgroupSize = vk::Extent3D{ (uint32_t)sz[0], (uint32_t)sz[1], (uint32_t)sz[2] };
		}

		ReflectBindings(shader->mBindings, shaderReflection);
	}

	request->Release();
	session->Release();

	return shader;
}

}