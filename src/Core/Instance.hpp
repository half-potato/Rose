#pragma once

#include <unordered_set>

#include "RoseEngine.hpp"

namespace RoseEngine {

class Instance {
private:
	vk::raii::Context  mContext = {};
	vk::raii::Instance mInstance = nullptr;

	std::unordered_set<std::string> mExtensions = {};
	std::unordered_set<std::string> mValidationLayers = {};
	uint32_t mVulkanApiVersion = 0;

	vk::raii::DebugUtilsMessengerEXT mDebugMessenger = nullptr;

public:
	Instance() = default;
	Instance(Instance&&) = default;
	Instance(const std::vector<std::string>& extensions = {}, const std::vector<std::string>& layers = {});

	inline       vk::raii::Instance& operator*()        { return mInstance; }
	inline const vk::raii::Instance& operator*() const  { return mInstance; }
	inline       vk::raii::Instance* operator->()       { return &mInstance; }
	inline const vk::raii::Instance* operator->() const { return &mInstance; }

	inline const auto& EnabledExtensions() const { return mExtensions; }
	inline const auto& EnabledLayers() const { return mValidationLayers; }
	inline uint32_t    VulkanVersion() const { return mVulkanApiVersion; }
	inline bool        DebugMessengerEnabled() const { return *mDebugMessenger; }
};


}