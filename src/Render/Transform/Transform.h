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

	#ifdef __cplusplus
	inline static Transform Translate(const float3 v) {
		Transform t = {};
		t.transform = glm::translate(v);
		return t;
	}
	inline static Transform Scale(const float3 v) {
		Transform t = {};
		t.transform = glm::scale(v);
		return t;
	}
	inline static Transform Rotate(const quat v) {
		Transform t = {};
		t.transform = float4x4(v);
		return t;
	}
	inline static Transform Perspective(const float fovY, const float aspect, const float nearZ) {
		Transform t = {};
		t.transform = glm::infinitePerspectiveRH(fovY, aspect, nearZ);
		return t;
	}
	#endif
};

#ifdef __cplusplus
inline Transform operator*(const Transform lhs, const Transform rhs) {
	Transform r = {};
	r.transform = lhs.transform * rhs.transform;
	return r;
}
inline float4 operator*(const Transform lhs, const float4 v) {
	return lhs.transform * v;
}
#else
inline Transform operator*(const Transform lhs, const Transform rhs) {
	Transform r = {};
	r.transform = mul(lhs.transform, rhs.transform);
	return r;
}
inline float4 operator*(const Transform lhs, const float4 v) {
	return mul(lhs.transform, v);
}
#endif
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
inline Transform transpose(const Transform t) {
	Transform r = {};
	r.transform = transpose(t.transform);
	return r;
}
#endif

}