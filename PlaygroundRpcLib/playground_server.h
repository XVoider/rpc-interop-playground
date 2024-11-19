#pragma once

#include "playground_rpc.h"
#include "callbacks.h"

namespace playground::server
{
	void initialize(callbacks callbacks);
	void terminate();
}
