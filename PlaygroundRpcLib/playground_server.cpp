#include "playground_server.h"

#include "../Common/defer.h"

#include <format>
#include <system_error>
#include <Windows.h>

static playground::callbacks& get_callbacks()
{
	static playground::callbacks callbacks;
	return callbacks;
}

namespace playground::server
{
	void initialize(callbacks callbacks)
	{
		if (auto status = RpcServerUseProtseqEpA(
			rpc_str_cast(PROTOCOL_SEQUENCE),
			RPC_C_PROTSEQ_MAX_REQS_DEFAULT,
			rpc_str_cast(ENDPOINT),
			nullptr /* security descriptor */); status != RPC_S_OK)
		{
			throw std::system_error(status, std::system_category(), "RpcServerUseProtseqEpA failed");
		}

		if (auto status = RpcServerRegisterIf3(
			s_playground_interface_v1_0_s_ifspec,
			nullptr /* epv manager uuid */,
			nullptr /* manager routines' entry-point vector */,
			RPC_IF_AUTOLISTEN,
			RPC_C_LISTEN_MAX_CALLS_DEFAULT,
			static_cast<unsigned int>(-1),
			nullptr /* security callback */,
			nullptr /* security descriptor */); status != RPC_S_OK)
		{
			throw std::system_error(status, std::system_category(), "RpcServerRegisterIf3 failed");
		}

		get_callbacks() = callbacks;
	}

	void terminate()
	{
		get_callbacks() = {};

		if (auto status = RpcServerUnregisterIf(s_playground_interface_v1_0_s_ifspec, nullptr, 0); status != RPC_S_OK) {
			throw std::system_error(status, std::system_category(), "RpcServerUnregisterIf failed");
		}
	}
}

error_status_t s_pass_and_get_string(
	/* [in] */ handle_t binding_handle,
	/* [string][in] */ const char* str,
	/* [string][out] */ char** out_str)
{
	std::ignore = binding_handle;
	auto* str_local = get_callbacks().pass_and_get_string(str);

	if (str_local == nullptr)
		return ERROR_SUCCESS;

	defer(CoTaskMemFree(str_local));

	const size_t buffer_size = std::strlen(str_local) + 1;

	*out_str = static_cast<char*>(MIDL_user_allocate(buffer_size));
	if (*out_str == nullptr)
		return ERROR_NOT_ENOUGH_MEMORY;

	if (auto err = memcpy_s(*out_str, buffer_size, str_local, buffer_size); err != 0)
	{
		MIDL_user_free(*out_str);
		*out_str = nullptr;
		return ERROR_INTERNAL_ERROR;
	}

	return ERROR_SUCCESS;
}
