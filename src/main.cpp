#include <Core/WindowedApp.hpp>
#include <Scene/ObjectRenderer.hpp>
#include <Scene/TerrainRenderer.hpp>

#include <ImGuizmo.h>

using namespace RoseEngine;

int main(int argc, const char** argv) {
	WindowedApp app(std::span { argv, (size_t)argc });

	ViewportWidget widget(*app.contexts[0], {
		make_ref<TerrainRenderer>(),
		make_ref<ObjectRenderer>(),
	});

	app.AddWidget("Renderer", [&]() {
		widget.InspectorGui();
	}, true);

	app.AddWidget("Viewport", [&]() {
		widget.Render(*app.contexts[app.swapchain->ImageIndex()], app.dt);
	}, true);

	app.Run();

	app.device->Wait();

	return EXIT_SUCCESS;
}