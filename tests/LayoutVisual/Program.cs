using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Markup;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Xml.Linq;

internal static class Program
{
    [STAThread] private static void Main(string[] args)
    {
        // Render production XAML without its class or event handlers. This does
        // not open a window, read settings/accounts, or start a server/game.
        var project = Path.GetFullPath(args[0]);
        var output = Path.GetFullPath(args[1]); Directory.CreateDirectory(output);
        var app = new Application();
        XNamespace p = "http://schemas.microsoft.com/winfx/2006/xaml/presentation";
        XNamespace x = "http://schemas.microsoft.com/winfx/2006/xaml";
        var application = XDocument.Load(Path.Combine(project, "src/CSNZ.Launcher/App.xaml"));
        var resources = new XElement(p + "ResourceDictionary", new XAttribute("xmlns", p.NamespaceName),
            new XAttribute(XNamespace.Xmlns + "x", x.NamespaceName), application.Root!.Element(p + "Application.Resources")!.Elements());
        app.Resources = (ResourceDictionary)XamlReader.Parse(resources.ToString());
        Csnz.Launcher.Localizer.Apply("zh-CN");
        var source = XDocument.Load(Path.Combine(project, "src/CSNZ.Launcher/MainWindow.xaml"));
        var handlers = new HashSet<string> { "Click", "Loaded", "Closing", "TextChanged", "PasswordChanged", "Checked", "Unchecked" };
        foreach (var attribute in source.Descendants().Attributes().ToArray())
        {
            if (attribute.Name == x + "Class" || handlers.Contains(attribute.Name.LocalName)) attribute.Remove();
            else if (attribute.Value.StartsWith("Assets/", StringComparison.Ordinal))
                attribute.Value = Path.Combine(project, "assets", attribute.Value[7..]);
        }
        foreach (var scenario in new[] { ("login", 1200, 820), ("minimum", 1100, 760), ("wide", 1600, 900), ("registration-error", 1100, 760) })
        {
            var window = (Window)XamlReader.Parse(source.ToString());
            T Named<T>(string name) where T : FrameworkElement => (T)window.FindName(name);
            var root = (Grid)window.Content;
            root.Background = window.Background;
            System.Windows.Documents.TextElement.SetForeground(root, window.Foreground);
            System.Windows.Documents.TextElement.SetFontFamily(root, window.FontFamily);
            System.Windows.Documents.TextElement.SetFontSize(root, window.FontSize);
            var card = Named<Border>("LoginCard"); var server = Named<Border>("ServerControlCard"); var art = Named<Border>("LoginArtwork");
            Named<TextBox>("AccountBox").Text = "Preview123"; Named<TextBlock>("AccountHint").Visibility = Visibility.Collapsed;
            Named<PasswordBox>("PasswordInput").Password = "Preview9!"; Named<TextBlock>("PasswordHint").Visibility = Visibility.Collapsed;
            Named<TextBlock>("ServerStateText").Text = "Ready"; Named<TextBlock>("ServerStateText").Foreground = new SolidColorBrush(Color.FromRgb(41, 146, 118));
            Named<System.Windows.Shapes.Ellipse>("ServerDot").Fill = new SolidColorBrush(Color.FromRgb(41, 146, 118));
            Named<Border>("ServerBadge").Background = new SolidColorBrush(Color.FromRgb(237, 248, 243));
            Named<Border>("ServerBadge").BorderBrush = new SolidColorBrush(Color.FromRgb(213, 237, 225));
            Named<TextBlock>("ServerDetail").Text = "本地服务端已就绪 · 由登录器管理";
            if (scenario.Item1 == "registration-error")
            {
                Named<StackPanel>("ConfirmPanel").Visibility = Visibility.Visible;
                Named<Grid>("RememberRow").Visibility = Visibility.Collapsed;
                Named<TextBlock>("FormTitle").Text = "创建你的账号";
                Named<Button>("LoginTab").Background = Brushes.Transparent;
                Named<Button>("RegisterTab").Background = Brushes.White;
                Named<Button>("SubmitButton").Content = "创建账号  →";
                Named<Border>("FeedbackBox").Visibility = Visibility.Visible;
                Named<TextBlock>("FeedbackText").Text = "测试提示：两次输入的密码不一致，请重新确认。较长反馈不应挤出按钮，也不应破坏两侧底边对齐。";
            }
            window.Content = null;
            root.Measure(new Size(scenario.Item2, scenario.Item3));
            root.Arrange(new Rect(0, 0, scenario.Item2, scenario.Item3)); root.UpdateLayout();
            Point At(FrameworkElement element) => element.TransformToAncestor(root).Transform(new Point());
            double cardBottom = At(card).Y + card.ActualHeight, serverBottom = At(server).Y + server.ActualHeight;
            if (Math.Abs(cardBottom - serverBottom) > 0.1 || Math.Abs(At(card).Y - At(art).Y) > 0.1)
                throw new Exception("Columns are not aligned: " + scenario.Item1);
            if (art.Background is not ImageBrush brush || brush.Stretch != Stretch.UniformToFill || brush.AlignmentX != AlignmentX.Left
                || brush.ImageSource is not BitmapSource bitmap || bitmap.PixelWidth != 1920 || bitmap.PixelHeight != 1201)
                throw new Exception("Artwork is missing or distorted");
            var submit = Named<Button>("SubmitButton");
            if (At(submit).Y + submit.ActualHeight > cardBottom - 20 || card.ActualHeight < 580)
                throw new Exception("Login controls overflow the card");
            var render = new RenderTargetBitmap(scenario.Item2 * 2, scenario.Item3 * 2, 192, 192, PixelFormats.Pbgra32); render.Render(root);
            var encoder = new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(render));
            using (var stream = File.Create(Path.Combine(output, scenario.Item1 + ".png"))) encoder.Save(stream);
            Console.WriteLine($"PASS: {scenario.Item1} {scenario.Item2}x{scenario.Item3}, top={At(card).Y:F2}, both bottoms={cardBottom:F2}, artwork={art.ActualWidth:F2}x{art.ActualHeight:F2}, uniform scaling");
        }
        app.Shutdown();
    }
}
