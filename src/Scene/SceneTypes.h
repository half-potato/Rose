#pragma once

#ifdef __cplusplus
#include <Core/Gui.hpp>
#include <Core/DxgiFormatConvert.h>
#define SLANG_MUTATING
#define CPP_CONST const
#endif

#ifdef __SLANG_COMPILER__
import Core.DxgiFormatConvert;
#define SLANG_MUTATING [mutating]
#define CPP_CONST
#endif

#include "Core/Bitfield.h"

namespace RoseEngine {

struct VertexAttribute {
    uint bufferOffset;
    uint packed;

    inline uint GetBufferIndex() { return BF_GET(packed, 0, 27); }
    SLANG_MUTATING inline void SetBufferIndex(uint i) { BF_SET(packed, i, 0, 27); }

    inline uint GetStride() { return BF_GET(packed, 27, 5); }
    SLANG_MUTATING inline void SetStride(uint i) { BF_SET(packed, i, 27, 5); }

#ifndef __cplusplus
    property uint bufferIndex {
        get { return GetBufferIndex(); }
        set { SetBufferIndex(newValue); }
    }
    property uint stride {
        get { return GetStride(); }
        set { SetStride(newValue); }
    }
#endif
};
struct MeshHeader {
	VertexAttribute triangles;
	VertexAttribute positions;
	VertexAttribute normals;
	VertexAttribute texcoords;
};
struct InstanceHeader {
    uint transformIndex;
	uint materialIndex;
	uint meshIndex;
	uint pad;
};

enum MaterialFlags {
	eNone        = 0,
	eAlphaCutoff = 1,
	eAlphaBlend  = 2,
	eDoubleSided = 4
};

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

	// | baseColorR   | baseColorG | baseColorB | alphaCutoff |
	// | roughness    | metallic   | specular   | clearcoat   |
	// | transmission | ior        |     emissionScale (f16)  |
	// | emissionR    | emissionG  | emissionB  | flags       |
	uint4 packed = uint4(0);

	inline float3 GetBaseColor() CPP_CONST {
		return float3(
			BF_GET_UNORM(packed[0],  0, 8),
			BF_GET_UNORM(packed[0],  8, 8),
			BF_GET_UNORM(packed[0], 16, 8) );
	}
	SLANG_MUTATING inline void SetBaseColor(const float3 newValue) {
		BF_SET_UNORM(packed[0], newValue.x,  0, 8);
		BF_SET_UNORM(packed[0], newValue.y,  8, 8);
		BF_SET_UNORM(packed[0], newValue.z, 16, 8);
	}


	inline float3 GetEmission() CPP_CONST {
		const float scale = f16tof32(BF_GET(packed[2], 16, 16));
		const float4 rgba = RoseEngine::D3DX_R8G8B8A8_UNORM_to_FLOAT4(packed[3]);
		return float3(rgba.x, rgba.y, rgba.z) * scale;
	}
	SLANG_MUTATING inline void SetEmission(float3 newValue) {
		const float scale = max(0.f, max(max(newValue.x, newValue.y), newValue.z));
		if (scale > 0) newValue /= scale;
		BF_SET(packed[2], f32tof16(scale), 16, 16);
		BF_SET(packed[3], RoseEngine::D3DX_FLOAT4_to_R8G8B8A8_UNORM(float4(newValue, 0.f)), 0, 24);
	}

	inline float GetAlphaCutoff()  CPP_CONST { return BF_GET_UNORM(packed[0], 24, 8); }
	inline float GetRoughness()    CPP_CONST { return BF_GET_UNORM(packed[1],  0, 8); }
	inline float GetMetallic()     CPP_CONST { return BF_GET_UNORM(packed[1],  8, 8); }
	inline float GetSpecular()     CPP_CONST { return BF_GET_UNORM(packed[1], 16, 8); }
	inline float GetClearcoat()    CPP_CONST { return BF_GET_UNORM(packed[1], 24, 8); }
	inline float GetTransmission() CPP_CONST { return BF_GET_UNORM(packed[2],  0, 8); }
	inline float GetIor()          CPP_CONST { return BF_GET_UNORM(packed[2],  8, 8) + 1; }
	inline uint  GetFlags()        CPP_CONST { return BF_GET(packed[3], 24, 8); }

	SLANG_MUTATING inline void SetAlphaCutoff (float newValue) { BF_SET_UNORM(packed[0], newValue,    24, 8); }
	SLANG_MUTATING inline void SetRoughness   (float newValue) { BF_SET_UNORM(packed[1], newValue,     0, 8); }
	SLANG_MUTATING inline void SetMetallic    (float newValue) { BF_SET_UNORM(packed[1], newValue,     8, 8); }
	SLANG_MUTATING inline void SetSpecular    (float newValue) { BF_SET_UNORM(packed[1], newValue,    16, 8); }
	SLANG_MUTATING inline void SetClearcoat   (float newValue) { BF_SET_UNORM(packed[1], newValue,    24, 8); }
	SLANG_MUTATING inline void SetTransmission(float newValue) { BF_SET_UNORM(packed[2], newValue,     0, 8); }
	SLANG_MUTATING inline void SetIor         (float newValue) { BF_SET_UNORM(packed[2], newValue - 1, 8, 8); }
	SLANG_MUTATING inline void SetFlags       (uint  newValue) { BF_SET(packed[3], newValue, 24, 8); }

	inline bool HasFlag(MaterialFlags flag) CPP_CONST { return (GetFlags() & (uint)flag) != 0; }
};

#ifdef __cplusplus

inline bool InspectorGui(Material<ImageView>& material) {
	bool changed = false;

	if (ImGui::Selectable("Alpha cutoff", material.HasFlag(MaterialFlags::eAlphaCutoff))) { material.SetFlags(material.GetFlags() ^ (uint)MaterialFlags::eAlphaCutoff); changed = true; }
	if (ImGui::Selectable("Alpha blend",  material.HasFlag(MaterialFlags::eAlphaBlend)))  { material.SetFlags(material.GetFlags() ^ (uint)MaterialFlags::eAlphaBlend); changed = true; }
	if (ImGui::Selectable("Double sided", material.HasFlag(MaterialFlags::eDoubleSided))) { material.SetFlags(material.GetFlags() ^ (uint)MaterialFlags::eDoubleSided); changed = true; }

	ImGui::Separator();

	{
		float3 c = material.GetBaseColor();
		if (ImGui::ColorEdit3("Base color", &c.x)) {
			material.SetBaseColor(c);
			changed = true;
		}
	}
	{
		float3 c = material.GetEmission();
		if (ImGui::ColorEdit3("Emission", &c.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float)) {
			material.SetEmission(c);
			changed = true;
		}
	}

	#define SLIDER(label, id, mn, mx) { \
		float f = material.Get ## id(); \
		if (ImGui::SliderFloat(label, &f, mn, mx)) { \
			changed = true; \
			material.Set ## id(f); \
		} \
	}

	ImGui::BeginDisabled(!material.HasFlag(MaterialFlags::eAlphaCutoff));
	SLIDER("Alpha cutoff", AlphaCutoff, 0, 1)
	ImGui::EndDisabled();
	SLIDER("Roughness",    Roughness, 0, 1)
	SLIDER("Metallic",     Metallic, 0, 1)
	SLIDER("Specular",     Specular, 0, 1)
	SLIDER("Clearcoat",    Clearcoat, 0, 1)
	SLIDER("Transmission", Transmission, 0, 1)
	SLIDER("Refraction index", Ior, 1, 2)

	#undef SLIDER

	return changed;
}

inline Material<uint> PackMaterial(const Material<ImageView>& material, std::unordered_map<ImageView, uint32_t>& imageMap) {
	auto find_or_emplace = [&](const ImageView& img) -> uint32_t {
		if (!img) return -1;
		auto it = imageMap.find(img);
		if (it == imageMap.end())
			it = imageMap.emplace(img, (uint32_t)imageMap.size()).first;
		return it->second;
	};
	Material<uint> m = {};
	m.baseColorImage    = find_or_emplace(material.baseColorImage);
	m.emissionImage     = find_or_emplace(material.emissionImage);
	m.metallicRoughness = find_or_emplace(material.metallicRoughness);
	m.bumpMap           = find_or_emplace(material.bumpMap);
	m.packed = material.packed;
	return m;
}

inline MeshHeader PackMesh(const Mesh& mesh, std::unordered_map<ref<Buffer>, uint32_t>& bufferMap) {
	auto find_or_emplace = [&](const BufferView& buf) -> uint32_t {
		if (!buf) return -1;
		auto it = bufferMap.find(buf.mBuffer);
		if (it == bufferMap.end())
			it = bufferMap.emplace(buf.mBuffer, (uint32_t)bufferMap.size()).first;
		return it->second;
	};
	MeshHeader m = {};
	m.triangles.bufferOffset = mesh.indexBuffer.mOffset;
	m.triangles.SetBufferIndex(find_or_emplace(mesh.indexBuffer));
	m.triangles.SetStride(mesh.indexSize);

	const auto& [positionsBuf, positionsLayout] = mesh.vertexAttributes.at(MeshVertexAttributeType::ePosition)[0];
	m.positions.bufferOffset = positionsBuf.mOffset + positionsLayout.offset;
	m.positions.SetBufferIndex(find_or_emplace(positionsBuf));
	m.positions.SetStride(positionsLayout.stride);

	const auto& [normalsBuf, normalsLayout] = mesh.vertexAttributes.at(MeshVertexAttributeType::eNormal)[0];
	m.normals.bufferOffset = normalsBuf.mOffset + normalsLayout.offset;
	m.normals.SetBufferIndex(find_or_emplace(normalsBuf));
	m.normals.SetStride(normalsLayout.stride);

	const auto& [texcoordsBuf, texcoordsLayout] = mesh.vertexAttributes.at(MeshVertexAttributeType::eTexcoord)[0];
	m.texcoords.bufferOffset = texcoordsBuf.mOffset + texcoordsLayout.offset;
	m.texcoords.SetBufferIndex(find_or_emplace(texcoordsBuf));
	m.texcoords.SetStride(texcoordsLayout.stride);

	return m;
}
#endif

}