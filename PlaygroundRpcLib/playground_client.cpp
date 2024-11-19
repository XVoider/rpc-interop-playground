#include "playground_client.h"

#include "../Common/defer.h"

#include <system_error>

/// __try __except must be in a function that does not require unwinding
template <class Fn, class ...Args> requires std::invocable<Fn, Args...>
[[nodiscard]] error_status_t rpc_exception_wrapper(Fn fn, Args&&... args) noexcept
{
	__try {
		return fn(std::forward<Args>(args)...);
	}
	__except (RpcExceptionFilter(RpcExceptionCode())) {
		return RpcExceptionCode();
	}
}

namespace playground::client
{
	handle_t connect()
	{
		RPC_CSTR string_binding = nullptr;
		if (auto status = RpcStringBindingComposeA(
			nullptr /* uuid */,
			rpc_str_cast(PROTOCOL_SEQUENCE),
			nullptr /* network address */,
			rpc_str_cast(ENDPOINT),
			nullptr /* options */,
			&string_binding); status != RPC_S_OK)
		{
			throw std::system_error(status, std::system_category(), "RpcStringBindingComposeA failed");
		}

		defer(std::ignore = RpcStringFreeA(&string_binding));

		handle_t binding = nullptr;
		if (auto status = RpcBindingFromStringBindingA(string_binding, &binding); status != RPC_S_OK)
		{
			throw std::system_error(status, std::system_category(), "RpcBindingFromStringBindingA failed");
		}

		return binding;
	}

	std::string pass_and_get_string(handle_t handle, const std::string& str)
	{
		char* out_str = nullptr;
		auto status = rpc_exception_wrapper(c_pass_and_get_string, handle, str.c_str(), &out_str);

		if (status != ERROR_SUCCESS)
			throw std::system_error(status, std::system_category(), "c_pass_and_get_string failed");

		if (out_str == nullptr)
			return {};

		defer(MIDL_user_free(out_str));
		return std::string(out_str);
	}
}
