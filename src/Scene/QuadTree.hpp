#pragma once

#include <Core/CommandContext.hpp>

namespace RoseEngine {

class MeshRenderer {
	ref<Mesh>       mesh = {};
	ref<Pipeline>   pipeline = {};
	ShaderParameter parameters = {};

	void Render() {

	}
};

}