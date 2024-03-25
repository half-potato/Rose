#pragma once

#include "Transform.h"

#include <imgui/imgui.h>
#include <ImGuizmo.h>

namespace RoseEngine {

inline bool TransformGizmoGui(
	Transform& transform,
	const Transform& view,
	const Transform& projection,
	ImGuizmo::OPERATION operation = ImGuizmo::OPERATION::TRANSLATE,
	bool local = false,
	std::optional<float3> snap = std::nullopt) {
	float4x4 t = transpose(transform.transform);
	float4x4 v = transpose(view.transform);
	float4x4 p = transpose(projection.transform);
	const bool changed = ImGuizmo::Manipulate(
		&v[0][0],
		&p[0][0],
		operation,
		local ? ImGuizmo::MODE::LOCAL : ImGuizmo::MODE::WORLD,
		&t[0][0],
		NULL,
		snap.has_value() ? &snap->x : NULL);
	if (changed) transform.transform = transpose(t);
	return changed;
}

inline bool InspectorGui(Transform& v) {
	bool changed = false;
	float4x4 tmp = transpose(v.transform);
	float matrixTranslation[3], matrixRotation[3], matrixScale[3];
	ImGuizmo::DecomposeMatrixToComponents(&tmp[0][0], matrixTranslation, matrixRotation, matrixScale);
	if (ImGui::InputFloat3("Translation", matrixTranslation)) changed = true;
	if (ImGui::InputFloat3("Rotation", matrixRotation)) changed = true;
	if (ImGui::InputFloat3("Scale", matrixScale)) changed = true;
	if (changed) {
		ImGuizmo::RecomposeMatrixFromComponents(matrixTranslation, matrixRotation, matrixScale, &tmp[0][0]);
		v.transform = transpose(tmp);
	}
	return changed;
}

}