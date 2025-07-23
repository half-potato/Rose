#pragma once

#include "Transform.h"

#include <imgui/imgui.h>
#include <ImGuizmo.h>

namespace RoseEngine {

inline bool InspectorGui(Transform& v) {
	bool changed = false;
	float4x4 tmp = transpose(v.transform);
	float matrixTranslation[3], matrixRotation[3], matrixScale[3];
	ImGuizmo::DecomposeMatrixToComponents(&tmp[0][0], matrixTranslation, matrixRotation, matrixScale);
	if (ImGui::DragFloat3("Translation", matrixTranslation, 0.01f)) changed = true;
	if (ImGui::DragFloat3("Rotation",    matrixRotation   , 0.05f)) changed = true;
	if (ImGui::DragFloat3("Scale",       matrixScale      , 0.05f)) changed = true;
	if (changed) {
		ImGuizmo::RecomposeMatrixFromComponents(matrixTranslation, matrixRotation, matrixScale, &tmp[0][0]);
		v.transform = transpose(tmp);
	}
	return changed;
}

}