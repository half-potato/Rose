#pragma once

#include <variant>
#include "RoseEngine.hpp"
#include "Hash.hpp"

namespace RoseEngine {

template<typename T, typename...Types>              struct one_of_t : std::false_type {};
template<typename T, typename U>                    struct one_of_t<T, U> : std::integral_constant<bool, std::convertible_to<T, U>> {};
template<typename T, typename U, typename... Types> struct one_of_t<T, U, Types...> : std::integral_constant<bool, one_of_t<T,U>::value || one_of_t<T, Types...>::value> {};
template<typename T, typename...Types>              concept one_of = one_of_t<T, Types...>::value;

template<typename...Types>
class ParameterMap {
private:
	using map_type = NameMap<ParameterMap>;
	map_type mParameters;
	std::variant<Types...> mValue;

public:
	/*
	class const_iterator {
	public:
		using value_type = std::pair<std::string, const ParameterMap&>;
		NameMap<std::unique_ptr<ParameterMap>>::const_iterator it = {};

		inline const_iterator& operator++(int) const { it++; return *this; }
		inline const_iterator operator++() { const_iterator it_ = *this; operator++(); return it_; }

		inline bool operator==(const const_iterator& rhs) const { return it == rhs.it; }
		inline bool operator!=(const const_iterator& rhs) const { return it != rhs.it; }

		inline value_type operator*()       { return value_type{ it->first, *it->second }; }
		inline value_type operator*() const { return value_type{ it->first, std::reference_wrapper<const ParameterMap>(*it->second) }; }
	};
	class iterator {
	public:
		using value_type = std::pair<std::string, ParameterMap&>;
		NameMap<std::unique_ptr<ParameterMap>>::iterator it = {};

		inline iterator& operator++(int) const { it++; return *this; }
		inline iterator operator++() { iterator it_ = *this; operator++(); return it_; }

		inline bool operator==(const iterator& rhs) const { return it == rhs.it; }
		inline bool operator!=(const iterator& rhs) const { return it != rhs.it; }
		inline bool operator==(const const_iterator& rhs) const { return it == rhs.it; }
		inline bool operator!=(const const_iterator& rhs) const { return it != rhs.it; }

		inline value_type operator*()       { return value_type{ it->first, *it->second }; }
		inline value_type operator*() const { return value_type{ it->first, std::reference_wrapper<ParameterMap>(*it->second) }; }
	};
	/*/
	using iterator = map_type::iterator;
	using const_iterator = map_type::const_iterator;
	//*/
	inline       iterator begin() { return mParameters.begin(); }
	inline       iterator end()   { return mParameters.end(); }
	inline const_iterator begin() const { return mParameters.begin(); }
	inline const_iterator end()   const { return mParameters.end(); }

	inline iterator find(size_t i) { return mParameters.find(std::to_string(i)); }
	inline iterator find(const std::string& i) { return mParameters.find(i); }

	inline const_iterator find(size_t i) const { return mParameters.find(std::to_string(i)); }
	inline const_iterator find(const std::string& i) const { return mParameters.find(i); }

	inline size_t size() const { return mParameters.size(); }

	inline       ParameterMap& operator[](const std::string& i) { return mParameters[i]; }
	inline       ParameterMap& operator[](size_t i) { return mParameters[std::to_string(i)]; }
	inline const ParameterMap& at(const std::string& i) const { return mParameters.at(i); }
	inline const ParameterMap& at(size_t i) const { return mParameters.at(std::to_string(i)); }

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