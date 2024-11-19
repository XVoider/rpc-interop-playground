using System.Runtime.InteropServices;
using System.Runtime.InteropServices.Marshalling;

namespace PlaygroundLib;

public interface ICallbacks
{
    string PassAndGetString(string str);
    void PassAndGetStringOut(string str, out string outStr);
}

[return: MarshalAs(UnmanagedType.LPUTF8Str)]
public delegate string PassAndGetString(
    [MarshalAs(UnmanagedType.LPUTF8Str)] string str);

public delegate void PassAndGetStringOut(
    [MarshalAs(UnmanagedType.LPUTF8Str)] string str,
    [MarshalAs(UnmanagedType.LPUTF8Str)] out string outStr);

[NativeMarshalling(typeof(CallbacksMarshaller))]
public struct Callbacks
{
    public PassAndGetString passAndGetString;
    public PassAndGetStringOut passAndGetStringOut;
}

[CustomMarshaller(typeof(Callbacks), MarshalMode.ManagedToUnmanagedIn, typeof(CallbacksMarshaller))]
internal static class CallbacksMarshaller
{
    /// <summary>Mimics the unmanaged callbacks struct at a binary level</summary>
    internal struct CallbacksUnmanaged
    {
        internal nint passAndGetString;
        internal nint passAndGetStringOut;
    }

    internal static CallbacksUnmanaged ConvertToUnmanaged(Callbacks managed)
    {
        return new CallbacksUnmanaged
        {
            passAndGetString = Marshal.GetFunctionPointerForDelegate(managed.passAndGetString),
            passAndGetStringOut = Marshal.GetFunctionPointerForDelegate(managed.passAndGetStringOut),
        };
    }
}
