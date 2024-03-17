#pragma once

#include <variant>

namespace RoseEngine {

template<typename T, typename...Types>              struct one_of_t : std::false_type {};
template<typename T, typename U>                    struct one_of_t<T, U> : std::integral_constant<bool, std::convertible_to<T, U>> {};
template<typename T, typename U, typename... Types> struct one_of_t<T, U, Types...> : std::integral_constant<bool, one_of_t<T,U>::value || one_of_t<T, Types...>::value> {};
template<typename T, typename...Types>              concept one_of = one_of_t<T, Types...>::value;

template<typename...Types>
class ParameterMap : public NameMap<ParameterMap<Types...>> {
private:
	std::variant<Types...> mValue;

public:
	inline ParameterMap<Types...>& operator[](const std::string& i)     { return NameMap<ParameterMap<Types...>>::operator[](i); }
	inline const ParameterMap<Types...>& at(const std::string& i) const { return NameMap<ParameterMap<Types...>>::at(i); }
	inline ParameterMap<Types...>& operator[](size_t i)     { return NameMap<ParameterMap<Types...>>::operator[](std::to_string(i)); }
	inline const ParameterMap<Types...>& at(size_t i) const { return NameMap<ParameterMap<Types...>>::at(std::to_string(i)); }

	template<one_of<Types...> T> inline bool holds_alternative() const { return std::holds_alternative<T>(mValue); }

	template<one_of<Types...> T> inline T& get() { return std::get<T>(mValue); }
	template<one_of<Types...> T> inline const T& get() const { return std::get<T>(mValue); }

	template<one_of<Types...> T> inline T* get_if() { return std::get_if<T>(&mValue); }
	template<one_of<Types...> T> inline const T* get_if() const { return std::get_if<T>(&mValue); }

	template<one_of<Types...> T>
	inline ParameterMap& operator=(T&& rhs) {
		mValue = rhs;
		return *this;
	}
	template<one_of<Types...> T>
	inline ParameterMap& operator=(const T& rhs) {
		mValue = rhs;
		return *this;
	}
};

}