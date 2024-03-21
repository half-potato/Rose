#include "ShaderModule.hpp"
#include "Hash.hpp"

#include <slang/slang.h>
#include <portable-file-dialogs.h>

#include <stdio.h>
#include <stdlib.h>

namespace RoseEngine {

inline static const std::unordered_map<SlangBindingType, vk::DescriptorType> gDescriptorTypeMap = {
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

inline const char* to_string(slang::TypeReflection::Kind kind) {
	switch (kind) {
		default:
		case slang::TypeReflection::Kind::None: return "None";
		case slang::TypeReflection::Kind::Struct: return "Struct";
		case slang::TypeReflection::Kind::Array: return "Array";
		case slang::TypeReflection::Kind::Matrix: return "Matrix";
		case slang::TypeReflection::Kind::Vector: return "Vector";
		case slang::TypeReflection::Kind::Scalar: return "Scalar";
		case slang::TypeReflection::Kind::ConstantBuffer: return "ConstantBuffer";
		case slang::TypeReflection::Kind::Resource: return "Resource";
		case slang::TypeReflection::Kind::SamplerState: return "SamplerState";
		case slang::TypeReflection::Kind::TextureBuffer: return "TextureBuffer";
		case slang::TypeReflection::Kind::ShaderStorageBuffer: return "ShaderStorageBuffer";
		case slang::TypeReflection::Kind::ParameterBlock: return "ParameterBlock";
		case slang::TypeReflection::Kind::GenericTypeParameter: return "GenericTypeParameter";
		case slang::TypeReflection::Kind::Interface: return "Interface";
		case slang::TypeReflection::Kind::OutputStream: return "OutputStream";
		case slang::TypeReflection::Kind::Specialized: return "Specialized";
		case slang::TypeReflection::Kind::Feedback: return "Feedback";
		case slang::TypeReflection::Kind::Pointer: return "Pointer";
	};
};

inline const char* to_string(slang::ParameterCategory category) {
	switch (category) {
		default:
		case slang::ParameterCategory::None: return "None";
		case slang::ParameterCategory::Mixed: return "Mixed";
		case slang::ParameterCategory::ConstantBuffer: return "ConstantBuffer";
		case slang::ParameterCategory::ShaderResource: return "ShaderResource";
		case slang::ParameterCategory::UnorderedAccess: return "UnorderedAccess";
		case slang::ParameterCategory::VaryingInput: return "VaryingInput";
		case slang::ParameterCategory::VaryingOutput: return "VaryingOutput";
		case slang::ParameterCategory::SamplerState: return "SamplerState";
		case slang::ParameterCategory::Uniform: return "Uniform";
		case slang::ParameterCategory::DescriptorTableSlot: return "DescriptorTableSlot";
		case slang::ParameterCategory::SpecializationConstant: return "SpecializationConstant";
		case slang::ParameterCategory::PushConstantBuffer: return "PushConstantBuffer";
		case slang::ParameterCategory::RegisterSpace: return "RegisterSpace";
		case slang::ParameterCategory::GenericResource: return "GenericResource";
		case slang::ParameterCategory::RayPayload: return "RayPayload";
		case slang::ParameterCategory::HitAttributes: return "HitAttributes";
		case slang::ParameterCategory::CallablePayload: return "CallablePayload";
		case slang::ParameterCategory::ShaderRecord: return "ShaderRecord";
		case slang::ParameterCategory::ExistentialTypeParam: return "ExistentialTypeParam";
		case slang::ParameterCategory::ExistentialObjectParam: return "ExistentialObjectParam";
		case slang::ParameterCategory::SubElementRegisterSpace: return "SubElementRegisterSpace";
	};
};

struct ReflectParameterContext {
	uint32_t bindingSpaceOffset = 0;
	uint32_t bindingIndexOffset = 0;
	uint32_t depth = 1;
	bool     pushConstant = false;
};

void ReflectParameter(ShaderParameterBinding& parent, slang::VariableLayoutReflection& parameter, ReflectParameterContext ctx = {}) {
	std::string                  parameterName = parameter.getName();
	slang::TypeReflection*       type          = parameter.getType();
	slang::TypeLayoutReflection* typeLayout    = parameter.getTypeLayout();
	slang::ParameterCategory     category      = typeLayout->getParameterCategory();
	slang::TypeReflection::Kind  kind          = typeLayout->getKind();

	if (category == slang::ParameterCategory::None)
		return; // non-bindable parameter (e.g. thread index)

	if (category == slang::ParameterCategory::RegisterSpace || category == slang::ParameterCategory::SubElementRegisterSpace) {
		ctx.bindingSpaceOffset += parameter.getBindingIndex();
		ctx.bindingIndexOffset = 0;
	}

	if (category == slang::ParameterCategory::DescriptorTableSlot) {
		// bindable descriptor such as a texture or constant buffer
		ctx.bindingSpaceOffset += parameter.getBindingSpace();
		ctx.bindingIndexOffset += parameter.getBindingIndex();
		parent[parameterName] = ShaderDescriptorBinding{
			.descriptorType = gDescriptorTypeMap.at((SlangBindingType)typeLayout->getBindingRangeType(0)),
			.setIndex       = ctx.bindingSpaceOffset,
			.bindingIndex   = ctx.bindingIndexOffset,
			.arraySize = std::max(1u, (uint32_t)type->getTotalArrayElementCount()),
			.inputAttachmentIndex = (uint32_t)-1,
			.writable = false
		};
	}

	if (category == slang::ParameterCategory::Uniform) {
		ShaderParameterBinding& param = parent[parameterName];

		param = ShaderConstantBinding{
			.offset   = (uint32_t)parameter.getOffset(),
			.typeSize = (uint32_t)typeLayout->getSize(),
			.setIndex       = ctx.bindingSpaceOffset,
			.bindingIndex   = ctx.bindingIndexOffset,
			.pushConstant = ctx.pushConstant };

		if (kind == slang::TypeReflection::Kind::Struct) {
			for (uint32_t i = 0; i < typeLayout->getFieldCount(); i++) {
				ReflectParameter(param, *typeLayout->getFieldByIndex(i), ctx);
			}
		}
	}

	/*{
		std::cout << std::string(ctx.depth, '\t') << parameterName;
		if (parameter.getSemanticName()) std::cout << " : " << parameter.getSemanticName();
		std::cout << " | " << to_string(category);
		std::cout << " | " << to_string(kind);
		if (category == slang::ParameterCategory::Uniform) {
			// For uniform parameters the binding "space" is unused,
			// and the "index" is the byte offset of the parameter in its parent.
			std::cout << " | " << parameter.getBindingIndex() << "+" << typeLayout->getSize();
			std::cout << " | " << ctx.bindingSpaceOffset << "." << ctx.bindingIndexOffset;
		} else {
			if (category == slang::ParameterCategory::DescriptorTableSlot)
				std::cout << " | " << (parameter.getBindingSpace() + ctx.bindingSpaceOffset) << "." << (parameter.getBindingIndex() + ctx.bindingIndexOffset);
		}
		std::cout << std::endl;
		ctx.depth += 1;
	}*/

	if (category == slang::ParameterCategory::RegisterSpace || category == slang::ParameterCategory::SubElementRegisterSpace || kind == slang::TypeReflection::Kind::ConstantBuffer) {
		slang::TypeLayoutReflection* subElementType = typeLayout->getElementTypeLayout();

		ShaderParameterBinding* param;
		if (kind == slang::TypeReflection::Kind::ConstantBuffer && (subElementType->getName() == nullptr || parameterName == subElementType->getName())) {
			// unnamed cbuffer
			param = &parent;
		} else {
			param = &parent[parameterName];
			if (category == slang::ParameterCategory::RegisterSpace || category == slang::ParameterCategory::SubElementRegisterSpace)
				*param = std::monostate{};
			else
				*param = ShaderConstantBinding{ .pushConstant = ctx.pushConstant };
		}

		if (category == slang::ParameterCategory::PushConstantBuffer)
			ctx.pushConstant = true;

		for (uint32_t i = 0; i < subElementType->getFieldCount(); i++)
			ReflectParameter(*param, *subElementType->getFieldByIndex(i), ctx);
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
		std::cout << "Compiled " << sourceFile.string() << ":" << entryPoint;
		for (const auto&[d,v] : defines)
			std::cout << " -D" << d << "=" << v;
		std::cout << std::endl;
		const char* msg = request->getDiagnosticOutput();
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

		shader->mRootBinding = ShaderParameterBinding{};

		ReflectParameterContext ctx = {};

		// global parameters
		for (uint32_t parameter_index = 0; parameter_index < shaderReflection->getParameterCount(); parameter_index++) {
			slang::VariableLayoutReflection* parameter = shaderReflection->getParameterByIndex(parameter_index);
			ReflectParameter(shader->mRootBinding, *parameter, ctx);
		}

		auto entryPointReflection = shaderReflection->getEntryPointByIndex(0);
		if (entryPointReflection->getParameterCount() > 0) {
			//std::cout << entryPoint << ":" << std::endl;
			for (uint32_t parameter_index = 0; parameter_index < entryPointReflection->getParameterCount(); parameter_index++) {
				slang::VariableLayoutReflection* parameter = entryPointReflection->getParameterByIndex(parameter_index);
				if (parameter->getCategory() != slang::ParameterCategory::None)
					shader->mEntryPointArguments.emplace_back(parameter->getName());

				ReflectParameterContext ctxi = ctx;
				if (parameter->getCategory() == slang::ParameterCategory::Uniform)
					ctxi.pushConstant = true; // slang converts uniforms in the entry point to push constants
				ReflectParameter(shader->mRootBinding, *parameter, ctxi);
			}
		}
	}

	request->Release();
	session->Release();

	return shader;
}

}