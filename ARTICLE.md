# Interoperability in Action: Calling C# Functions from C++ with IPC

## Introduction

Interoperability and inter-process communication (IPC) is a topic worth volumes of technical knowledge and esoteric issues one encounters when writing code that must interoperate or communicate with a foreign system. In Safetica, our use-case is even more complicated over what you'll generally encounter. We need to enable a program written in C++ to invoke functions in another C#-based process and receive a response.

Separately, these issues are relatively easy to solve. C# can load functions from a C++ DLL and treat them as first-class citizens. And if you need two C++ processes to communicate, you can use a remote procedure call (RPC) framework.

In context of this article, I want to show how it is possible to mix these to together, discuss implementation challenges and offer a simple proof-of-concept solution, which satisfies our needs and can be used by others as a starting point or simply as a learning material. The associated code was developed and is meant to function only on x64 Windows platform. The code in the article was simplified, omitting error handling and project structure details, but complete enough to understand the functionality. You can always browse the repository for the full picture.

In the following chapters, we explore in detail:

- [how to setup a simple communication channel using Microsoft RPC](#generating-client-and-server-stubs),
- [how to load a native DLL in C# and pass delegate callbacks](#bridging-native-and-managed-code),
- [how to write an integration test that goes from a native client to invoking a managed callback](#testing),
- [how to deal with memory management issues stemming from mixing native and managed code](#overview-of-common-memory-and-resource-management-issues),
- other quirks related to used frameworks and techniques.

## Setting up RPC communication channel

In this chapter, we build a basic RPC channel with client/server model using C and C++. A complete overview and documentation of Microsoft's RPCs can be found at [Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/rpc/rpc-start-page).

### Generating client and server stubs

In order to communicate, we need a client and a server. Both are stubs, a generated C code by MIDL (Microsoft Interface Definition Language) compiler. This code is custom generated based on C-like function declarations in an IDL file. Here is an example of an RPC interface with one function.

```idl
// playground_interface.idl
[uuid(8680233F-63F1-489B-80A5-1E69468DF64A), version(1.0)]
interface playground_interface
{
	error_status_t pass_and_get_string(
		[in] handle_t binding_handle,
		[in, string] const char* str,
		[out, string] char** out_str);
}
```

Let's look at the function more closely. The function returns an error code (a value conforming to [system error codes](https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes)) and has 3 parameters. A `binding_handle` is used to identify the client or server, `str` is an input string and `out_str` is an output string. Here we use the convention of returning error codes directly and any additional data through pointers. Notice the `string` attribute, which is necessary so the parameters are treated as zero-terminated strings.

Compilation of this interface file generates 3 files: `playground_interface_c.c`, `playground_interface_s.c` and `playground_interface_h.h`. The two source files, one for a client, the other for the server, must be added to the resulting binary, which also has to be linked against the RPC library, either static `rpcrt4.lib` or dynamic `rpcrt4.dll`.

Beyond this, the source files do not interest us. However, `playground_interface_h.h` is interesting as it declares functions and extern handles, which we need to use. Here is the real C function declaration from the header.

```c
error_status_t pass_and_get_string( 
    /* [in] */ handle_t binding_handle,
    /* [string][in] */ const unsigned char *str,
    /* [string][out] */ unsigned char **out_str);
```

There are two issues with this. The first is the default signedness of `char`. MIDL treats `char` as `unsigned` and MSVC treats it as `signed`, so if we specify `char` in IDL, the equivalent is `unsigned char` in generated stubs. There is a [compiler switch](https://learn.microsoft.com/en-us/windows/win32/midl/-char) `/char`, which controls this behavior. However, there is a better way to deal with this. We can use common Windows string types like `LPCSTR` and `LPSTR`, which deal with the signedness issue without a compiler switch and we can also omit the `string` attribute.

The second issue is that this declaration is shared by both the client and the server, which is an issue if both are supposed to be used in a single binary. The [compiler switch](https://learn.microsoft.com/en-us/windows/win32/midl/-prefix) `/prefix` can help with that, the following is the output if you set `/prefix client "c_" server "s_"`:

```c
/* client prototype */
error_status_t c_pass_and_get_string( 
    /* [in] */ handle_t binding_handle,
    /* [string][in] */ const char *str,
    /* [string][out] */ char **out_str);

/* server prototype */
error_status_t s_pass_and_get_string( 
    /* [in] */ handle_t binding_handle,
    /* [string][in] */ const char *str,
    /* [string][out] */ char **out_str);
```

The function `c_pass_and_get_string` is automatically implemented in the client source file. However, `s_pass_and_get_string` is not implemented anywhere yet. That is because the server function does the real work and we have to define what that work is. We will take care of that in the next section.

Feel free to inspect the contents of generated stubs. The client and server source files contain various glue code necessary for the RPC run-time library. Only the header file has function declarations and handles we directly use.

### Setting up a server

To process incoming remote procedure calls, we need to register our interface with the RPC run-time library.

First, we specify which protocol sequence we want to use with our server. The protocol sequence is `ncalrpc`, which means local IPC only. The endpoint (server) address is just a simple name, e.g. `playground_server`. Both will also be needed later to create an appropriate [string binding](https://learn.microsoft.com/en-us/windows/win32/rpc/string-binding).

```cpp
RpcServerUseProtseqEpA(
	rpc_str_cast("ncalrpc"), // protocol sequence
	RPC_C_PROTSEQ_MAX_REQS_DEFAULT,
	rpc_str_cast("playground_server"), // endpoint address
	nullptr /* security descriptor */);
```

At this point we can register our interface. We can specify `RPC_IF_AUTOLISTEN` to immediately start listening without blocking, so we do not need to call `RpcServerListen`, which blocks until all requests are processed. Note that server routines are executed in separate threads so some form of thread synchronization might be necessary if a shared resource is present.

```cpp
RpcServerRegisterIf3(
	s_playground_interface_v1_0_s_ifspec,
	nullptr /* epv manager uuid */,
	nullptr /* manager routines' entry-point vector */,
	RPC_IF_AUTOLISTEN,
	RPC_C_LISTEN_MAX_CALLS_DEFAULT,
	static_cast<unsigned int>(-1),
	nullptr /* security callback */,
	nullptr /* security descriptor */);
```

To stop the server, simply unregister the interface.

```cpp
RpcServerUnregisterIf(s_playground_interface_v1_0_s_ifspec, nullptr, 0);
```

The final part is defining remote procedures. We already have the declaration generated from the IDL file for our function `s_pass_and_get_string`. For now, it can be a simple function which copies the input string to the output.

```cpp
error_status_t s_pass_and_get_string(
	/* [in] */ handle_t, // client-binding handle not needed right now
	/* [string][in] */ const char* str,
	/* [string][out] */ char** out_str)
{
	if (str == nullptr)
		return ERROR_SUCCESS;

	const size_t buffer_size = std::strlen(str) + 1;

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
```

This function is rather straightforward, but there is one thing that should be pointed out and that is [memory management](https://learn.microsoft.com/en-us/windows/win32/rpc/how-memory-is-allocated-and-deallocated). If we allocate a string or are supposed to get a string, the generated stubs need to know how to deal with that. We have to specify how our memory is allocated and deallocated for client and server. For that we can simply use `malloc` and `free`.

```cpp
_Must_inspect_result_
_Ret_maybenull_ _Post_writable_byte_size_(size)
void* __RPC_USER MIDL_user_allocate(_In_ size_t size)
{
	return std::malloc(size);
}

void __RPC_USER MIDL_user_free(_Pre_maybenull_ _Post_invalid_ void* ptr)
{
	std::free(ptr);
}
```

This is the bare minimum we need for a working server which can respond to remote procedure calls on a local machine. In the next section, we set up a client connection to the server.

### Setting up a client

To connect to a server, we need to create a server-binding handle.

```cpp
RPC_CSTR string_binding = nullptr;
RpcStringBindingComposeA(
	nullptr /* uuid */,
	rpc_str_cast(PROTOCOL_SEQUENCE),
	nullptr /* network address */,
	rpc_str_cast(SERVER_NAME),
	nullptr /* options */,
	&string_binding);

handle_t binding = nullptr;
RpcBindingFromStringBindingA(string_binding, &binding);
```

Similar to the server registration step, we use protocol sequence and server name to create a server-binding handle, which has to be used for client's remote procedure calls. At this point, the client function `c_pass_and_get_string` can be used as is. Nonetheless, we should also provide an easy to use C++ wrapper.

A notable thing to look out for is [exception handling](https://learn.microsoft.com/en-us/windows/win32/rpc/exception-handling). In case something goes wrong during the call, the RPC run-time library uses structured exception handling (SEH) to notify what went wrong. The disadvantage is that it does not work well with standard C++ exception handling. In our project, we use `/EHsc`, which is the recommended [switch](https://learn.microsoft.com/en-us/cpp/build/reference/eh-exception-handling-model) for standard C++ exception handling and does not mix structured and standard exceptions. We can still catch structured exceptions with  `__try __except` statement, but it must be used in a function that does not require stack unwinding. To accommodate this, we can leverage the fact that our remote procedure calls return an error code and RPC exception codes are very similar.

```cpp
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
```

This way, we gracefully handle situations like server not running and other possible RPC exceptions. Now the client call can be used in the following way. We also need to free the memory allocated by the client stub using `MIDL_user_free`.

```cpp
std::string pass_and_get_string(handle_t handle, const std::string& str)
{
	char* out_str = nullptr;
	auto status = rpc_exception_wrapper(c_pass_and_get_string, handle, str.c_str(), &out_str);

	if (status != ERROR_SUCCESS)
		throw std::system_error(status, std::system_category(),
			                    "c_pass_and_get_string failed");

	if (out_str == nullptr)
		return {};

	defer(MIDL_user_free(out_str)); // helpful macro similar to defer in other languages
	return std::string(out_str);
}
```

With this, we have a communication channel set up between the client and server, even if they are in the same process.

## Bridging native and managed code

The next step is to integrate the server with C# code, since our ultimate goal is to invoke C# functions on the server. The first step is to create a DLL which we can load from managed code and control our RPC server.

### Creating a dynamic library for RPC functions

Exporting functions is simple, but as always, footguns are ever-present. With `/EHsc`, `extern "C"` functions are assumed to not throw C++ exceptions. Also, throwing exceptions across DLL boundaries is not exactly the best practice, not even mentioning our DLL will be loaded from managed code.

```cpp
extern "C" __declspec(dllexport) bool server_initialize()
{
	try
	{
		playground::server::initialize(); // register interface, start listening
		return true;
	}
	catch (const std::exception& e)
	{
		std::println("Error: {}", e.what());
		return false;
	}
}

extern "C" __declspec(dllexport) bool server_terminate()
{
	try
	{
		playground::server::terminate(); // unregister interface
		return true;
	}
	catch (const std::exception& e)
	{
		std::println("Error: {}", e.what());
		return false;
	}
}
```

Here we catch exception, print error message and return `bool` to notify the caller about success or failure, which is enough for our use case. If exceptions are not caught, it leads to undefined behavior and crashes, so proper error handling is mandatory on the DLL boundary.

With our DLL in place, we can now load it in managed code with platform invoke (P/Invoke).

### Loading a dynamic library in managed code

To load a DLL in C#, create a `static` `NativeMethods` class, which has methods mirroring the exported functions.

```cs
public static partial class NativeMethods
{
    private const string Library = "PlaygroundRpc";

    [LibraryImport(Library, EntryPoint = "server_initialize")]
    [return: MarshalAs(UnmanagedType.I1)]
    public static partial bool Initialize();

    [LibraryImport(Library, EntryPoint = "server_terminate")]
    [return: MarshalAs(UnmanagedType.I1)]
    public static partial bool Terminate();
}
```

In this case, we opted to use the newer `LibraryImport` attribute instead of the traditional `DllImport`. The added benefit is that code for marshalling arguments is generated at compilation time and not at run time. That is why the class is `partial`, since the body of these functions is generated for us. We also need to specify how do we want to marshal the `bool` type. Windows API also has `BOOL` type, which is an `int`, so here we specify `I1` or signed 1 byte integer, which is compatible with C++ `bool`.

One aspect I have not addressed is calling convention. On x64 Windows, there is [x64 calling convention](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention) that all functions follow by default and thus it does not have to be specified. Additionally, common conventions like `Cdecl` or `Stdcall` can be specified for 64bit code, but they are ignored by the compiler. Instead, they are treated as a [platform default calling convention](https://learn.microsoft.com/en-us/dotnet/standard/native-interop/calling-conventions). Typically, on x86 platforms these conventions are honored and it might be necessary to specify the correct one.

Using this class, `PlaygroundRpc.dll` is loaded at run-time and we can start or stop the server, all from a C# program. Another program using our client code can then send requests to the server. At the moment, the server is not doing anything interesting. Now we need to provide callbacks for our server, which can be used to invoke whatever functionality our C# program might have.

### Passing managed callbacks

When initializing the server, we need to provide function pointers that can be used in native code to invoke managed code.

First, we specify our function signature. The P/Invoke function does no need to mirror the RPC function. In this case, we do not have any error codes to pass from C# callback. We can also choose how to return the output string, either using `out` paramater or return statement.

```cs
// C++ type: void (*)(const char* str, char** out_str)
public delegate void PassAndGetString(
    [MarshalAs(UnmanagedType.LPUTF8Str)] string str,
    [MarshalAs(UnmanagedType.LPUTF8Str)] out string outStr);

// C++ type: char* (*)(const char* str)
[return: MarshalAs(UnmanagedType.LPUTF8Str)]
public delegate string PassAndGetString(
    [MarshalAs(UnmanagedType.LPUTF8Str)] string str);
```

The choice is arbitrary, but my recommendation is to avoid output paramaters, especially if there is only one output value. We will use the second version moving forward.

Next, we define a `struct` to hold `delegate` objects. We do not need a `struct` specifically, but it is more ergonomic to work with. If there is a need to add another function, it can be added to the struct instead of changing the function signatures where callbacks are passed.

```cs
[NativeMarshalling(typeof(CallbacksMarshaller))]
public struct Callbacks
{
    public PassAndGetString passAndGetString;
}
```

Now we need to define a custom marshaller for the `struct` above. Here we create a new `struct`, which holds function pointers and can be passed into native code.

```cs
[CustomMarshaller(typeof(Callbacks), MarshalMode.ManagedToUnmanagedIn, typeof(CallbacksMarshaller))]
internal static class CallbacksMarshaller
{
    internal struct CallbacksUnmanaged
    {
        internal nint passAndGetString;
    }

    internal static CallbacksUnmanaged ConvertToUnmanaged(Callbacks managed)
    {
        return new CallbacksUnmanaged
        {
            passAndGetString = Marshal.GetFunctionPointerForDelegate(managed.passAndGetString)
        };
    }
}
```

On the native side we need to define a mirror struct for `CallbacksUnmanaged`. Compared to the managed version, notice how types map to each other, `string` marshalled as UTF-8 to `char*` and `out string` to `char**`.

```cpp
namespace playground
{
	using pass_and_get_string_t = char* (*)(const char* str);

	struct callbacks {
		pass_and_get_string_t pass_and_get_string = nullptr;
	};
}
```

Notice the structs are not an exact mirror. In C# the function pointer is stored as `nint` or `IntPtr` without any type information. In C++, the function pointer type matches the C# delegate declaration and the function can be easily invoked. A better mirror would be `intptr_t`, but it is not necessary and we avoid a cast to a function pointer.

Lastly, we add the callbacks to our function parameters. For both P/Invoke,

```cs
[LibraryImport(Library, EntryPoint = "server_initialize")]
[return: MarshalAs(UnmanagedType.I1)]
public static partial bool Initialize(Callbacks callbacks);
```

and DLL export. During the server initialization, the callbacks can be stored in a global variable, which is enough for now.

```cpp
static playground::callbacks& get_callbacks()
{
	static playground::callbacks callbacks;
	return callbacks;
}

extern "C" __declspec(dllexport) bool server_initialize(playground::callbacks callbacks)
{
    // ...
    get_callbacks() = callbacks;
}
```

Finally, we can use callbacks in our server function like this.

```cpp
error_status_t s_pass_and_get_string(
	/* [in] */ handle_t binding_handle,
	/* [string][in] */ const char* str,
	/* [string][out] */ char** out_str)
{
	char* str_local = get_callbacks().pass_and_get_string(str);
	// ...
}
```

As is the theme here, memory management is paramount. The `Utf8StringMarshaller` uses COM task memory allocator. This is Windows specific, it can also be verified by looking at the marshaller's source code. On the native side, the memory given to us by the marshaller is freed by calling `CoTaskMemFree`.

At this point, we have all the building blocks in place. We have a C++ client, a C++ server, which can be loaded as DLL and initialized with callbacks from C# code. Congratulations on making it this far, but there is still one last thing left to do.

## Testing

We should test that the whole path works, from client code to managed callback. This presents a certain challenge since we mix managed and native code. Solution we chose at Safetica was to create tests in C# with xUnit, Moq and custom DLL calling the native client code.

The idea is to create a mock for callbacks so we can easily verify that the mocked callback was called when we invoke a client function from our custom client DLL.

To better imagine what we are trying to do, let's look at the final test. Server and mocked callbacks are a part of [shared context](https://xunit.net/docs/shared-context) for each test in the test class. Unfortunately, we need to again define our callback methods, this time as an interface so it can be mocked.

```cs
public interface ICallbacks
{
    string PassAndGetString(string str);
}

public class RpcTest : IDisposable
{
    private readonly Mock<ICallbacks> _callbacksMock;
    private readonly Callbacks _callbacks;

    public RpcTest()
    {
        _callbacksMock = new();
        _callbacks = new Callbacks
        {
            passAndGetString = _callbacksMock.Object.PassAndGetString,
        };
        Assert.True(ServerMethods.Initialize(_callbacks));
    }

    public void Dispose() => ServerMethods.Terminate()
}
```

Since our client only has C++ API, we need a bit creative solution to call it from C#. We can do it the same way we did DLL and P/Invokes for the server.

```cpp
extern "C" __declspec(dllexport) char* pass_and_get_string(const char* str)
{
	try
	{
		auto handle = playground::client::connect();
		defer(std::ignore = RpcBindingFree(&handle));

		auto result_str = playground::client::pass_and_get_string(handle, str);

		return alloc_co_task_string(result_str);
	}
	catch (const std::exception& e)
	{
		std::println("Error: {}", e.what());
		return nullptr;
	}
}
```

Notice that even here we utilize what we have learned so far. Exception handling in `extern` functions, passing COM memory back to managed code, which the marshaller will free.

The test structure is designed to be as simple as possible. There is very little, if any, decision-making logic. The point is to verify that all the glue code and wiring will not break. We set up how the mock should behave, call our client testing function and verify the result.

```cs
[Fact]
public void TestPassAndGetString()
{
    var str = "Test data"
    var expectedResult = $"Callback: {str}";

    _callbacksMock
        .Setup(mock => mock.PassAndGetString(str))
        .Returns(expectedResult);

    var result = ClientMethods.PassAndGetString(str);

    Assert.Equal(expectedResult, result);
    _callbacksMock.Verify(mock => mock.PassAndGetString(str), Times.Once());
}
```

This way we are able to test client code, server code and callback calls all in one go. Testing each part individually does not provide much value as it is mainly glue which makes sure that the whole channel works and normally does not contain any decision logic.

That's it! We managed to do what we set out to do. We have an RPC communication channel wrapped in modern C++. The server can be loaded as DLL and accepts callbacks to C# code. The whole path is also covered by an integration test.

The next chapters are additional content discussing technical aspects not elaborated in previous sections.

## Overview of common memory and resource management issues

A great care should be taken not to mix up various memory allocators and resource management techniques. As we have seen over the course of this article, there are many and they are not an easy landscape to navigate.

In C++ it is preferable to use RAII to manage resources. In our case memory is not an issue as we are never releasing its ownership. Note that implementation of `new` and `delete` is compiler specific, which may lead to issues when binaries are compiled with multiple compilers, but again this is not our case. Managing resources like RPC handles or RPC strings on the other hand is very interesting since those behave like C constructs. In a more complex project, it is almost mandatory to create RAII wrappers to ensure these resources are released properly. Here I chose a simpler solution, specifically a custom `defer` macro, an ad-hoc RAII wrapper.

```cpp
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
```

Shoutout to Ginger Bill for his [article](https://www.gingerbill.org/article/2015/08/19/defer-in-cpp/) and Arthur O'Dwyer for his CppCon 2014 [presentation](https://github.com/CppCon/CppCon2014/tree/master/Presentations/C%2B%2B11%20in%20the%20Wild%20-%20Techniques%20from%20a%20Real%20Codebase) which gave me this idea. Similar feature may appear in standard C++ library as scope guards, currently in [C++ standard libraries extensions, version 3](https://en.cppreference.com/w/cpp/experimental/lib_extensions_3).

Another solution is to use [Windows Implementation Libraries](https://github.com/microsoft/wil), which include many helpful RPC-related RAII wrappers and helper functions.

When passing or receiving memory in RPC client or server, we use `MIDL_user_allocate` and `MIDL_user_free`. The implementation is user defined, usually `malloc` and `free` from C, but other allocators can be used as well. In our case not every function argument needs to be allocated like this, only when the server sends memory back to the client, like a string through `return` or output parameter. Remember that client and server normally do not share the same process. The RPC runtime will transport data to potentially another memory space, so the stubs need memory allocation functions, which the user code also knows, to preserve the illusion of seamlessly invoking server code with client code.

Marshalling data from native to managed code and vice versa is another challenge akin to previously discussed RPC memory management, with the addition we also have to handle proper data representation. When dealing with `LibraryImport` or `DllImport` attribute, we also specify marshalling for unmanaged data types if needed. We have already seen that `bool` is signed 1 byte integer, but strings are more complicated.

C# `string` is UTF-16 encoded, but in C++ it is not that simple. On Windows, `std::string` is normally for OS-default ANSI code page, like Windows-1250 or Windows-1252 and `std::wstring` is normally UTF-16 encoded. In other words, bad things happen when your `std::wstring` with Turkish letters is converted to `std::string` with a western code page, which is relevant when interacting with Windows API and choosing `-A` or `-W` API. In our case `std::string` is used to hold UTF-8 encoded string, as Windows [now also supports](https://learn.microsoft.com/en-us/windows/apps/design/globalizing/use-utf8-code-page) UTF-8 (code page 65001) for processes. This is why we are using `MarshalAs(UnmanagedType.LPUTF8Str)` for strings in P/Invoke.

Another noteworthy mention is `BSTR` string type, which is used mostly for COM and interop functions, and is accompanied with its own functions like `SysAllocString`. In our case we are using COM memory allocator directly for our null-terminated UTF-8 encoded strings, since that is already the default allocator of `Utf8StringMarshaller`. For string marshalling in general, Lim Bio Liong covers similar concepts in [Returning Strings from a C++ API to C#](https://limbioliong.wordpress.com/2011/06/16/returning-strings-from-a-c-api/).

When working with garbage collector in C#, it is important to note it is non-deterministic, with interesting implications. A common mistake is not storing callback delegates anywhere, which makes them eligible for garbage collection in a near future. Remember, that we obtain function pointers from delegates, which can be invoked from the RPC server. Since memory is not reclaimed immediately, the callbacks can still work, but will fail if delegates were reclaimed by the collector. Therefore, the delegates need to be available until the server successfully terminates.

A common pattern for releasing unmanaged resources is `IDisposable` interface, which we have not really touched upon in this article. The only resource we have in C# code is the server, which does not have a handle of any kind. We merely control if the server is up or down, which has some implications for integration tests, but we do not care if the same server is up for all tests or is terminated after every test and set up again.

In a real application, the server would likely have a disposable wrapper, not shown in this article. The [.NET documentation](https://learn.microsoft.com/en-us/dotnet/api/system.idisposable) has an example on how to create it.

Here are recommended further readings on the subject of garbage collection:

 * Raymond Chen's [Everybody thinks about garbage collection the wrong way](https://devblogs.microsoft.com/oldnewthing/20100809-00/?p=13203) is a short dive into common misconceptions.
 * Eric Lippert's [When everything you know is wrong, part one](https://ericlippert.com/2015/05/18/when-everything-you-know-is-wrong-part-one/) and [part two](https://ericlippert.com/2015/05/21/when-everything-you-know-is-wrong-part-two/) explains more than you ever wanted to know about behavior of finalizers and `Dispose`.

## Summary

In this article, we have tackled the challenge of enabling communication between C++ and C# processes by implementing a proof-of-concept solution. We have explored different areas ranging from specifics of RPC API and C++ wrappers to native/managed code interoperability and finishing it with an integration test.

We have also seen many intricacies with resource management and other technicalities such as calling conventions, string encoding, data marshalling and exception handling, which can serve as a point of reference for those who encounter similar issues in the future.

The accompanying code repository contains the full source code with Visual Studio 2022 project files. Although the project is small, the build system part may be of interest to those who wonder how it is all tied together. For the most part, it is a combination of copy tasks, custom commands and project references set up in a way to minimize issues with MSBuild.

A potential next step and a key aspect which is not explored is securing RPC communication. It may not be desirable for unprivileged applications to connect to our server or otherwise interact with the channel.

Another direction is to look at the build system and try to implement it with CMake as de-facto standard build system for C++ projects. The main challenge is doing it in a way that does not interfere with developer's workflow and IDE functionality.
