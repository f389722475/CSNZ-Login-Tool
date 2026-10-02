using static Csnz.Launcher.Localizer;
using System.Windows;
using System.Threading;

namespace Csnz.Launcher;
public partial class App : Application
{
    private Mutex? instance;
    private void OnStartup(object sender, StartupEventArgs e)
    {
        if (e.Args.Length == 2 && e.Args[0] == "--check-paths")
        {
            int result = 2;
            try { result = NativePreflight.CheckPaths(e.Args[1]); } catch { }
            Shutdown(result); return;
        }
        if (e.Args.Length > 0 && e.Args[0] == "--check-native")
        {
            int result = 2;
            try { if (e.Args.Length == 3) result = NativePreflight.Run(e.Args[1], e.Args[2]); }
            catch { result = 2; } // no dialog, settings access, or credential logging in diagnostic mode
            Shutdown(result); return;
        }
        var settings = SettingsStore.Load(out var warning);
        Apply(settings.Language);
        instance = new Mutex(true, "Local\\CSNZ_SaaS_Desktop_Launcher", out var created);
        if (!created) { UiDialog.Show(null, Msg("App.AlreadyRunning"), Msg("AppTitle")); Shutdown(); return; }
        var window = new MainWindow(settings, warning);
        MainWindow = window;
        window.Show();
        ShellIcon.Refresh();
    }
    protected override void OnExit(ExitEventArgs e) { instance?.Dispose(); base.OnExit(e); }
}
