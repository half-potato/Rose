#pragma once

#include <Rose/Core/Gui.hpp>
#include <Rose/Scene/Transform.hpp>

namespace RoseEngine {

struct ViewportCamera {
	float3 cameraPos   = float3(0, 2, 2);
	float2 cameraAngle = float2(-float(M_PI)/4,0);
	float  fovY  = 50.f; // in degrees
	float  nearZ = 0.01f;

	float moveSpeed = 1.f;

	inline void DrawInspectorGui() {
		ImGui::PushID("Camera");
		ImGui::DragFloat3("Position", &cameraPos.x);
		ImGui::DragFloat2("Angle", &cameraAngle.x);
		Gui::ScalarField("Vertical field of view", &fovY);
		Gui::ScalarField("Near Z", &nearZ);
		ImGui::PopID();
	}

	inline void Update(double dt) {
		if (ImGui::IsWindowHovered()) {
			if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
				cameraAngle += -float2(ImGui::GetIO().MouseDelta.y, ImGui::GetIO().MouseDelta.x) * float(M_PI) / 1920.f;
				cameraAngle.x = clamp(cameraAngle.x, -float(M_PI/2), float(M_PI/2));
			}
		}

		if (ImGui::IsWindowFocused()) {
			if (ImGui::GetIO().MouseWheel != 0) {
				moveSpeed *= (1 + ImGui::GetIO().MouseWheel / 8);
				moveSpeed = std::max(moveSpeed, .05f);
			}

			float3 move = float3(0,0,0);
			if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_W)) move += float3( 0, 0,-1);
			if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_S)) move += float3( 0, 0, 1);
			if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_D)) move += float3( 1, 0, 0);
			if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_A)) move += float3(-1, 0, 0);
			if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_Q)) move += float3( 0,-1, 0);
			if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_E)) move += float3( 0, 1, 0);
			if (move != float3(0,0,0)) {
				move = Rotation() * normalize(move);
				if (ImGui::IsKeyDown(ImGuiKey_LeftShift))
					move *= 3.f;
				cameraPos += move * moveSpeed * float(dt);
			}
		}
	}

	inline quat Rotation() const {
		quat rx = glm::angleAxis(cameraAngle.x, float3(1,0,0));
		quat ry = glm::angleAxis(cameraAngle.y, float3(0,1,0));
		return ry * rx;
	}

	inline Transform GetCameraToWorld() const {
		return Transform::Translate(cameraPos) * Transform::Rotate(Rotation());
	}

	// aspect = width / height
	inline Transform GetProjection(float aspect) const {
		return Transform::Perspective(glm::radians(fovY), aspect, nearZ);
	}
};

}