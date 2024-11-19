#pragma once

#include "playground_rpc.h"

#include <string>

namespace playground::client
{
	handle_t connect();

	std::string pass_and_get_string(handle_t handle, const std::string& str);
}
