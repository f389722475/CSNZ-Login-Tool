using System.Globalization;
using System.IO;
using System.Reflection;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using Csnz.Launcher;

internal static class Program
{
    private static int checks;
    private static void Check(bool value, string name)
    { if (!value) throw new Exception("FAIL: " + name); checks++; Console.WriteLine("PASS: " + name); }
    private static void Call(MainWindow window, string method, params object[] args) =>
        typeof(MainWindow).GetMethod(method, BindingFlags.Instance | BindingFlags.NonPublic)!.Invoke(window, args);
    private static T Named<T>(MainWindow window, string name) where T : FrameworkElement => (T)window.FindName(name);
    private static void Language(MainWindow window, string language)
    { Localizer.Apply(language); Call(window, "RefreshLocalizedUi"); }

    [STAThread] private static void Main(string[] args)
    {
        var output = Path.GetFullPath(args[0]); Directory.CreateDirectory(output);
        // Instantiate production controls without Show/Run/Loaded. No accounts,
        // real settings, network, servers or game processes are accessed.
        var app = new App(); app.InitializeComponent();
        var fields = new Regex(@"\{(\d+)(?:[^}]*)\}");
        foreach (var (key, entry) in Localizer.Catalog)
        {
            if (string.IsNullOrWhiteSpace(entry.Zh) || string.IsNullOrWhiteSpace(entry.En) || Regex.IsMatch(entry.En, "[\u3400-\u9fff]"))
                throw new Exception("Incomplete bilingual entry: " + key);
            if (!fields.Matches(entry.Zh).Select(m => m.Value).Order().SequenceEqual(fields.Matches(entry.En).Select(m => m.Value).Order()))
                throw new Exception("Mismatched format arguments: " + key);
            foreach (var language in new[] { "zh-CN", "en" })
            { Localizer.Apply(language); _ = Localizer.Text(key, "a", "b", "c", "d"); }
        }
        Check(true, $"all {Localizer.Catalog.Count} entries have complete translations and matching format arguments");
        var error = Localizer.Error<InvalidDataException>("Error.PacketFormat");
        var registered = CsnzProtocol.MapRegistrationReply("You have successfully registered.");
        Localizer.Apply("zh-CN"); Check(registered.Message.Contains("注册成功"), "registration result in Chinese");
        Localizer.Apply("en"); Check(registered.Message.StartsWith("Registration succeeded") && Localizer.Describe(error).ToString().StartsWith("The server packet"), "existing reply and typed exception retranslate without rerunning operations");
        Check(!CsnzProtocol.MapRegistrationReply("connected OK").Success, "unknown reply is still not registration success");
        foreach (var language in new[] { "zh-CN", "en" })
        {
            Localizer.Apply(language);
            foreach (var invalid in new[] { ("bad", "Valid99!", false), ("Preview123", "bad\\pass", false), ("Preview123", "123456", true) })
            {
                bool rejected = false;
                try { CsnzProtocol.ValidateCredentials(invalid.Item1, invalid.Item2, invalid.Item3); }
                catch (InvalidOperationException e) { rejected = e.Data.Count > 0 && (language != "en" || !Regex.IsMatch(e.Message, "[\u3400-\u9fff]")); }
                Check(rejected, $"credential validation unchanged and translated ({language})");
            }
        }
        var settings = new LauncherSettings { Account = "Preview123", ProtectedPassword = "synthetic-ciphertext-not-a-secret", RememberPassword = true, Host = "localhost", Port = 31093 };
        string binding = settings.CredentialBinding, cipher = settings.ProtectedPassword;
        var settingsPath = Path.Combine(output, "synthetic-settings.json");
        SettingsStore.SaveLanguage(settings, "en", settingsPath);
        var loaded = JsonSerializer.Deserialize<LauncherSettings>(File.ReadAllText(settingsPath))!;
        Check(loaded.Language == "en" && loaded.CredentialBinding == binding && loaded.ProtectedPassword == cipher, "language persistence preserves endpoint, binding and encrypted password bytes");
        SettingsStore.SaveLanguage(settings, "zh-CN", settingsPath);
        Check(JsonSerializer.Deserialize<LauncherSettings>(File.ReadAllText(settingsPath))!.Language == "zh-CN", "switch back to Chinese persists");
        File.WriteAllText(Path.Combine(output, "blocked-path"), "fixture");
        try { SettingsStore.SaveLanguage(settings, "en", Path.Combine(output, "blocked-path", "settings.json")); } catch (IOException) { }
        Check(settings.Language == "zh-CN", "failed language save rolls back preference");
        Check(Localizer.Normalize("unknown") == "zh-CN" && JsonSerializer.Deserialize<LauncherSettings>("{}")!.Language == "zh-CN", "legacy or invalid language falls back to Chinese");

        var state = new LauncherSettings { Language = "zh-CN", StartLocalServer = false, RememberAccount = false };
        var window = new MainWindow(state);
        Named<TextBox>(window, "AccountBox").Text = "Preview123";
        Named<PasswordBox>(window, "PasswordInput").Password = "Preview9!";
        Named<PasswordBox>(window, "ConfirmInput").Password = "Preview9!";
        Call(window, "SwitchTab", true);
        Call(window, "Feedback", Localizer.Msg("Error.ConfirmPassword"), false);
        Language(window, "en");
        Check(Named<TextBlock>(window, "FormTitle").Text == "Create your account" && Named<TextBlock>(window, "FeedbackText").Text.StartsWith("The passwords do not match"), "active registration tab and displayed error translate in place");
        Check(Named<TextBox>(window, "AccountBox").Text == "Preview123" && Named<PasswordBox>(window, "PasswordInput").Password == "Preview9!" && Named<PasswordBox>(window, "ConfirmInput").Password == "Preview9!", "language switch preserves account, password and confirmation inputs");
        Call(window, "Reveal_Click", window, new RoutedEventArgs()); Language(window, "zh-CN");
        Check(Named<TextBox>(window, "PasswordVisible").Visibility == Visibility.Visible && Named<TextBox>(window, "PasswordVisible").Text == "Preview9!" && (string)Named<Button>(window,"RevealButton").Content == "隐藏", "language switch preserves reveal state");
        Call(window, "ShowPage", "settings");
        Named<TextBox>(window, "HostBox").Text = "draft.example";
        Named<TextBox>(window, "GamePathBox").Text = @"Z:\unsaved folder";
        Named<TextBox>(window, "PortBox").Text = "31234";
        Language(window, "zh-CN");
        Check(Named<TextBox>(window, "HostBox").Text == "draft.example" && Named<TextBox>(window, "GamePathBox").Text == @"Z:\unsaved folder" && Named<TextBox>(window, "PortBox").Text == "31234" && Named<StackPanel>(window, "SettingsPage").Visibility == Visibility.Visible, "language switch preserves active page and unsaved settings");
        Call(window, "SetBusy", true); Language(window, "en");
        Check(!Named<Button>(window, "SubmitButton").IsEnabled && (string)Named<Button>(window, "SubmitButton").Content == "Please wait…", "busy state remains disabled and translates");
        Call(window, "SetBusy", false);
        foreach (var language in new[] { "zh-CN", "en" })
        foreach (var scenario in new[] { ("login",1200,820), ("minimum",1100,760), ("register-error",1100,760), ("settings",1100,760), ("settings-lan",1100,760), ("settings-internet",1100,760), ("settings-join",1100,760), ("mods",1100,760), ("plugins",1100,760), ("wide",1600,900) })
            Render(output, language, scenario.Item1, scenario.Item2, scenario.Item3);
        foreach (var language in new[] { "zh-CN", "en" })
        {
            Localizer.Apply(language);
            var dialog = new UiDialog(Localizer.Msg("Close.Confirm"), Localizer.Msg("Close.Title"), MessageBoxButton.YesNoCancel);
            var panel = (StackPanel)dialog.Content;
            var buttons = ((StackPanel)panel.Children[^1]).Children.Cast<Button>().Select(b => (string)b.Content).ToArray();
            Check(buttons.SequenceEqual(language == "en" ? new[] { "Yes", "No", "Cancel" } : new[] { "是", "否", "取消" }), "dialog actions follow app language, not Windows language: " + language);
            Check(((StackPanel)panel.Children[^1]).Children.Cast<Button>().Single(b=>b.IsCancel).Content.Equals(Localizer.Text("Dialog.Cancel")), "Escape cancels rather than choosing No: " + language);
            dialog.Content = null;
            var dialogRoot = new Grid { Background = dialog.Background };
            System.Windows.Documents.TextElement.SetForeground(dialogRoot, dialog.Foreground);
            System.Windows.Documents.TextElement.SetFontFamily(dialogRoot, dialog.FontFamily);
            System.Windows.Documents.TextElement.SetFontSize(dialogRoot, dialog.FontSize);
            dialogRoot.Children.Add(panel); dialogRoot.Measure(new Size(550, double.PositiveInfinity));
            dialogRoot.Arrange(new Rect(0, 0, 550, dialogRoot.DesiredSize.Height)); dialogRoot.UpdateLayout();
            SaveImage(dialogRoot, 550, (int)Math.Ceiling(dialogRoot.ActualHeight), Path.Combine(output, language + "-dialog.png"));
        }
        Console.WriteLine($"LOCALIZATION PASS: {checks}; no production settings/accounts or game/server processes accessed.");
        // Do not close production MainWindow objects: Closing persists credentials.
        // No dispatcher loop was started; returning ends this offline test process.
    }
    private static void Render(string output, string language, string scenario, int width, int height)
    {
        bool isSettings = scenario.StartsWith("settings", StringComparison.Ordinal);
        var mode = scenario switch { "settings-lan" => MultiplayerMode.Lan, "settings-internet" => MultiplayerMode.Internet, "settings-join" => MultiplayerMode.Join, _ => MultiplayerMode.Local };
        var window = new MainWindow(new LauncherSettings { Language = language, StartLocalServer = false, RememberAccount = false, NetworkMode = mode,
            Host = mode == MultiplayerMode.Join ? "192.0.2.123" : "127.0.0.1", AdvertisedGameAddress = mode == MultiplayerMode.Internet ? "203.0.113.42" : "192.168.0.12" });
        if (scenario == "register-error") { Call(window, "SwitchTab", true); Call(window, "Feedback", Localizer.Msg("Error.ConfirmPassword"), false); }
        if (isSettings || scenario is "mods" or "plugins") Call(window, "ShowPage", isSettings ? "settings" : scenario);
        Call(window, "RenderServer", new ServerStatus(ServerState.Ready, Localizer.Msg("Server.ReadyOwned"), true));
        var root = (Grid)window.Content; window.Content = null;
        root.Background = window.Background;
        System.Windows.Documents.TextElement.SetForeground(root, window.Foreground);
        System.Windows.Documents.TextElement.SetFontFamily(root, window.FontFamily);
        System.Windows.Documents.TextElement.SetFontSize(root, window.FontSize);
        root.Measure(new Size(width, height)); root.Arrange(new Rect(0, 0, width, height)); root.UpdateLayout();
        Point At(FrameworkElement e) => e.TransformToAncestor(root).Transform(new Point());
        if (!isSettings && scenario is not ("mods" or "plugins"))
        {
            var card = Named<Border>(window, "LoginCard"); var server = Named<Border>(window, "ServerControlCard"); var art = Named<Border>(window, "LoginArtwork");
            if (Math.Abs(At(card).Y-At(art).Y)>0.1 || Math.Abs(At(card).Y+card.ActualHeight-At(server).Y-server.ActualHeight)>0.1)
                throw new Exception("Misaligned columns: " + language + scenario);
            if (scenario != "register-error")
            {
                var account = Named<CheckBox>(window, "RememberAccountBox"); var password = Named<CheckBox>(window, "RememberPasswordBox"); var forgot = Named<Button>(window, "ForgotButton");
                if (At(account).Y+account.ActualHeight>At(forgot).Y || At(password).Y+password.ActualHeight>At(forgot).Y)
                    throw new Exception("Remember/recovery controls overlap");
            }
        }
        var toggle=Named<Button>(window,"LanguageButton"); var address=Named<TextBlock>(window,"ServerAddressHeader");
        if (At(toggle).X+toggle.ActualWidth>At(address).X) throw new Exception("Language toggle overlaps address");
        CheckText(root, root, language);
        SaveImage(root,width,height,Path.Combine(output,language+"-"+scenario+".png"));
        Check(true,$"production WPF {language}/{scenario} {width}x{height}: aligned columns, no text clipping/overlap");
    }
    private static void CheckText(DependencyObject node, FrameworkElement root, string language)
    {
        if (node is UIElement { Visibility: not Visibility.Visible }) return;
        if (node is TextBlock t && t.Text.Length>0)
        {
            if (language=="en" && Regex.IsMatch(t.Text,"[\u3400-\u9fff]")) throw new Exception("Chinese text in English view: "+t.Text);
            if (t.TextWrapping==TextWrapping.NoWrap)
            {
                var text=new FormattedText(t.Text,CultureInfo.InvariantCulture,t.FlowDirection,new Typeface(t.FontFamily,t.FontStyle,t.FontWeight,t.FontStretch),t.FontSize,t.Foreground,1);
                if (text.Width>t.ActualWidth+1) throw new Exception("Clipped text: "+t.Text+$" ({text.Width:F1}>{t.ActualWidth:F1})");
            }
            var at=t.TransformToAncestor(root).Transform(new Point());
            if (at.X < -1 || at.X+t.ActualWidth>root.ActualWidth+1) throw new Exception("Text outside window: "+t.Text);
            DependencyObject? parent=VisualTreeHelper.GetParent(t);
            while(parent != null && parent is not Button) parent=VisualTreeHelper.GetParent(parent);
            if(parent is Button button)
            {
                var left=button.TransformToAncestor(root).Transform(new Point()).X;
                if(at.X<left+button.Padding.Left+button.BorderThickness.Left-1 || at.X+t.ActualWidth>left+button.ActualWidth-button.Padding.Right-button.BorderThickness.Right+1)
                    throw new Exception("Text clipped by button padding: "+t.Text);
            }
        }
        for(int i=0;i<VisualTreeHelper.GetChildrenCount(node);i++)CheckText(VisualTreeHelper.GetChild(node,i),root,language);
    }
    private static void SaveImage(Visual root,int width,int height,string path)
    {
        var bitmap=new RenderTargetBitmap(width*2,height*2,192,192,PixelFormats.Pbgra32);bitmap.Render(root);
        var encoder=new PngBitmapEncoder();encoder.Frames.Add(BitmapFrame.Create(bitmap));
        using var stream=File.Create(path);encoder.Save(stream);
    }
}
