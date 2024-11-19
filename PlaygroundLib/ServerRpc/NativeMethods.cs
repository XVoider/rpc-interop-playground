using System.Runtime.InteropServices;

namespace PlaygroundLib.ServerRpc;

public static partial class NativeMethods
{
    private const string Library = "PlaygroundRpc";

    [LibraryImport(Library, EntryPoint = "server_initialize")]
    [return: MarshalAs(UnmanagedType.I1)]
    public static partial bool Initialize(Callbacks callbacks);

    [LibraryImport(Library, EntryPoint = "server_terminate")]
    [return: MarshalAs(UnmanagedType.I1)]
    public static partial bool Terminate();
}
