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
	float4x4 t = transform.transform;
	float4x4 v = view.transform;
	float4x4 p = projection.transform;
	const bool changed = ImGuizmo::Manipulate(
		&v[0][0],
		&p[0][0],
		operation,
		local ? ImGuizmo::MODE::LOCAL : ImGuizmo::MODE::WORLD,
		&t[0][0],
		NULL,
		snap.has_value() ? &snap->x : NULL);
	if (changed) transform.transform = t;
	return changed;
}

inline bool InspectorGui(Transform& v) {
	ImGui::PushID(&v);
	bool changed = false;
	float4x4 tmp = transpose(v.transform);
	float matrixTranslation[3], matrixRotation[3], matrixScale[3];
	ImGuizmo::DecomposeMatrixToComponents(&tmp[0][0], matrixTranslation, matrixRotation, matrixScale);
	if (ImGui::DragFloat3("Translation", matrixTranslation, 0.01f)) changed = true;
	if (ImGui::DragFloat3("Rotation", matrixRotation, 0.05f)) changed = true;
	if (ImGui::DragFloat3("Scale", matrixScale, 0.05f)) changed = true;
	if (changed) {
		ImGuizmo::RecomposeMatrixFromComponents(matrixTranslation, matrixRotation, matrixScale, &tmp[0][0]);
		v.transform = transpose(tmp);
	}
	ImGui::PopID();
	return changed;
}

}