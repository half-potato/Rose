#pragma once

#ifdef __cplusplus
#include <Core/MathTypes.hpp>
#include <Core/Image.hpp>
#endif

namespace RoseEngine {

#ifdef __cplusplus
template<typename ImageHandle>
#else
typedef uint ImageHandle;
#endif
struct Material {
	ImageHandle baseColorImage = {};
	ImageHandle emissionImage = {};
	ImageHandle metallicRoughness = {};
	ImageHandle bumpMap = {};
	float3 baseColor = float3(1);
	float  roughness = 0;
	float3 emission = float3(0);
	float  metallic = 0;
	float  ior = 1.5;
	float  transmission = 0;
	float  clearcoat = 0;
	float  specular = 0;
};

#ifdef __cplusplus
inline Material<uint> PackMaterial(const Material<ImageView>& material, std::unordered_map<ImageView, uint32_t>& imageMap) {
	Material<uint> m = {};
	auto find_or_emplace = [&](const ImageView& img) -> uint32_t {
		if (!img) return -1;
		auto it = imageMap.find(img);
		if (it == imageMap.end())
			it = imageMap.emplace(img, (uint32_t)imageMap.size()).first;
		return it->second;
	};
	m.baseColorImage    = find_or_emplace(material.baseColorImage);
	m.emissionImage     = find_or_emplace(material.emissionImage);
	m.metallicRoughness = find_or_emplace(material.metallicRoughness);
	m.bumpMap           = find_or_emplace(material.bumpMap);
	m.baseColor    = material.baseColor;
	m.roughness    = material.roughness;
	m.emission     = material.emission;
	m.metallic     = material.metallic;
	m.ior          = material.ior;
	m.transmission = material.transmission;
	m.clearcoat    = material.clearcoat;
	m.specular     = material.specular;
	return m;
}
#endif

}