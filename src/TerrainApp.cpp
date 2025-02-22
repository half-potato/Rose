#include <Core/WindowedApp.hpp>
#include <Render/SceneRenderer/SceneEditor.hpp>
#include <Render/Terrain/TerrainRenderer.hpp>

#include <ImGuizmo.h>

using namespace RoseEngine;

int main(int argc, const char** argv) {

	WindowedApp app("Terrain", {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_KHR_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME,
		VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME });

	auto terrain = make_ref<TerrainRenderer>();

	ViewportWidget viewport(*app.contexts[0], terrain);

	app.AddWidget("Renderers", [&]()     { viewport.InspectorWidget(*app.contexts[app.swapchain->ImageIndex()]); }, true);
	app.AddWidget("Viewport", [&]()      { viewport.Render(*app.contexts[app.swapchain->ImageIndex()], app.dt); }, true);

	app.Run();

	app.device->Wait();

	return EXIT_SUCCESS;
}