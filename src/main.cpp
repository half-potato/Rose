#include <Core/WindowedApp.hpp>
#include <Render/SceneRenderer/SceneEditor.hpp>
#include <Render/Terrain/TerrainRenderer.hpp>

#include <ImGuizmo.h>

using namespace RoseEngine;

int main(int argc, const char** argv) {

	WindowedApp app({
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
		VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
		VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
		VK_KHR_RAY_QUERY_EXTENSION_NAME,
		VK_KHR_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME });

	auto sceneRenderer = make_ref<SceneRenderer>();
	auto sceneEditor   = make_ref<SceneEditor>(sceneRenderer);
	auto terrain       = make_ref<TerrainRenderer>();

	ViewportWidget viewport(*app.contexts[0], sceneRenderer, sceneEditor, terrain);

	app.AddMenuItem("File", [&]() {
		if (ImGui::MenuItem("Open scene")) {
			sceneEditor->LoadScene(*app.contexts[app.swapchain->ImageIndex()]);
		}
	});

	app.AddWidget("Renderers", [&]()     { viewport.InspectorWidget(*app.contexts[app.swapchain->ImageIndex()]); }, true);
	app.AddWidget("Viewport", [&]()      { viewport.Render(*app.contexts[app.swapchain->ImageIndex()], app.dt); }, true);
	app.AddWidget("Scene graph", [&]()   { sceneEditor->SceneGraphWidget(); }, true);
	app.AddWidget("Tools", [&]()         { sceneEditor->ToolsWidget(); }, true);
	app.AddWidget("Terrain nodes", [&]() { terrain->NodeEditorWidget(); }, true);

	app.Run();

	app.device->Wait();

	return EXIT_SUCCESS;
}