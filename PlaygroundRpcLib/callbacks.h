#pragma once

namespace playground
{
	using pass_and_get_string_t = char* (*)(const char* str);
	using pass_and_get_string_out_t = void (*)(const char* str, char** out_str);

	struct callbacks {
		pass_and_get_string_t pass_and_get_string = nullptr;
		pass_and_get_string_out_t pass_and_get_string_out = nullptr;
	};
}
