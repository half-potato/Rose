#pragma once

#include <queue>
#include "Device.hpp"

namespace RoseEngine {

template<typename T>
struct TransientResourceCache  {
	std::queue<std::pair<T, uint64_t>> mResources;

	inline void push(T&& resource, uint64_t counterValue)      { mResources.push(std::make_pair(std::forward<T>(resource), counterValue)); }
	inline void push(const T& resource, uint64_t counterValue) { mResources.push(std::make_pair(resource, counterValue)); }

	inline bool can_pop(const Device& device) {
		if (mResources.empty())
			return false;
		else
			return mResources.front().second >= device.TimelineSemaphore().getCounterValue();
	}

	inline T pop() {
		T tmp = std::move(mResources.front().first);
		mResources.pop();
		return tmp;
	}

	inline T pop_or_create(const Device& device, auto ctor) {
		if (can_pop(device))
			return pop();
		else
			return ctor();
	}
};

}