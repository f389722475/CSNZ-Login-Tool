using static Csnz.Launcher.Localizer;
using System.Buffers.Binary;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;

namespace Csnz.Launcher;

internal static class AuthBridge
{
    // A new immutable cache identity for the compatibility bridge. Old caches
    // remain untouched, including those used by an already running game.
    private static readonly Lazy<string> Payload = new(() => NativeBundle.Extract("LauncherAuth", "1.0.1-compat2",
        new[] { "CSNZLauncherBridge.dll", "MinHook-LICENSE.txt", "Auth-NOTICE.txt" }));
    internal static string DllPath => Payload.Value;
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr OpenEvent(uint access, bool inherit, string name);
    [DllImport("kernel32.dll")] private static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
    [DllImport("kernel32.dll")] private static extern bool CloseHandle(IntPtr handle);

    internal static void Validate(string gameRoot)
    {
        var bin = Path.Combine(GamePaths.NormalizeRoot(gameRoot), "Bin");
        GameCompatibility.ValidateFile(Path.Combine(bin, "CSOLauncher.exe"), 811008);
        GameCompatibility.ValidateFile(Path.Combine(bin, "hw.dll"), 73768960);
        _ = DllPath; // Verify/extract this EXE's own payload before starting a game.
    }
    internal static void Attach(Process game, string account, string password, CancellationToken ct)
    {
        CsnzProtocol.ValidateCredentials(account, password, false);
        var argument = new byte[36];
        try
        {
            BinaryPrimitives.WriteInt32LittleEndian(argument, argument.Length);
            Encoding.ASCII.GetBytes(account, argument.AsSpan(4, 16));
            Encoding.ASCII.GetBytes(password, argument.AsSpan(20, 16));
            NativePatch.AttachNewProcess(game, DllPath, ct, "CSNZAuth_Start", argument);
        }
        finally { CryptographicOperations.ZeroMemory(argument); }
    }
    internal static async Task<bool> WaitSentAsync(Process game, CancellationToken ct)
    {
        for (int attempt = 0; attempt < 60; attempt++)
        {
            await Task.Delay(1000, ct);
            if (game.HasExited) return false;
            var signal = OpenEvent(0x00100000, false, $"Local\\CSNZ_Desktop_Auth_Result_{game.Id}");
            if (signal == IntPtr.Zero) continue;
            bool completed;
            try { completed = WaitForSingleObject(signal, 0) == 0; }
            finally { CloseHandle(signal); }
            if (completed) return await Task.Run(() => NativePatch.QueryStatus(game, DllPath, "CSNZAuth_Status", ct), ct) == 2;
        }
        return false;
    }
}
