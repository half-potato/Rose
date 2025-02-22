#pragma once

#include <variant>
#include "RoseEngine.hpp"
#include "Hash.hpp"

namespace RoseEngine {

using ParameterMapKey = std::variant<std::string, size_t>;

template<typename T, typename...Types>              struct one_of_t : std::false_type {};
template<typename T, typename U>                    struct one_of_t<T, U> : std::integral_constant<bool, std::convertible_to<T, U>> {};
template<typename T, typename U, typename... Types> struct one_of_t<T, U, Types...> : std::integral_constant<bool, one_of_t<T,U>::value || one_of_t<T, Types...>::value> {};
template<typename T, typename...Types>              concept one_of = one_of_t<T, Types...>::value;

template<typename...Types>
class ParameterMap {
private:
	using map_type = std::unordered_map<ParameterMapKey, ParameterMap>;
	map_type mParameters;
	std::variant<Types...> mValue;

public:
	using iterator = map_type::iterator;
	using const_iterator = map_type::const_iterator;

	inline       iterator begin() { return mParameters.begin(); }
	inline       iterator end()   { return mParameters.end(); }
	inline const_iterator begin() const { return mParameters.begin(); }
	inline const_iterator end()   const { return mParameters.end(); }

	inline iterator find(const ParameterMapKey& i) { return mParameters.find(i); }
	template<std::integral T>
	inline iterator find(const T& i) { return mParameters.find(ParameterMapKey((size_t)i)); }
	inline const_iterator find(const ParameterMapKey& i) const { return mParameters.find(i); }
	template<std::integral T>
	inline const_iterator find(const T& i) const { return mParameters.find(ParameterMapKey((size_t)i)); }

	inline size_t size() const { return mParameters.size(); }

	inline       ParameterMap& operator[](const ParameterMapKey& i) { return mParameters[i]; }
	template<std::integral T>
	inline       ParameterMap& operator[](const T& i) { return mParameters[ParameterMapKey((size_t)i)]; }
	inline const ParameterMap& at(const ParameterMapKey& i) const { return mParameters.at(i); }
	template<std::integral T>
	inline const ParameterMap& at(const T& i) const { return mParameters.at(ParameterMapKey((size_t)i)); }

	inline const std::variant<Types...>& raw_variant() const { return mValue; }

	template<one_of<Types...> T> inline bool holds_alternative() const { return std::holds_alternative<T>(mValue); }

	template<one_of<Types...> T> inline T& get() { return std::get<T>(mValue); }
	template<one_of<Types...> T> inline const T& get() const { return std::get<T>(mValue); }

	template<one_of<Types...> T> inline T* get_if() { return std::get_if<T>(&mValue); }
	template<one_of<Types...> T> inline const T* get_if() const { return std::get_if<T>(&mValue); }

	inline ParameterMap& operator=(const std::variant<Types...>& rhs) {
		mValue = rhs;
		return *this;
	}
};

}

namespace std {

inline string to_string(const RoseEngine::ParameterMapKey& rhs) {
	if (const auto* str = get_if<std::string>(&rhs))
		return *str;
	else
		return to_string(get<size_t>(rhs));
}

inline ostream& operator<<(ostream& os, const RoseEngine::ParameterMapKey& rhs) {
	if (const auto* str = get_if<std::string>(&rhs))
		return os << *str;
	else
		return os << get<size_t>(rhs);
}

}
