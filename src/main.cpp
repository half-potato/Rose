#include <Core/WindowedApp.hpp>
#include <Render/Scene/SceneRenderer.hpp>
#include <Render/Scene/SceneEditor.hpp>
#include <Render/Terrain/TerrainRenderer.hpp>

#include <ImGuizmo.h>

using namespace RoseEngine;

int main(int argc, const char** argv) {
	WindowedApp app(std::span { argv, (size_t)argc });

	auto terrain       = make_ref<TerrainRenderer>();
	auto sceneRenderer = make_ref<SceneRenderer>();
	auto sceneEditor   = make_ref<SceneEditor>(sceneRenderer);

	ViewportWidget viewport(*app.contexts[0], {
		terrain,
		sceneRenderer,
		sceneEditor
	});

	app.AddWidget("Renderers", [&]() { viewport.InspectorGui(*app.contexts[app.swapchain->ImageIndex()]); }, true);
	app.AddWidget("Viewport", [&]() { viewport.Render(*app.contexts[app.swapchain->ImageIndex()], app.dt); }, true);
	app.AddWidget("Terrain nodes", [&]() { terrain->NodeEditorWidget(); }, true);
	app.AddWidget("Scene graph", [&]() { sceneEditor->SceneGraphWidget(); }, true);
	app.AddWidget("Tools", [&]() { sceneEditor->ToolsWidget(); }, true);

	app.AddMenuItem("File", [&]() {
		if (ImGui::MenuItem("Open scene")) {
			sceneEditor->LoadScene(*app.contexts[app.swapchain->ImageIndex()]);
		}
	});

	app.Run();

	app.device->Wait();

	return EXIT_SUCCESS;
}