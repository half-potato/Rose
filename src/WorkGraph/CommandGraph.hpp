#pragma once

#include <Core/CommandContext.hpp>
#include <iostream>
#include <stack>
#include <optional>
#include <imnodes.h>

#include <WorkGraph/Nodes/ResourceCopyNode.hpp>
#include <WorkGraph/Nodes/ResourceCreateNode.hpp>
#include <WorkGraph/Nodes/ComputeProgramNode.hpp>

namespace RoseEngine {

using CommandGraph = WorkGraph<
	ResourceCopyNode,
	ResourceCreateNode,
	ComputeProgramNode>;

}