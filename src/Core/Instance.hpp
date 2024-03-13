#pragma once

#include <unordered_set>

#include "RoseEngine.hpp"

namespace RoseEngine {

class Instance {
public:
	static bool sDisableDebugCallback;

	vk::raii::Context  mContext;
	vk::raii::Instance mInstance = nullptr;

	std::unordered_set<std::string> mExtensions;
	std::unordered_set<std::string> mValidationLayers;
	uint32_t mVulkanApiVersion = 0;

	vk::raii::DebugUtilsMessengerEXT mDebugMessenger = nullptr;

	Instance() = default;
	Instance(Instance&&) = default;
	Instance(const std::vector<std::string>& extensions = {}, const std::vector<std::string>& layers = {});

	inline       vk::raii::Instance& operator*()        { return mInstance; }
	inline const vk::raii::Instance& operator*() const  { return mInstance; }
	inline       vk::raii::Instance* operator->()       { return &mInstance; }
	inline const vk::raii::Instance* operator->() const { return &mInstance; }
};


}