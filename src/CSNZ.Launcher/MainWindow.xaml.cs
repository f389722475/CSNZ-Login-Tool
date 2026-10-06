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
    private readonly AwakeningPlugin awakening = new();
    private readonly NativeWeaponServer weapons = new();
    private readonly bool previewPlugins;
    private UiText? pluginFailure;
    private readonly CancellationTokenSource lifetime = new();
    private readonly DispatcherTimer timer = new() { Interval = TimeSpan.FromSeconds(8) };
    private bool initialized, registering, busy, syncing, closing, allowClose;
    private Process? game;
    private string currentPage = "login";
    private UiText? feedbackMessage, settingsMessage, modFailure;
    private static SolidColorBrush Brush(string hex) => (SolidColorBrush)new BrushConverter().ConvertFromString(hex)!;
    public MainWindow(LauncherSettings settings, UiText? warning = null, bool previewPlugins = false)
    {
        this.settings = settings;
        this.previewPlugins = previewPlugins;
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
        timer.Tick += async (_, _) => { if (!busy && !closing) { try { await server.RefreshAsync(settings, lifetime.Token); await SyncPluginsAsync(); await RefreshWeaponsAsync(); } catch (OperationCanceledException) { } catch { } } };
    }
    private async void Window_Loaded(object sender, RoutedEventArgs e)
    {
        if (previewPlugins) { ShowPage("plugins"); ClassAwakeningBox.IsEnabled = false; return; }
        timer.Start();
        if (settings.StartLocalServer && Multiplayer.IsHost(settings)) await StartServerAsync();
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
        StopServerButton.IsEnabled = !busy && !closing && (s.Owned || weapons.Owned) && !transitioning;
        StopServerButton.Content = Text(s.State == ServerState.Stopping ? "Server.StoppingButton" : "Server.Stop");
    }
    private void SetBusy(bool value)
    {
        busy = value; SubmitButton.IsEnabled = !value; LoginTab.IsEnabled = !value; RegisterTab.IsEnabled = !value;
        AccountBox.IsEnabled = !value; PasswordInput.IsEnabled = !value; PasswordVisible.IsEnabled = !value; ConfirmInput.IsEnabled = !value;
        PluginsNav.IsEnabled = !value; ClassAwakeningBox.IsEnabled = !value;
        SettingsNav.IsEnabled = !value; WeaponModsNav.IsEnabled = !value; WeaponChoices.IsEnabled = !value && !weapons.Owned; RememberRow.IsEnabled = !value; RevealButton.IsEnabled = !value;
        SubmitButton.Content = value ? Text("Form.Busy") : registering ? Text("Form.Create") : Text("Form.Submit");
        RenderServer(server.Status);
    }
    private async Task StartServerAsync()
    {
        if (busy) return; SetBusy(true);
        try { if (!await PrepareWeaponsAsync()) return; await server.EnsureReadyAsync(settings, lifetime.Token); await weapons.EnsureReadyAsync(settings, lifetime.Token); await SyncPluginsAsync(true); UpdateModHints(); }
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
        try { await weapons.StopOwnedAsync(lifetime.Token); if (server.Status.Owned) await server.StopOwnedAsync(lifetime.Token); UpdateModHints(); RenderServer(server.Status); Feedback(Msg("Server.StoppedFeedback"), true); }
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
            if (!await PrepareWeaponsAsync()) return;
            await server.EnsureReadyAsync(settings, lifetime.Token);
            await weapons.EnsureReadyAsync(settings, lifetime.Token);
            await SyncPluginsAsync(true);
            if (!settings.UseTls && !CsnzProtocol.IsLoopback(settings.Host) && UiDialog.Show(this,
                Msg("Connection.Unencrypted"), Msg("Connection.UnencryptedTitle"), MessageBoxButton.YesNo, MessageBoxImage.Warning) != MessageBoxResult.Yes) return;
            if (register)
            {
                var timestamp = NativePatch.ReadPeTimestamp(Path.Combine(settings.GameRoot, "Bin", "client.dll"));
                var result = await CsnzProtocol.RegisterAsync(settings, account, password, timestamp, lifetime.Token, allowUnencryptedRemote: true);
                if (result.Success) { SwitchTab(false); ConfirmInput.Clear(); SaveCredentials(); }
                Feedback(result.MessageText, result.Success);
            }
            else
            {
                UpdateModHints();
                game = await GameLauncher.StartAsync(settings, account, password, text => Dispatcher.BeginInvoke(() => Feedback(text, true)), lifetime.Token, weapons);
                SaveCredentials();
                _ = MonitorAuthenticationAsync(game);
                _ = MonitorAwakeningObserverAsync();
                _ = RefreshWeaponsAsync();
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
    private async Task<bool> PrepareWeaponsAsync()
    {
        if (Multiplayer.IsHost(settings) && !Multiplayer.NeedsDedicated(settings)) return true;
        if (Multiplayer.IsHost(settings)) WeaponBundle.ValidateGame(settings.GameRoot, WeaponBundle.Selected(settings).Length != 0);
        var plan = await Task.Run(() => WeaponAssets.Inspect(settings), lifetime.Token);
        if (!plan.Required) return true;
        if (UiDialog.Show(this, Msg(Multiplayer.IsHost(settings) ? "Weapons.PreparePrompt" : "Network.JoinAssetsPrompt", settings.GameRoot), Msg("Nav.Mods"), MessageBoxButton.YesNo, MessageBoxImage.Question) != MessageBoxResult.Yes) return false;
        var backup = await Task.Run(() => WeaponAssets.Apply(plan), lifetime.Token);
        Feedback(Msg("Weapons.Prepared", backup), true); return true;
    }
    private async Task RefreshWeaponsAsync()
    {
        try { await weapons.RefreshAsync(lifetime.Token); modFailure = weapons.RuntimeStatus == 100 ? Msg("Weapons.NativeFailed", weapons.LogPath) : null; }
        catch (OperationCanceledException) { }
        catch (Exception error) { modFailure = Msg("Weapons.StatusFailed", Explain(error)); }
        finally { UpdateModHints(); }
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
        NetworkModeBox.SelectedIndex = (int)Multiplayer.Mode(settings); AdvertisedAddressBox.Text = settings.AdvertisedGameAddress; DedicatedPortBox.Text = settings.DedicatedWeaponPort.ToString();
        UpdateNetworkHelp();
        AutoServerBox.IsChecked = settings.StartLocalServer; MinimizeBox.IsChecked = settings.MinimizeOnLaunch;
        syncing = true; LoadWeaponChoices(); ClassAwakeningBox.IsChecked = settings.EnableClassAwakening; syncing = false;
        ServerAddressHeader.Text = $"{settings.Host}:{settings.Port}"; UpdateModHints();
    }
    private void UpdateModHints()
    {
        var selected = WeaponBundle.Selected(settings).Length;
        WeaponRuntimeText.Text = !Multiplayer.IsHost(settings) ? Text("Network.RemoteWeapons") : weapons.Owned && selected == 0 ? Text("Network.DedicatedOnly") : weapons.RuntimeStatus == 3 ? Text("Weapons.Active", weapons.LoadedCount) : weapons.RuntimeStatus == 2 ? Text("Weapons.WaitingMap") : Text("Weapons.Idle");
        ModsFeedback.Text = modFailure?.ToString() ?? Text("Weapons.Selected", selected, WeaponBundle.Catalog.Weapons.Length);
        WeaponChoices.IsEnabled = !busy && !weapons.Owned;
    }
    private sealed class WeaponChoice
    {
        public int Id { get; init; } public string Name { get; init; } = ""; public string Detail { get; init; } = ""; public bool Enabled { get; set; }
    }
    private void LoadWeaponChoices()
    {
        var selected = WeaponBundle.Selected(settings).Select(w => w.Id).ToHashSet();
        WeaponChoices.ItemsSource = WeaponBundle.Catalog.Weapons.Select(w => new WeaponChoice { Id = w.Id, Name = w.Name, Detail = w.Detail, Enabled = selected.Contains(w.Id) }).ToArray();
    }
    private void WeaponMod_Changed(object sender, RoutedEventArgs e)
    {
        if (!initialized || syncing || previewPlugins) return;
        if (sender is not System.Windows.Controls.CheckBox box || box.Tag is not int id) return;
        if (WeaponBundle.Selected(settings).Any(w => w.Id == id) == (box.IsChecked == true)) return;
        var previous = settings.EnabledWeaponIds; bool legacy = settings.EnableNativePatch;
        try
        {
            if (weapons.Owned) throw Error<InvalidOperationException>("Weapons.RestartRequired");
            var ids = WeaponBundle.Selected(settings).Select(w => w.Id).ToHashSet();
            if (box.IsChecked == true) ids.Add(id); else ids.Remove(id);
            settings.EnabledWeaponIds = WeaponBundle.Catalog.Weapons.Where(w => ids.Contains(w.Id)).Select(w => w.Id).ToArray();
            settings.EnableNativePatch = settings.EnabledWeaponIds.Length != 0;
            SettingsStore.Save(settings); modFailure = null; UpdateModHints();
        }
        catch (Exception error)
        {
            settings.EnabledWeaponIds = previous; settings.EnableNativePatch = legacy; syncing = true; LoadWeaponChoices(); syncing = false;
            modFailure = Msg("Settings.SaveFailed", Explain(error)); UpdateModHints();
        }
    }
    private void UpdatePluginHints()
    {
        ClassAwakeningStatus.Text = Text(!Multiplayer.IsHost(settings) ? "Plugins.LocalOnly" : pluginFailure != null ? "Plugins.ShortError" : awakening.Active ? "Plugins.ShortActive" : settings.EnableClassAwakening ? "Plugins.ShortWaiting" : "Plugins.ShortDisabled");
        ClassAwakeningStatus.ToolTip = pluginFailure?.ToString();
        PluginsFeedback.Text = awakening.Active ? Text("Plugins.CountOne") : Text("Plugins.Count", 0);
        PluginsFeedback.ToolTip = pluginFailure?.ToString();
    }
    private async Task SyncPluginsAsync(bool required = false)
    {
        try { await awakening.SyncAsync(settings, lifetime.Token); pluginFailure = null; }
        catch (OperationCanceledException) { throw; }
        catch (Exception error) { pluginFailure = Msg("Plugins.Failed", error.Message); if (required) throw; }
        finally { UpdatePluginHints(); }
        _ = MonitorAwakeningObserverAsync();
    }
    private async Task MonitorAwakeningObserverAsync()
    {
        try { await awakening.SyncObserverAsync(settings, lifetime.Token); }
        catch (OperationCanceledException) { }
        catch (Exception error) { if (!closing) { pluginFailure = Msg("Plugins.ObserverFailed", error.Message); UpdatePluginHints(); } }
    }
    private async void ClassAwakening_Changed(object sender, RoutedEventArgs e)
    {
        if (!initialized || syncing || previewPlugins) return;
        bool previous = settings.EnableClassAwakening;
        try
        {
            settings.EnableClassAwakening = ClassAwakeningBox.IsChecked == true;
            SettingsStore.Save(settings);
        }
        catch (Exception error)
        {
            settings.EnableClassAwakening = previous; syncing = true; ClassAwakeningBox.IsChecked = previous; syncing = false;
            pluginFailure = Msg("Settings.SaveFailed", Explain(error)); UpdatePluginHints(); return;
        }
        ClassAwakeningBox.IsEnabled = false;
        try { await SyncPluginsAsync(); }
        catch (OperationCanceledException) { }
        finally { ClassAwakeningBox.IsEnabled = !busy; }
    }
    private void ShowPage(string page)
    {
        currentPage = page;
        PluginsPage.Visibility = page == "plugins" ? Visibility.Visible : Visibility.Collapsed;
        LoginPage.Visibility = page == "login" ? Visibility.Visible : Visibility.Collapsed;
        WeaponModsPage.Visibility = page == "mods" ? Visibility.Visible : Visibility.Collapsed;
        SettingsPage.Visibility = page == "settings" ? Visibility.Visible : Visibility.Collapsed;
        Breadcrumb.Text = page == "plugins" ? Text("Nav.Plugins") : page == "mods" ? Text("Nav.Mods") : page == "settings" ? Text("Nav.Settings") : Text("Nav.Login");
        foreach (var (button, name) in new[] { (LoginNav, "login"), (WeaponModsNav, "mods"), (PluginsNav, "plugins"), (SettingsNav, "settings") })
        { button.Background = Brush(page == name ? "#EEECFF" : "#00FFFFFF"); button.Foreground = Brush(page == name ? "#6157E8" : "#758198"); }
        if (page == "settings") { LoadSettingsFields(); SetSettingsFeedback(null); }
        UpdatePluginHints();
        PageScroll.ScrollToTop();
    }
    private void LoginNav_Click(object sender, RoutedEventArgs e) => ShowPage("login");
    private void WeaponModsNav_Click(object sender, RoutedEventArgs e) => ShowPage("mods");
    private void PluginsNav_Click(object sender, RoutedEventArgs e) => ShowPage("plugins");
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
            if (!int.TryParse(DedicatedPortBox.Text, out int dedicatedPort)) throw Error<InvalidOperationException>("Error.Port");
            var networkMode = (MultiplayerMode)NetworkModeBox.SelectedIndex; var advertised = AdvertisedAddressBox.Text.Trim();
            Multiplayer.Validate(new LauncherSettings { NetworkMode = networkMode, Host = host, Port = port, DedicatedWeaponPort = dedicatedPort, AdvertisedGameAddress = advertised });
            if ((server.Status.Owned || weapons.Owned) && (networkMode != Multiplayer.Mode(settings) || advertised != settings.AdvertisedGameAddress || dedicatedPort != settings.DedicatedWeaponPort)) throw Error<InvalidOperationException>("Error.StopBeforeSwitch");
            if ((server.Status.Owned || weapons.Owned) && (!string.Equals(root, settings.GameRoot, StringComparison.OrdinalIgnoreCase) || host != settings.Host || port != settings.Port || TlsBox.IsChecked != settings.UseTls))
                throw Error<InvalidOperationException>("Error.StopBeforeSwitch");
            bool endpointChanged = host != settings.Host || port != settings.Port || TlsBox.IsChecked != settings.UseTls;
            settings.GameRootIsManual = !string.Equals(root, GamePaths.ResolveRoot("", false, AppContext.BaseDirectory), StringComparison.OrdinalIgnoreCase);
            settings.NetworkMode = networkMode; settings.AdvertisedGameAddress = advertised; settings.DedicatedWeaponPort = dedicatedPort;
            settings.GameRoot = root; settings.Host = host; settings.Port = port; settings.UseTls = TlsBox.IsChecked == true;
            settings.StartLocalServer = AutoServerBox.IsChecked == true; settings.MinimizeOnLaunch = MinimizeBox.IsChecked == true;
            if (endpointChanged) { AccountBox.Clear(); PasswordInput.Clear(); ConfirmInput.Clear(); RememberPasswordBox.IsChecked = false; }
            SaveCredentials(); LoadSettingsFields(); SetSettingsFeedback(Msg(endpointChanged ? "Settings.EndpointSaved" : "Settings.Saved"));
            if (settings.StartLocalServer && Multiplayer.IsHost(settings)) await StartServerAsync();
            else await server.RefreshAsync(settings, lifetime.Token);
        }
        catch (Exception error) { SetSettingsFeedback(Explain(error)); }
    }
    private void NetworkMode_Changed(object sender, System.Windows.Controls.SelectionChangedEventArgs e)
    {
        if (AdvertisedAddressBox == null || NetworkHelpText == null) return;
        UpdateNetworkHelp();
    }
    private void UpdateNetworkHelp()
    {
        if (NetworkHelpText == null) return;
        var mode = (MultiplayerMode)Math.Max(0, NetworkModeBox.SelectedIndex);
        AdvertisedAddressBox.IsEnabled = mode is MultiplayerMode.Lan or MultiplayerMode.Internet;
        DedicatedPortBox.IsEnabled = mode != MultiplayerMode.Join;
        DetectAddressButton.IsEnabled = mode == MultiplayerMode.Lan;
        NetworkHelpText.Text = Text("Network.Help." + mode);
    }
    private void DetectAddress_Click(object sender, RoutedEventArgs e)
    {
        var addresses = Multiplayer.LocalAddresses();
        if (addresses.Length == 1) AdvertisedAddressBox.Text = addresses[0];
        SetSettingsFeedback(Msg("Network.Addresses", addresses.Length == 0 ? "—" : string.Join(" / ", addresses)));
    }
    private void CopyConnection_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            if (Multiplayer.Mode(settings) is not (MultiplayerMode.Lan or MultiplayerMode.Internet)) throw Error<InvalidOperationException>("Network.SaveHostFirst");
            Multiplayer.Validate(settings);
            Clipboard.SetText(Text("Network.Invitation", Multiplayer.AdvertisedAddress(settings), settings.Port, settings.DedicatedWeaponPort, Text(settings.UseTls ? "Common.On" : "Common.Off")));
            SetSettingsFeedback(Msg("Network.Copied"));
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
        if (allowClose || previewPlugins) return;
        if (closing) { e.Cancel = true; return; }
        if (busy) { e.Cancel = true; Feedback(Msg("Close.Busy"), false); return; }
        e.Cancel = true; closing = true;
        try
        {
            SaveCredentials();
            if (server.Status.Owned || weapons.Owned)
            {
                if (GameIsRunning())
                {
                    if (UiDialog.Show(this, Msg("Close.GameRunning"), Msg("Close.Title"), MessageBoxButton.YesNo) != MessageBoxResult.Yes) return;
                }
                else
                {
                    var answer = UiDialog.Show(this, Msg("Close.Confirm"), Msg("Close.Title"), MessageBoxButton.YesNoCancel, MessageBoxImage.Question);
                    if (answer == MessageBoxResult.Cancel) return;
                    if (answer == MessageBoxResult.Yes) { await weapons.StopOwnedAsync(lifetime.Token); if (server.Status.Owned) await server.StopOwnedAsync(lifetime.Token); }
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
        Breadcrumb.Text = Text(currentPage == "plugins" ? "Nav.Plugins" : currentPage == "mods" ? "Nav.Mods" : currentPage == "settings" ? "Nav.Settings" : "Nav.Login");
        LanguageCn.Foreground = Brush(Localizer.Language == "zh-CN" ? "#6157E8" : "#8F99AC");
        LanguageEn.Foreground = Brush(Localizer.Language == "en" ? "#6157E8" : "#8F99AC");
        RenderServer(server.Status); UpdateModHints(); UpdatePluginHints(); UpdateNetworkHelp();
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
