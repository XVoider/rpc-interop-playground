using PlaygroundLib;
using ServerMethods = PlaygroundLib.ServerRpc.NativeMethods;
using ClientMethods = PlaygroundLib.ClientRpc.NativeMethods;

Console.InputEncoding = System.Text.Encoding.UTF8;
Console.OutputEncoding = System.Text.Encoding.UTF8;

// playground application to try things without tests

var callbacks = new Callbacks
{
    passAndGetString = (string str) => $"Callback: {str}",
    passAndGetStringOut = (string str, out string outStr) => outStr = $"Callback: {str}",
};

ServerMethods.Initialize(callbacks);

var content = ClientMethods.GetFileContent(@"Assets\history.txt", true);

ClientMethods.PassAndGetString(content, out var outStr);
Console.WriteLine(outStr);

var outStr2 = ClientMethods.PassAndGetString(content);
Console.WriteLine(outStr2);

ServerMethods.Terminate();
