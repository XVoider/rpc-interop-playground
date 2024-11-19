using System.Runtime.InteropServices;

namespace PlaygroundLib.ClientRpc;

public static partial class NativeMethods
{
    private const string Library = "PlaygroundRpc";

    [LibraryImport(Library, EntryPoint = "get_file_content", StringMarshalling = StringMarshalling.Utf8)]
    public static partial string GetFileContent(
        string filepath, 
        [MarshalAs(UnmanagedType.I1)] bool showMessageBox);

    [LibraryImport(Library, EntryPoint = "pass_and_get_string_out", StringMarshalling = StringMarshalling.Utf8)]
    public static partial void PassAndGetString(string str, out string outStr);

    [LibraryImport(Library, EntryPoint = "pass_and_get_string", StringMarshalling = StringMarshalling.Utf8)]
    public static partial string PassAndGetString(string str);
}
