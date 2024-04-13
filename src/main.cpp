#include <Core/WindowedApp.hpp>
#include <Render/MeshRenderer/MeshRenderer.hpp>
#include <Render/Terrain/TerrainRenderer.hpp>

#include <ImGuizmo.h>

using namespace RoseEngine;

int main(int argc, const char** argv) {
	WindowedApp app(std::span { argv, (size_t)argc });

	auto terrain = make_ref<TerrainRenderer>();

	ViewportWidget widget(*app.contexts[0], {
		terrain,
		make_ref<ObjectRenderer>(),
	});

	app.AddWidget("Renderer", [&]() {
		widget.InspectorGui(*app.contexts[app.swapchain->ImageIndex()]);
	}, true);

	app.AddWidget("Viewport", [&]() {
		widget.Render(*app.contexts[app.swapchain->ImageIndex()], app.dt);
	}, true);

	app.AddWidget("Terrain nodes", [&]() {
		terrain->NodeEditorGui();
	}, true);

	app.Run();

	app.device->Wait();

	return EXIT_SUCCESS;
}