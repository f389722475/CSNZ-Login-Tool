using static Csnz.Launcher.Localizer;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Security.Authentication;
using System.Windows;
using System.Windows.Media;
using System.Windows.Threading;
using Microsoft.Win32;

namespace Csnz.Launcher;
public partial class MainWindow : Window
{
    private LauncherSettings settings;
    private readonly ServerManager server = new();
    private readonly CancellationTokenSource lifetime = new();
    private readonly DispatcherTimer timer = new() { Interval = TimeSpan.FromSeconds(8) };
    private bool initialized, registering, busy, syncing, closing, allowClose;
    private Process? game;
    private string currentPage = "login";
    private UiText? feedbackMessage, settingsMessage, modFailure;
    private static SolidColorBrush Brush(string hex) => (SolidColorBrush)new BrushConverter().ConvertFromString(hex)!;
    public MainWindow(LauncherSettings settings, UiText? warning = null)
    {
        this.settings = settings;
        Apply(settings.Language);
        InitializeComponent();
        server.Changed += s => Dispatcher.BeginInvoke(() => RenderServer(s));
        LoadSettingsFields();
        RememberAccountBox.IsChecked = settings.RememberAccount;
        RememberPasswordBox.IsChecked = settings.RememberPassword && settings.RememberAccount;
        AccountBox.Text = settings.RememberAccount ? settings.Account : "";
        if (settings.RememberPassword && settings.RememberAccount && settings.ProtectedPassword.Length > 0)
        {
            try { PasswordInput.Password = SecretStore.Unprotect(settings.ProtectedPassword, settings.CredentialBinding); }
            catch { warning = Msg("Settings.DecryptFailed"); settings.ProtectedPassword = ""; RememberPasswordBox.IsChecked = false; }
        }
        initialized = true; UpdateHints(); RefreshLocalizedUi();
        if (warning != null) Feedback(warning, false);
        timer.Tick += async (_, _) => { if (!busy && !closing) { try { await server.RefreshAsync(settings, lifetime.Token); } catch (OperationCanceledException) { } catch { } } };
    }
    private async void Window_Loaded(object sender, RoutedEventArgs e)
    {
        timer.Start();
        if (settings.StartLocalServer && CsnzProtocol.IsLoopback(settings.Host)) await StartServerAsync();
        else { try { await server.RefreshAsync(settings, lifetime.Token); } catch { } }
    }
    private void RenderServer(ServerStatus s)
    {
        ServerStateText.Text = Text("State." + s.State);
        bool transitioning = s.State is ServerState.Starting or ServerState.Stopping;
        ServerSpinner.Visibility = transitioning ? Visibility.Visible : Visibility.Collapsed;
        ServerDot.Visibility = transitioning ? Visibility.Collapsed : Visibility.Visible;
        var color = s.State switch { ServerState.Ready => "#299276", ServerState.Starting or ServerState.Stopping => "#BB8B28", _ => "#C76172" };
        ServerDot.Fill = Brush(color); ServerStateText.Foreground = Brush(color);
        ServerBadge.Background = Brush(s.State switch { ServerState.Ready => "#EDF8F3", ServerState.Starting or ServerState.Stopping => "#FFF8E8", _ => "#FFF1F2" });
        ServerBadge.BorderBrush = Brush(s.State switch { ServerState.Ready => "#D5EDE1", ServerState.Starting or ServerState.Stopping => "#F2E5BF", _ => "#F8D9DF" });
        ServerBadge.ToolTip = s.Detail; ServerDetail.Text = s.Detail;
        StartServerButton.IsEnabled = !busy && s.State == ServerState.Stop;
        StopServerButton.IsEnabled = !busy && !closing && s.Owned && !transitioning;
        StopServerButton.Content = Text(s.State == ServerState.Stopping ? "Server.StoppingButton" : "Server.Stop");
    }
    private void SetBusy(bool value)
    {
        busy = value; SubmitButton.IsEnabled = !value; LoginTab.IsEnabled = !value; RegisterTab.IsEnabled = !value;
        AccountBox.IsEnabled = !value; PasswordInput.IsEnabled = !value; PasswordVisible.IsEnabled = !value; ConfirmInput.IsEnabled = !value;
        SettingsNav.IsEnabled = !value; WeaponModsNav.IsEnabled = !value; GigaBreakLEBox.IsEnabled = !value; RememberRow.IsEnabled = !value; RevealButton.IsEnabled = !value;
        SubmitButton.Content = value ? Text("Form.Busy") : registering ? Text("Form.Create") : Text("Form.Submit");
        RenderServer(server.Status);
    }
    private async Task StartServerAsync()
    {
        if (busy) return; SetBusy(true);
        try { await server.EnsureReadyAsync(settings, lifetime.Token); }
        catch (OperationCanceledException) { }
        catch (Exception e) { Feedback(Explain(e), false); }
        finally { SetBusy(false); }
    }
    private async void StartServer_Click(object sender, RoutedEventArgs e) => await StartServerAsync();
    private async void StopServer_Click(object sender, RoutedEventArgs e)
    {
        if (GameIsRunning()) { Feedback(Msg("Server.GameRunning"), false); return; }
        if (UiDialog.Show(this, Msg("Server.ConfirmStop"), Msg("Server.Stop"), MessageBoxButton.YesNo, MessageBoxImage.Question) != MessageBoxResult.Yes) return;
        SetBusy(true);
        try { await server.StopOwnedAsync(lifetime.Token); Feedback(Msg("Server.StoppedFeedback"), true); }
        catch (Exception error) { Feedback(Explain(error), false); }
        finally { SetBusy(false); }
    }
    private async void Submit_Click(object sender, RoutedEventArgs e)
    {
        if (busy) return;
        var account = AccountBox.Text.Trim(); var password = PasswordInput.Password;
        bool register = registering;
        try
        {
            CsnzProtocol.ValidateCredentials(account, password, register);
            if (register && password != ConfirmInput.Password) throw Error<InvalidOperationException>("Error.ConfirmPassword");
            if (!register) GameLauncher.CheckGame(settings);
            SetBusy(true); FeedbackBox.Visibility = Visibility.Collapsed;
            await server.EnsureReadyAsync(settings, lifetime.Token);
            if (register)
            {
                var timestamp = NativePatch.ReadPeTimestamp(Path.Combine(settings.GameRoot, "Bin", "client.dll"));
                var result = await CsnzProtocol.RegisterAsync(settings, account, password, timestamp, lifetime.Token);
                if (result.Success) { SwitchTab(false); ConfirmInput.Clear(); SaveCredentials(); }
                Feedback(result.MessageText, result.Success);
            }
            else
            {
                if (!settings.UseTls && !CsnzProtocol.IsLoopback(settings.Host) && UiDialog.Show(this,
                    Msg("Connection.Unencrypted"), Msg("Connection.UnencryptedTitle"), MessageBoxButton.YesNo, MessageBoxImage.Warning) != MessageBoxResult.Yes) return;
                game = await GameLauncher.StartAsync(settings, account, password, text => Dispatcher.BeginInvoke(() => Feedback(text, true)), lifetime.Token);
                SaveCredentials();
                _ = MonitorAuthenticationAsync(game);
                if (settings.EnableNativePatch) _ = MonitorPatchAsync(game);
                if (settings.MinimizeOnLaunch) WindowState = WindowState.Minimized;
            }
        }
        catch (Exception error) { Feedback(Explain(error), false); }
        finally { password = ""; SetBusy(false); }
    }
    private async Task MonitorAuthenticationAsync(Process process)
    {
        try
        {
            if (!await AuthBridge.WaitSentAsync(process, lifetime.Token) && !closing && !process.HasExited)
                Feedback(Msg("Auth.NotSent"), false);
        }
        catch (OperationCanceledException) { }
        catch (Exception error) { if (!closing) Feedback(Msg("Auth.CheckFailed", Explain(error)), false); }
    }
    private async Task MonitorPatchAsync(Process process)
    {
        try
        {
            bool ready = await NativePatch.WaitReadyAsync(process, GameLauncher.NativeDll, lifetime.Token);
            if (!closing) Feedback(ready ? Msg("Patch.Ready") : Msg("Patch.NotReady", Path.Combine(NativeBundle.DirectoryPath, "native-runtime.log")), ready);
        }
        catch (OperationCanceledException) { }
        catch (Exception e) { if (!closing) Feedback(Msg("Patch.CheckFailed", Explain(e)), false); }
    }
    private static UiText Explain(Exception e) => Describe(e);
    private void Feedback(UiText message, bool success)
    {
        feedbackMessage = message; FeedbackText.Text = message.ToString(); FeedbackText.Foreground = Brush(success ? "#487F72" : "#B7625C");
        FeedbackBox.Background = Brush(success ? "#F0F8F4" : "#FFF4F2"); FeedbackBox.Visibility = Visibility.Visible;
    }
    private void SwitchTab(bool register)
    {
        registering = register; ConfirmPanel.Visibility = register ? Visibility.Visible : Visibility.Collapsed;
        RememberRow.Visibility = register ? Visibility.Collapsed : Visibility.Visible;
        FormTitle.Text = register ? Text("Form.RegisterTitle") : Text("Form.LoginTitle");
        FormSubtitle.Text = register ? Text("Form.RegisterSubtitle") : Text("Form.LoginSubtitle");
        LoginTab.Background = Brush(register ? "#00FFFFFF" : "#FFFFFF"); RegisterTab.Background = Brush(register ? "#FFFFFF" : "#00FFFFFF");
        LoginTab.Foreground = Brush(register ? "#939BAE" : "#6258D9"); RegisterTab.Foreground = Brush(register ? "#6258D9" : "#939BAE");
        SubmitButton.Content = register ? Text("Form.Create") : Text("Form.Submit"); FeedbackBox.Visibility = Visibility.Collapsed;
        PasswordVisible.Visibility = Visibility.Collapsed; PasswordInput.Visibility = Visibility.Visible; RevealButton.Content = Text("Form.Show");
    }
    private void LoginTab_Click(object sender, RoutedEventArgs e) => SwitchTab(false);
    private void RegisterTab_Click(object sender, RoutedEventArgs e) => SwitchTab(true);
    private void Reveal_Click(object sender, RoutedEventArgs e)
    {
        bool reveal = PasswordVisible.Visibility != Visibility.Visible; syncing = true; PasswordVisible.Text = PasswordInput.Password; syncing = false;
        PasswordVisible.Visibility = reveal ? Visibility.Visible : Visibility.Collapsed; PasswordInput.Visibility = reveal ? Visibility.Collapsed : Visibility.Visible;
        RevealButton.Content = reveal ? Text("Form.Hide") : Text("Form.Show");
    }
    private void Account_Changed(object sender, System.Windows.Controls.TextChangedEventArgs e) { if (initialized) UpdateHints(); }
    private void Password_Changed(object sender, RoutedEventArgs e) { if (initialized && !syncing) { syncing = true; PasswordVisible.Text = PasswordInput.Password; syncing = false; UpdateHints(); } }
    private void VisiblePassword_Changed(object sender, System.Windows.Controls.TextChangedEventArgs e) { if (initialized && !syncing) { syncing = true; PasswordInput.Password = PasswordVisible.Text; syncing = false; UpdateHints(); } }
    private void UpdateHints() { AccountHint.Visibility = AccountBox.Text.Length == 0 ? Visibility.Visible : Visibility.Collapsed; PasswordHint.Visibility = PasswordInput.Password.Length == 0 ? Visibility.Visible : Visibility.Collapsed; }
    private void Remember_Changed(object sender, RoutedEventArgs e)
    {
        if (!initialized || syncing) return;
        syncing = true;
        if (sender == RememberPasswordBox && RememberPasswordBox.IsChecked == true) RememberAccountBox.IsChecked = true;
        if (RememberAccountBox.IsChecked != true) RememberPasswordBox.IsChecked = false;
        syncing = false;
        try { SaveCredentials(); } catch (Exception error) { Feedback(Msg("Settings.PreferencesFailed", Explain(error)), false); }
    }
    private void SaveCredentials()
    {
        settings.RememberAccount = RememberAccountBox.IsChecked == true;
        settings.RememberPassword = settings.RememberAccount && RememberPasswordBox.IsChecked == true;
        settings.Account = settings.RememberAccount ? AccountBox.Text.Trim() : "";
        settings.ProtectedPassword = settings.RememberPassword && PasswordInput.Password.Length > 0 ? SecretStore.Protect(PasswordInput.Password, settings.CredentialBinding) : "";
        SettingsStore.Save(settings);
    }
    private void LoadSettingsFields()
    {
        GamePathBox.Text = settings.GameRoot; HostBox.Text = settings.Host; PortBox.Text = settings.Port.ToString(); TlsBox.IsChecked = settings.UseTls;
        AutoServerBox.IsChecked = settings.StartLocalServer; MinimizeBox.IsChecked = settings.MinimizeOnLaunch;
        syncing = true; GigaBreakLEBox.IsChecked = settings.EnableNativePatch; syncing = false;
        ServerAddressHeader.Text = $"{settings.Host}:{settings.Port}"; UpdateModHints();
    }
    private void UpdateModHints()
    {
        GigaBreakLEStatus.Text = settings.EnableNativePatch ? Text("Mods.Enabled") : Text("Mods.Disabled");
        ModsFeedback.Text = modFailure?.ToString() ?? Text("Mods.Count", settings.EnableNativePatch ? 1 : 0);
    }
    private void WeaponMod_Changed(object sender, RoutedEventArgs e)
    {
        if (!initialized || syncing) return;
        bool previous = settings.EnableNativePatch;
        try
        {
            settings.EnableNativePatch = GigaBreakLEBox.IsChecked == true;
            SettingsStore.Save(settings); modFailure = null; UpdateModHints();
        }
        catch (Exception error)
        {
            settings.EnableNativePatch = previous; syncing = true; GigaBreakLEBox.IsChecked = previous; syncing = false;
            modFailure = Msg("Settings.SaveFailed", Explain(error)); UpdateModHints();
        }
    }
    private void ShowPage(string page)
    {
        currentPage = page;
        LoginPage.Visibility = page == "login" ? Visibility.Visible : Visibility.Collapsed;
        WeaponModsPage.Visibility = page == "mods" ? Visibility.Visible : Visibility.Collapsed;
        SettingsPage.Visibility = page == "settings" ? Visibility.Visible : Visibility.Collapsed;
        Breadcrumb.Text = page == "mods" ? Text("Nav.Mods") : page == "settings" ? Text("Nav.Settings") : Text("Nav.Login");
        foreach (var (button, name) in new[] { (LoginNav, "login"), (WeaponModsNav, "mods"), (SettingsNav, "settings") })
        { button.Background = Brush(page == name ? "#EEECFF" : "#00FFFFFF"); button.Foreground = Brush(page == name ? "#6157E8" : "#758198"); }
        if (page == "settings") { LoadSettingsFields(); SetSettingsFeedback(null); }
        PageScroll.ScrollToTop();
    }
    private void LoginNav_Click(object sender, RoutedEventArgs e) => ShowPage("login");
    private void WeaponModsNav_Click(object sender, RoutedEventArgs e) => ShowPage("mods");
    private void SettingsNav_Click(object sender, RoutedEventArgs e) => ShowPage("settings");
    private void Browse_Click(object sender, RoutedEventArgs e) { var dialog = new OpenFolderDialog { Title = Text("Settings.FolderTitle") }; if (dialog.ShowDialog(this) == true) GamePathBox.Text = dialog.FolderName; }
    private async void SaveSettings_Click(object sender, RoutedEventArgs e)
    {
        if (busy) return;
        try
        {
            string root = GamePaths.NormalizeRoot(GamePathBox.Text); string host = HostBox.Text.Trim();
            if (!File.Exists(Path.Combine(root, "Bin", "CSOLauncher.exe"))) throw Error<InvalidOperationException>("Error.SettingsGameMissing");
            if (Uri.CheckHostName(host) == UriHostNameType.Unknown) throw Error<InvalidOperationException>("Error.Host");
            if (!int.TryParse(PortBox.Text, out int port) || port is < 1 or > 65535) throw Error<InvalidOperationException>("Error.Port");
            if (server.Status.Owned && (!string.Equals(root, settings.GameRoot, StringComparison.OrdinalIgnoreCase) || host != settings.Host || port != settings.Port || TlsBox.IsChecked != settings.UseTls))
                throw Error<InvalidOperationException>("Error.StopBeforeSwitch");
            bool endpointChanged = host != settings.Host || port != settings.Port || TlsBox.IsChecked != settings.UseTls;
            settings.GameRootIsManual = !string.Equals(root, GamePaths.ResolveRoot("", false, AppContext.BaseDirectory), StringComparison.OrdinalIgnoreCase);
            settings.GameRoot = root; settings.Host = host; settings.Port = port; settings.UseTls = TlsBox.IsChecked == true;
            settings.StartLocalServer = AutoServerBox.IsChecked == true; settings.MinimizeOnLaunch = MinimizeBox.IsChecked == true;
            if (endpointChanged) { AccountBox.Clear(); PasswordInput.Clear(); ConfirmInput.Clear(); RememberPasswordBox.IsChecked = false; }
            SaveCredentials(); LoadSettingsFields(); SetSettingsFeedback(Msg(endpointChanged ? "Settings.EndpointSaved" : "Settings.Saved"));
            if (settings.StartLocalServer && CsnzProtocol.IsLoopback(settings.Host)) await StartServerAsync();
            else await server.RefreshAsync(settings, lifetime.Token);
        }
        catch (Exception error) { SetSettingsFeedback(Explain(error)); }
    }
    private void ClearCredentials_Click(object sender, RoutedEventArgs e)
    {
        syncing = true; RememberPasswordBox.IsChecked = false; RememberAccountBox.IsChecked = false; syncing = false;
        AccountBox.Clear(); PasswordInput.Clear(); PasswordVisible.Clear(); ConfirmInput.Clear();
        try { SaveCredentials(); SetSettingsFeedback(Msg("Settings.Cleared")); } catch (Exception error) { SetSettingsFeedback(Explain(error)); }
    }
    private void Forgot_Click(object sender, RoutedEventArgs e) => UiDialog.Show(this, Msg("Dialog.Recover"), Msg("Dialog.RecoverTitle"), MessageBoxButton.OK, MessageBoxImage.Information);
    private void Help_Click(object sender, RoutedEventArgs e) => UiDialog.Show(this, Msg("Dialog.Help"), Msg("Nav.Help"), MessageBoxButton.OK, MessageBoxImage.Information);
    private bool GameIsRunning() { try { return ProcessTools.IsRunning(Path.Combine(settings.GameRoot, "Bin", "CSOLauncher.exe"), "CSOLauncher"); } catch { return true; } }
    private async void Window_Closing(object? sender, CancelEventArgs e)
    {
        if (allowClose) return;
        if (closing) { e.Cancel = true; return; }
        if (busy) { e.Cancel = true; Feedback(Msg("Close.Busy"), false); return; }
        e.Cancel = true; closing = true;
        try
        {
            SaveCredentials();
            if (server.Status.Owned)
            {
                if (GameIsRunning())
                {
                    if (UiDialog.Show(this, Msg("Close.GameRunning"), Msg("Close.Title"), MessageBoxButton.YesNo) != MessageBoxResult.Yes) return;
                }
                else
                {
                    var answer = UiDialog.Show(this, Msg("Close.Confirm"), Msg("Close.Title"), MessageBoxButton.YesNoCancel, MessageBoxImage.Question);
                    if (answer == MessageBoxResult.Cancel) return;
                    if (answer == MessageBoxResult.Yes) await server.StopOwnedAsync(lifetime.Token);
                }
            }
            timer.Stop(); lifetime.Cancel(); allowClose = true;
            // With no owned server there is no await above: Close() would re-enter
            // WPF's active Closing event and throw. Finish this event first.
            _ = Dispatcher.BeginInvoke(new Action(Close));
        }
        catch (Exception error) { Feedback(Msg("Close.Failed", Explain(error)), false); }
        finally { closing = false; }
    }
    private void SetSettingsFeedback(UiText? message)
    { settingsMessage = message; SettingsFeedback.Text = message?.ToString() ?? ""; }
    private void RefreshLocalizedUi()
    {
        // Do not reload settings, recreate pages or reset the sign-in form.
        // Draft settings, passwords, tab selection, reveal state and scroll stay intact.
        FormTitle.Text = Text(registering ? "Form.RegisterTitle" : "Form.LoginTitle");
        FormSubtitle.Text = Text(registering ? "Form.RegisterSubtitle" : "Form.LoginSubtitle");
        SubmitButton.Content = Text(busy ? "Form.Busy" : registering ? "Form.Create" : "Form.Submit");
        RevealButton.Content = Text(PasswordVisible.Visibility == Visibility.Visible ? "Form.Hide" : "Form.Show");
        Breadcrumb.Text = Text(currentPage == "mods" ? "Nav.Mods" : currentPage == "settings" ? "Nav.Settings" : "Nav.Login");
        LanguageCn.Foreground = Brush(Localizer.Language == "zh-CN" ? "#6157E8" : "#8F99AC");
        LanguageEn.Foreground = Brush(Localizer.Language == "en" ? "#6157E8" : "#8F99AC");
        RenderServer(server.Status); UpdateModHints();
        if (feedbackMessage != null) FeedbackText.Text = feedbackMessage.ToString();
        SetSettingsFeedback(settingsMessage);
    }
    private void Language_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            // Save only the preference: never re-encrypt or clear credentials and
            // never commit unsaved fields from the Settings page on a language click.
            SettingsStore.SaveLanguage(settings, Localizer.Language == "en" ? "zh-CN" : "en");
            Apply(settings.Language); RefreshLocalizedUi();
        }
        catch (Exception error) { Feedback(Msg("Language.SavedError", Explain(error)), false); }
    }
    private void Minimize_Click(object sender, RoutedEventArgs e) => WindowState = WindowState.Minimized;
    private void Maximize_Click(object sender, RoutedEventArgs e) => WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;
    private void Close_Click(object sender, RoutedEventArgs e) => Close();
}
