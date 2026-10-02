using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Markup;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using System.Xml.Linq;

internal static class Program
{
    [STAThread] private static void Main(string[] args)
    {
        // A synthetic WPF layout fixture, not the actual login window: no server,
        // account store, authentication, credential readback, or game launch.
        var app = new Application();
        XNamespace p = "http://schemas.microsoft.com/winfx/2006/xaml/presentation";
        var input = XDocument.Load(args[0]);
        var resources = new XElement(p + "ResourceDictionary", new XAttribute("xmlns", p.NamespaceName),
            new XAttribute(XNamespace.Xmlns + "x", "http://schemas.microsoft.com/winfx/2006/xaml"), input.Root!.Element(p + "Application.Resources")!.Elements());
        app.Resources = (ResourceDictionary)XamlReader.Parse(resources.ToString());
        var panel = new StackPanel { Margin = new Thickness(28) };
        panel.Children.Add(new TextBlock { Text = "Synthetic input rendering fixture", FontSize = 18, Margin = new Thickness(0, 0, 0, 16) });
        panel.Children.Add(new TextBox { Text = "Preview123", Height = 45, Margin = new Thickness(0, 0, 0, 18) });
        panel.Children.Add(new PasswordBox { Password = "TestPass9!", Height = 45, Padding = new Thickness(13, 11, 56, 11), Margin = new Thickness(0, 0, 0, 18) });
        panel.Children.Add(new TextBox { Text = "TestPass9!", Height = 45, Padding = new Thickness(13, 11, 56, 11), Margin = new Thickness(0, 0, 0, 18) });
        var window = new Window { Width = 450, Height = 320, Content = panel, Background = Brushes.White, ShowInTaskbar = false, WindowStyle = WindowStyle.None };
        window.Show();
        window.Dispatcher.Invoke(DispatcherPriority.Render, new Action(() => { }));
        window.UpdateLayout();
        var bitmap = new RenderTargetBitmap(900, 640, 192, 192, PixelFormats.Pbgra32); bitmap.Render(window);
        var encoder = new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(bitmap));
        using (var stream = File.Create(args[1])) encoder.Save(stream);
        int textViews = 0;
        void Dump(DependencyObject o, int depth)
        {
            if (o is FrameworkElement f && (o is TextBox || o is PasswordBox || o.GetType().Name.Contains("TextBoxView") || o is ScrollViewer))
                Console.WriteLine($"{o.GetType().Name} size={f.ActualWidth:F1}x{f.ActualHeight:F1} desired={f.DesiredSize} margin={f.Margin}");
            if (o is FrameworkElement view && o.GetType().Name == "TextBoxView")
            {
                textViews++;
                if (view.ActualHeight < 18 || view.ActualWidth < 200) throw new Exception("Input text is clipped by its template.");
            }
            if (o is Control control && (o is TextBox || o is PasswordBox))
            {
                if (control.Foreground is not SolidColorBrush brush || brush.Color != Colors.Black)
                    throw new Exception("Input foreground must be black.");
            }
            for (int i = 0; i < VisualTreeHelper.GetChildrenCount(o); i++) Dump(VisualTreeHelper.GetChild(o, i), depth + 1);
        }
        Dump(window, 0);
        if (textViews != 3) throw new Exception("Expected all three input renderers.");
        Console.WriteLine("PASS: account, masked password, revealed password have visible text bounds and black foreground.");
        window.Close(); app.Shutdown();
    }
}
