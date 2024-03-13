#pragma once

#include "Core/Device.hpp"

namespace RoseEngine {

struct AppContext {
	std::unique_ptr<Instance> mInstance;
	std::vector<Device> mDevices;
};

AppContext CreateContext();

}