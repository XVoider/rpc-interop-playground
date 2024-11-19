#pragma once

// Heavily inspired by:
// https://github.com/CppCon/CppCon2014 - C++11 in the Wild - Techniques from a Real Codebase
// https://www.gingerbill.org/article/2015/08/19/defer-in-cpp/

[[nodiscard]] inline constexpr auto defer_func(auto f) {
	struct defer {
		decltype(f) f;
		constexpr ~defer() noexcept { f(); }
	};
	return defer{ f };
}

#define DEFER_1(x, y) x##y
#define DEFER_2(x, y) DEFER_1(x, y)
#define DEFER_3(x)    DEFER_2(x, __COUNTER__)
#define defer(code)   auto DEFER_3(zz_defer_) = defer_func([&]{code;})
