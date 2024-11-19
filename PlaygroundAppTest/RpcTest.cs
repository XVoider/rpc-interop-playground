using PlaygroundLib;
using ServerMethods = PlaygroundLib.ServerRpc.NativeMethods;
using ClientMethods = PlaygroundLib.ClientRpc.NativeMethods;

using Moq;

namespace PlaygroundAppTest;

public class RpcTest : IDisposable
{
    private readonly Mock<ICallbacks> _callbacksMock;
    private readonly Callbacks _callbacks;

    // https://xunit.net/docs/shared-context
    public RpcTest()
    {
        _callbacksMock = new();

        _callbacks = new Callbacks
        {
            passAndGetString = _callbacksMock.Object.PassAndGetString,
            passAndGetStringOut = _callbacksMock.Object.PassAndGetStringOut,
        };

        Assert.True(ServerMethods.Initialize(_callbacks));
    }

    public void Dispose() => ServerMethods.Terminate();

    [Fact]
    public void TestPassAndGetString()
    {
        var str = "Test string";
        var expectedResult = $"Callback: {str}";

        _callbacksMock
            .Setup(mock => mock.PassAndGetString(str))
            .Returns(expectedResult);

        var result = ClientMethods.PassAndGetString(str);

        Assert.Equal(expectedResult, result);
        _callbacksMock.Verify(mock => mock.PassAndGetString(str), Times.Once());
    }
}
