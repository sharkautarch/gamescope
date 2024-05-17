#pragma once
#include <memory>
#include <optional>

//This constexpr function implementation is based off of
//a c++23 constexpr function implementation that used c++23 constexpr unique_ptr: https://www.reddit.com/r/cpp/comments/118z87c/c23_constexpr_stdfunction_in_40_loc_simplified/
template <class>
class constexpr_function;

template <class R, class... TArgs>
class constexpr_function<R(TArgs...)> {
  struct interface {
    constexpr virtual auto operator()(TArgs...) -> R = 0;
    constexpr virtual ~interface() = default;
  };

  template <class Fn>
  struct implementation final : interface {
    constexpr implementation(Fn fn) : fn{fn} {}
    constexpr ~implementation() = default;
    constexpr implementation(const implementation<Fn>& other) {
    	other = this;
    }
    constexpr implementation<Fn> operator=(const implementation<Fn>&) {
    	return *this;
    }
    constexpr auto operator()(TArgs... args) -> R {
    	if constexpr (std::is_trivial_v<Fn> || std::is_fundamental_v<Fn>)
     		return;
     	else if constexpr (sizeof...(TArgs) == 0)
     		return fn();
     	else
     		return fn(args...);
    }

   private:
    Fn fn{};
  };

 public:
  constexpr ~constexpr_function() {
    if (!std::is_constant_evaluated() && fn != nullptr) {
    	fn->~interface();
    	fn=nullptr;
    }
  }

  template <class Fn>
  #ifndef __clang__
  consteval
  #else
  constexpr
  #endif
  constexpr_function(Fn fn) {
  	if constexpr (std::is_trivial_v<Fn> || std::is_fundamental_v<Fn>) {
     		return;
    } else {
    	#ifndef __clang__
    	static_assert(! (fn_holder<implementation<Fn>>.has_value()) );
    	#endif
    	fn_holder<implementation<Fn>>.emplace(fn);
  		this->fn = &(*fn_holder<implementation<Fn>>);
  	}
  }
  constexpr auto operator()(TArgs... args) const -> R {
     if constexpr (sizeof...(TArgs) == 0)
     	return (*fn)();
    return (*fn)(args...);
  }
  constexpr operator bool() {
  	return fn != nullptr;
  }
 private:
  template <typename T>
  static inline std::optional<T> fn_holder = std::nullopt;
  interface* fn{};
};

template <class> struct function_traits {};

template <class R, class B, class... TArgs>
struct function_traits<R (B::*)(TArgs...) const> {
  using type = R(TArgs...);
};

template <class F>
constexpr_function(F) -> constexpr_function<typename function_traits<decltype(&F::operator())>::type>;