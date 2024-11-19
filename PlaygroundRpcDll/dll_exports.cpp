#include "../PlaygroundRpcLib/playground_client.h"
#include "../PlaygroundRpcLib/playground_server.h"
#include "../PlaygroundRpcLib/callbacks.h"
#include "../Common/defer.h"

#include <Windows.h>

#include <array>
#include <print>

[[nodiscard]] static char* alloc_co_task_string(std::string_view str)
{
	const size_t buffer_size = str.size() + 1;

	auto* buffer = static_cast<char*>(CoTaskMemAlloc(buffer_size));
	if (buffer == nullptr)
		throw std::bad_alloc{};

	if (auto err = memcpy_s(buffer, buffer_size, str.data(), buffer_size); err != 0)
	{
		CoTaskMemFree(buffer);
		throw std::exception{ "memcpy_s failed" };
	}

	return buffer;
}

//////////////////////////////////////////////////////////////////////////////////////////
// Server exports

extern "C" __declspec(dllexport) bool server_initialize(playground::callbacks callbacks)
{
	try {
		playground::server::initialize(callbacks);
		return true;
	}
	catch (const std::exception& e) {
		std::println("Error: {}", e.what());
		return false;
	}
}

extern "C" __declspec(dllexport) bool server_terminate()
{
	try {
		playground::server::terminate();
		return true;
	}
	catch (const std::exception& e) {
		std::println("Error: {}", e.what());
		return false;
	}
}

//////////////////////////////////////////////////////////////////////////////////////////
// Client exports (testing only)

extern "C" __declspec(dllexport) char* get_file_content(const char* filepath, bool show_message_box)
{
	try {
		auto handle = CreateFileA(
			filepath,
			GENERIC_READ,
			FILE_SHARE_READ,
			nullptr,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);

		if (handle == INVALID_HANDLE_VALUE)
			throw std::system_error(GetLastError(), std::system_category(), "CreateFileA failed");

		defer(std::ignore = CloseHandle(handle));

		constexpr size_t buffer_size = 512;
		std::array<char, buffer_size> buffer{};

		if (!ReadFile(handle, buffer.data(), buffer_size - 1, nullptr, nullptr))
			throw std::system_error(GetLastError(), std::system_category(), "ReadFile failed");

		if (show_message_box)
			MessageBoxA(nullptr, buffer.data(), "A message from PlaygroundRpc", MB_OK);

		return alloc_co_task_string(buffer.data());
	}
	catch (const std::exception& e) {
		std::println("Error: {}", e.what());
		return nullptr;
	}
}

extern "C" __declspec(dllexport) void pass_and_get_string_out(const char* str, char** out_str)
{
	try {
		if (out_str == nullptr)
			throw std::invalid_argument{ "out_str cannot be null" };

		auto handle = playground::client::connect();
		defer(std::ignore = RpcBindingFree(&handle));

		auto result_str = playground::client::pass_and_get_string(handle, str);

		*out_str = alloc_co_task_string(result_str);
	}
	catch (const std::exception& e) {
		std::println("Error: {}", e.what());
	}
}

extern "C" __declspec(dllexport) char* pass_and_get_string(const char* str)
{
	try {
		auto handle = playground::client::connect();
		defer(std::ignore = RpcBindingFree(&handle));

		auto result_str = playground::client::pass_and_get_string(handle, str);

		return alloc_co_task_string(result_str);
	}
	catch (const std::exception& e) {
		std::println("Error: {}", e.what());
		return nullptr;
	}
}
