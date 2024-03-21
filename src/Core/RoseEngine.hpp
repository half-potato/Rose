#pragma once

#include <chrono>
#include <memory>
#include <fstream>
#include <filesystem>
#include <vector>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <span>

#include <vulkan/vulkan_raii.hpp>
#include <vulkan/vulkan_enums.hpp>

namespace RoseEngine {

template<typename T>
using ref = std::shared_ptr<T>;

template<typename T, typename...Args>
inline ref<T> make_ref(Args&&... args) { return std::make_shared<T>(std::forward<Args>(args)...); }

template<typename T>
using NameMap = std::unordered_map<std::string, T>;

template<std::ranges::contiguous_range R>
inline R ReadFile(const std::filesystem::path& filename) {
	std::ifstream file(filename, std::ios::ate | std::ios::binary);
	if (!file.is_open()) return {};
	R dst;
	dst.resize((size_t)file.tellg()/sizeof(std::ranges::range_value_t<R>));
	if (dst.empty()) return dst;
	file.seekg(0);
	file.clear();
	file.read(reinterpret_cast<char*>(dst.data()), dst.size()*sizeof(std::ranges::range_value_t<R>));
	return dst;
}
template<std::ranges::contiguous_range R>
inline R ReadFile(const std::filesystem::path& filename, R& dst) {
	std::ifstream file(filename, std::ios::ate | std::ios::binary);
	if (!file.is_open()) return {};
	size_t sz = (size_t)file.tellg();
	file.seekg(0);
	file.clear();
	file.read(reinterpret_cast<char*>(dst.data()), sz);
	return dst;
}
template<std::ranges::contiguous_range R>
inline void WriteFile(const std::filesystem::path& filename, const R& r) {
	std::ofstream file(filename, std::ios::ate | std::ios::binary);
	file.write(reinterpret_cast<const char*>(r.data()), r.size()*sizeof(std::ranges::range_value_t<R>));
}

// returns bytes,unit
inline std::tuple<size_t, const char*> FormatBytes(size_t bytes) {
	const char* units[] { "B", "KiB", "MiB", "GiB", "TiB" };
	uint32_t i = 0;
	while (bytes > 1024 && i < std::ranges::size(units)-1) {
		bytes /= 1024;
		i++;
	}
	return std::tie(bytes, units[i]);
}
// returns number,unit
inline std::tuple<float, const char*> FormatNumber(float number) {
	const char* units[] { "", "K", "M", "B" };
	uint32_t i = 0;
	while (number > 1000 && i < std::ranges::size(units)-1) {
		number /= 1000;
		i++;
	}
	return std::tie(number, units[i]);
}

}