#pragma once

#include <variant>
#include <ranges>
#include <vulkan/vulkan_hash.hpp>

namespace RoseEngine {

template<typename T> concept hashable = requires(T v) { { std::hash<T>()(v) } -> std::convertible_to<size_t>; };

template<hashable T>
constexpr void HashCombine(size_t& seed, T value) {
	seed ^= (std::hash<T>{}( value ) + 0x9e3779b9 + ( seed << 6 ) + ( seed >> 2 ));
}

template<hashable Tx, hashable... Ty>
constexpr size_t HashArgs(const Tx& x, const Ty&... y) {
	std::hash<Tx> hasher;
	if constexpr (sizeof...(Ty) == 0)
		return hasher(x);
	else {
		size_t seed = 0;
		HashCombine(seed, hasher(x));
		HashCombine(seed, HashArgs<Ty...>(y...));
		return seed;
	}
}

// accepts string literals
template<typename T, size_t N>
constexpr size_t HashArray(const T(& arr)[N]) {
	std::hash<T> hasher;
	if constexpr (N == 0)
		return 0;
	else if constexpr (N == 1)
		return hasher(arr[0]);
	else
		return HashArgs(HashArray<T,N-1>(arr), hasher(arr[N-1]));
}

template<std::ranges::range R> requires(hashable<std::ranges::range_value_t<R>>)
inline size_t HashRange(const R& r) {
	std::hash<std::ranges::range_value_t<R>> hasher;
	size_t seed = 0;
	for (const auto& elem : r) {
		HashCombine(seed, elem);
	}
	return seed;
}

template<hashable... Ts>
constexpr size_t HashVariant(const std::variant<Ts...>& v) {
	size_t h = std::visit([]<typename T>(const T& x){ return std::hash<T>{}(x); }, v);
	return HashArgs(v.index(), h);
}

template<hashable T0, hashable T1>
struct PairHash {
	inline size_t operator()(const std::pair<T0,T1>& v) const {
		return HashArgs(v.first, v.second);
	}
};

template<hashable... Types>
struct TupleHash {
	inline size_t operator()(const std::tuple<Types...>& v) const {
		return HashArgs<Types...>(std::get<Types>(v)...);
	}
};

template<hashable T, size_t N>
struct ArrayHash {
	constexpr size_t operator()(const std::array<T,N>& a) const {
		return HashArray<T,N>(a.data());
	}
};

template<std::ranges::range R> requires(hashable<std::ranges::range_value_t<R>>)
struct RangeHash {
	constexpr size_t operator()(const R& r) const {
		return HashRange<R>(r);
	}
};

template<typename Ty, hashable T0, hashable T1>
using PairMap = std::unordered_map<std::pair<T0, T1>, Ty, PairHash<T0, T1>>;

template<typename Ty, hashable... Types>
using TupleMap = std::unordered_map<std::tuple<Types...>, Ty, TupleHash<Types...>>;

}