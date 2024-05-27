#pragma once
#include <memory>
#include <optional>

//This c++20-compatible constexpr function implementation is based off of
//a c++23 constexpr function implementation that used c++23 constexpr unique_ptr: https://www.reddit.com/r/cpp/comments/118z87c/c23_constexpr_stdfunction_in_40_loc_simplified/

template <class>
class constexpr_function;

template <class R, class... TArgs>
class constexpr_function<R(TArgs...)> {
  struct interface {
	constexpr virtual auto  operator()(TArgs...) const -> R = 0;
  };

  template <class Fn>
  struct implementation final : interface {
	constexpr implementation(Fn fn) : fn{fn} {}
	consteval implementation(const implementation<Fn>& other) {
		other = this;
	}
	consteval implementation<Fn> operator=(const implementation<Fn>&) {
		return *this;
	}
	constexpr auto operator()(TArgs... args) const -> R override {
		if constexpr ( 
	                    (std::is_trivial_v<Fn> || std::is_fundamental_v<Fn>)
	                    && (std::is_same<R,std::nullptr_t>::value||std::is_same<R,void>::value)
	                 )
	 		return;
	 	else if constexpr (sizeof...(TArgs) == 0)
	 		return fn();
	 	else
	 		return fn(args...);
	}

   private:
	Fn fn;
  };

 public:
  constexpr constexpr_function(const constexpr_function& __restrict__ other) : m_fn{other.m_fn} {}

  template <class Fn>
  constexpr constexpr_function(Fn fn) {
  	if constexpr ((std::is_trivial_v<Fn> || std::is_fundamental_v<Fn>)
	                    && (std::is_same<R,std::nullptr_t>::value)) {
	 		return;
	} else if (std::is_constant_evaluated()) {
	    if constexpr (std::is_base_of<constexpr_function, Fn>::value) {
	        this->m_fn = fn;
	    }
	} else {
	    fn_holder<implementation<Fn>>.emplace(fn);
  		this->m_fn = &(*fn_holder<implementation<Fn>>);
  	}
  }

  constexpr auto operator()(TArgs... args) const -> R {
	 if constexpr (sizeof...(TArgs) == 0)
	 	return (*m_fn)();
	 else if constexpr (sizeof...(TArgs) != 0) {
	    return (*m_fn)(args...);
	 } else {
	    __builtin_unreachable();
	 }
  }
  constexpr operator bool() {
  	return m_fn != nullptr;
  }

 private:
  template <typename T>
   static constinit inline std::optional<T> fn_holder;
   interface* m_fn{};
};

template <class> struct function_traits {};

template <class R, class B, class... TArgs>
struct function_traits<R (B::*)(TArgs...) const> {
  using type = R(TArgs...);
};

template <class F>
constexpr_function(F) -> constexpr_function<typename function_traits<decltype(&F::operator())>::type>;

//NOTE: when using a pure higher-order function w/ constexpr_function, like in the example below,
//		put CONST_ATTR on said function:
//			-- CONST_ATTR is constexpr when compiling with clang, since otherwise consteval won't work w/ clang. That's ok because clang is really good at devirtualizing constexpr higher order constexpr_functions
//			-- Vice versa for gcc   
#ifdef __clang__
	#define CONST_ATTR constexpr
#else
	#define CONST_ATTR consteval
#endif


/* example usage: https://godbolt.org/z/GbfPs6MPK
CONST_ATTR constexpr_function<int(int)>  binded(constexpr_function<int(int,int)> f) {
    auto f2 = [f](int x) {
        return f(x, 2);
    };
    return f2;
}

void run() {
    constexpr auto multiply = [](int x, int y) {
        return x * y;
    };
    auto f = binded(multiply);
    printf("%i\n", f(2));
}
*/