#pragma once

#include <variant>
#include <vector>

#include "Buffer.hpp"
#include "Image.hpp"

namespace RoseEngine {

using BufferParameter = BufferView;
struct ImageParameter {
	ImageView              mImage;
	vk::ImageLayout        mImageLayout;
	vk::AccessFlags        mImageAccessFlags;
	ref<vk::raii::Sampler> mSampler;
};
using AccelerationStructureParameter = ref<vk::raii::AccelerationStructureKHR>;

// represents a uniform or push constant
class ConstantParameter : public std::vector<std::byte> {
public:
	template<typename T>
	inline ConstantParameter(const T& value) {
		resize(sizeof(value));
		*reinterpret_cast<T*>(data()) = value;
	}

	ConstantParameter() = default;
	ConstantParameter(ConstantParameter&&) = default;
	ConstantParameter(const ConstantParameter&) = default;
	ConstantParameter& operator=(ConstantParameter&&) = default;
	ConstantParameter& operator=(const ConstantParameter&) = default;

	template<typename T>
	inline T& get() {
		if (empty())
			resize(sizeof(T));
		return *reinterpret_cast<T*>(data());
	}

	template<typename T>
	inline const T& get() const {
		return *reinterpret_cast<const T*>(data());
	}

	template<typename T>
	inline T& operator=(const T& value) {
		resize(sizeof(value));
		return *reinterpret_cast<T*>(data()) = value;
	}
};

using ShaderParameterValue = std::variant<
	ConstantParameter,
	BufferParameter,
	ImageParameter,
	AccelerationStructureParameter
>;

class ShaderParameterValueArray : public std::vector<ShaderParameterValue> {
public:
	inline ShaderParameterValue& operator[](size_t pos) {
		if (pos >= size()) resize(pos + 1);
		return std::vector<ShaderParameterValue>::operator[](pos);
	}
	inline const ShaderParameterValue& operator[](size_t pos) const {
		return std::vector<ShaderParameterValue>::operator[](pos);
	}

	inline operator ShaderParameterValue&() { return (*this)[0]; }
	inline operator const ShaderParameterValue&() const { return (*this)[0]; }

	inline ShaderParameterValue& operator =(const ShaderParameterValue& rhs) { return (*this)[0] = rhs; }
	inline ShaderParameterValue& operator =(ShaderParameterValue&& rhs) { return (*this)[0] = rhs; }
};

class ShaderParameters : public NameMap<ShaderParameterValueArray> {
public:
	inline ShaderParameters& AddParameters(const ShaderParameters& parameters) {
		for (const auto&[name, paramArray] : parameters)
			for (size_t i = 0; i < paramArray.size(); i++)
				operator[](name)[i] = paramArray[i];
		return *this;
	}

	// assigns parameters to "id.<param name>[<param index>]"
	inline ShaderParameters& AddParameters(const std::string& id, const ShaderParameters& parameters) {
		for (const auto&[name, paramArray] : parameters)
			for (size_t i = 0; i < paramArray.size(); i++)
				operator[](id + "." + name)[i] = paramArray[i];
		return *this;
	}
};

}