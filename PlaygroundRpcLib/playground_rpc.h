#pragma once

#include "Stubs/playground_interface_h.h"

#pragma comment(lib, "rpcrt4.lib")

[[nodiscard]] inline RPC_CSTR rpc_str_cast(const char* str)
{
	return reinterpret_cast<RPC_CSTR>(const_cast<char*>(str));
}

namespace playground {
	constexpr const char* ENDPOINT = "playground_server";

	// https://learn.microsoft.com/en-us/windows/win32/rpc/string-binding
	constexpr const char* PROTOCOL_SEQUENCE = "ncalrpc";
}
