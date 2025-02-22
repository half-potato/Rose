#pragma once

#include <Rose/Core/CommandContext.hpp>
#include "SceneNode.hpp"

namespace RoseEngine {

ref<SceneNode> LoadGLTF(CommandContext& context, const std::filesystem::path& filename);

}