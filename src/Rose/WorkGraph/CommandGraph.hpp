#pragma once

#include <iostream>
#include <stack>
#include <optional>
#include <imnodes.h>

#include <Rose/Core/CommandContext.hpp>

#include "Nodes/ResourceCopyNode.hpp"
#include "Nodes/ResourceCreateNode.hpp"
#include "Nodes/ComputeProgramNode.hpp"

namespace RoseEngine {

using CommandGraph = WorkGraph<
	ResourceCopyNode,
	ResourceCreateNode,
	ComputeProgramNode>;

}