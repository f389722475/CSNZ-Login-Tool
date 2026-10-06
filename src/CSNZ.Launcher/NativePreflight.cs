using System.IO;
using System.Text.Json;
using System.Reflection;

namespace Csnz.Launcher;

internal static class NativePreflight
{
    private static string Version => typeof(NativePreflight).Assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion.Split('+')[0] ?? "unknown";
    public static int CheckPaths(string reportPath)
    {
        var settings = SettingsStore.Load(out var warning);
        using var report = new FileStream(Path.GetFullPath(reportPath), FileMode.CreateNew, FileAccess.Write);
        bool found = GamePaths.IsGameRoot(settings.GameRoot);
        JsonSerializer.Serialize(report, new { launcherVersion = Version, executableDirectory = AppContext.BaseDirectory,
            workingDirectory = Environment.CurrentDirectory, gameRoot = settings.GameRoot, manual = settings.GameRootIsManual,
            gameFound = found, configurationWarning = warning != null, gameStarted = false, settingsWritten = false,
            credentialsDecrypted = false });
        return found ? 0 : 1;
    }
    // Diagnostic mode: does not open settings, connect to a server, or start/inject a game.
    public static int Run(string gameRoot, string reportPath)
    {
        using var report = new FileStream(Path.GetFullPath(reportPath), FileMode.CreateNew, FileAccess.Write);
        try
        {
            WeaponBundle.ValidateGame(Path.GetFullPath(gameRoot));
            WeaponBundle.ValidatePayload();
            AuthBridge.Validate(Path.GetFullPath(gameRoot));
            JsonSerializer.Serialize(report, new { passed = true, launcherVersion = Version, coreDll = WeaponBundle.CoreDll, weaponModules = WeaponBundle.Catalog.Weapons.Length,
                serverOnly = true, gamePeLayoutValidated = true, gameWholeFileHashesRequired = false, payloadHashesAndExportsValidated = true,
                runtimeCodeAndVtablesValidated = false, gameStarted = false, settingsRead = false, gameFilesChanged = false, nativeGameplayValidated = false });
            return 0;
        }
        catch (Exception error)
        {
            JsonSerializer.Serialize(report, new { passed = false, error = error.Message, gameStarted = false, settingsRead = false });
            return 1;
        }
    }
}
