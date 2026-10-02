using System.Diagnostics;
using System.Reflection;
using Csnz.Launcher;

// A disposable child models the server's stdin shutdown contract. No game,
// account store, server database, or production process is accessed.
if (args.Contains("--child"))
{
    Console.ReadLine();
    await Task.Delay(800);
    return;
}

int passed = 0;
void Check(bool ok, string name)
{
    if (!ok) throw new Exception("FAIL: " + name);
    Console.WriteLine("PASS: " + name); passed++;
}
Process Child()
{
    var start = new ProcessStartInfo(Environment.ProcessPath!, "--child")
    { UseShellExecute = false, CreateNoWindow = true, WindowStyle = ProcessWindowStyle.Hidden, RedirectStandardInput = true };
    return Process.Start(start) ?? throw new Exception("Cannot start isolated child");
}
ServerManager Own(Process child)
{
    var manager = new ServerManager();
    typeof(ServerManager).GetField("owned", BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(manager, child);
    return manager;
}

using (var child = Child())
using (var manager = Own(child))
{
    var states = new List<ServerState>();
    manager.Changed += s => states.Add(s.State);
    var stop = manager.StopOwnedAsync(CancellationToken.None);
    Check(manager.Status.State == ServerState.Stopping && manager.Status.Owned && !stop.IsCompleted,
        "Stopping is published while the owned child is still exiting");
    await manager.RefreshAsync(new LauncherSettings(), CancellationToken.None);
    Check(manager.Status.State == ServerState.Stopping, "refresh cannot overwrite an active shutdown state");
    await stop;
    Check(states.SequenceEqual(new[] { ServerState.Stopping, ServerState.Stop }), "shutdown emits Stopping then Stop");
    Check(!manager.Status.Owned, "ownership is released only after confirmed exit");
}

using (var child = Child())
using (var manager = Own(child))
{
    child.StandardInput.Dispose();
    bool rejected = false;
    try { await manager.StopOwnedAsync(CancellationToken.None); }
    catch (ObjectDisposedException) { rejected = true; }
    Check(rejected && manager.Status.State != ServerState.Stopping, "failed shutdown write does not leave Stopping stuck");
    using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(5));
    await child.WaitForExitAsync(deadline.Token);
}
Console.WriteLine($"SERVER STOP PASS: {passed} checks; no production server or accounts accessed.");
