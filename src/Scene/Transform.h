#pragma once

#ifdef __cplusplus
#include <Core/MathTypes.hpp>
#define CPP_CONST const
#else
#define CPP_CONST
#endif

namespace RoseEngine {

struct Transform {
	float4x4 transform;

	inline static Transform Identity() {
		Transform t = {};
		t.transform = float4x4(
			1,0,0,0,
			0,1,0,0,
			0,0,1,0,
			0,0,0,1 );
		return t;
	}

	inline static Transform Translate(const float3 v) {
		Transform t = {};
		t.transform = float4x4(
			1,0,0,v.x,
			0,1,0,v.y,
			0,0,1,v.z,
			0,0,0,1 );
		return t;
	}
	inline static Transform Scale(const float3 v) {
		Transform t = {};
		t.transform = float4x4(
			v.x,0,0,0,
			0,v.y,0,0,
			0,0,v.z,0,
			0,0,0,1 );
		return t;
	}
};

inline Transform operator*(const Transform lhs, const Transform rhs) {
	Transform r = {};
	r.transform = mul(lhs.transform, rhs.transform);
	return r;
}
inline float4 operator*(const Transform lhs, const float4 v) {
	return mul(lhs.transform, v);
}
inline float3 operator*(const Transform lhs, const float3 v) {
	float4 h = lhs * float4(v, 1);
	if (h.w > 0)
		h /= h.w;
	return float3(h.x, h.y, h.z);
}

#ifdef __cplusplus
inline Transform inverse(const Transform t) {
	Transform r = {};
	r.transform = inverse(t.transform);
	return r;
}
#endif

}