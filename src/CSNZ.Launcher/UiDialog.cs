using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace Csnz.Launcher;

// Native MessageBox buttons follow the Windows language, not the selected app
// language. Use the same WPF resources for the launcher-owned dialogs instead.
public sealed class UiDialog : Window
{
    public MessageBoxResult Result { get; private set; } = MessageBoxResult.Cancel;
    public UiDialog(UiText message, UiText title, MessageBoxButton buttons)
    {
        Title = title.ToString(); Width = 550; SizeToContent = SizeToContent.Height;
        ResizeMode = ResizeMode.NoResize; WindowStartupLocation = WindowStartupLocation.CenterOwner;
        ShowInTaskbar = false; Background = Brushes.White;
        FontFamily = new FontFamily("Segoe UI, Microsoft YaHei UI"); FontSize = 14;
        Foreground = (Brush)Application.Current.Resources["Ink"];
        var panel = new StackPanel { Margin = new Thickness(26) };
        panel.Children.Add(new TextBlock { Text = title.ToString(), FontSize = 20, FontWeight = FontWeights.Bold, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 0, 0, 16) });
        panel.Children.Add(new ScrollViewer { MaxHeight = 430, VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
            Content = new TextBlock { Text = message.ToString(), TextWrapping = TextWrapping.Wrap, LineHeight = 23 } });
        var actions = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right, Margin = new Thickness(0, 24, 0, 0) };
        var choices = buttons switch
        {
            MessageBoxButton.YesNoCancel => new[] { MessageBoxResult.Yes, MessageBoxResult.No, MessageBoxResult.Cancel },
            MessageBoxButton.YesNo => new[] { MessageBoxResult.Yes, MessageBoxResult.No },
            _ => new[] { MessageBoxResult.OK }
        };
        foreach (var choice in choices)
        {
            var button = new Button { Content = Localizer.Text("Dialog." + choice), MinWidth = 84,
                Margin = new Thickness(8, 0, 0, 0),
                IsDefault = choice is MessageBoxResult.OK or MessageBoxResult.Cancel || (buttons == MessageBoxButton.YesNo && choice == MessageBoxResult.No),
                IsCancel = choice is MessageBoxResult.OK or MessageBoxResult.Cancel || (buttons == MessageBoxButton.YesNo && choice == MessageBoxResult.No) };
            button.Click += (_, _) => { Result = choice; Close(); };
            actions.Children.Add(button);
        }
        panel.Children.Add(actions); Content = panel;
    }
    public static MessageBoxResult Show(Window? owner, UiText message, UiText title, MessageBoxButton buttons = MessageBoxButton.OK, MessageBoxImage image = MessageBoxImage.None)
    {
        var dialog = new UiDialog(message, title, buttons);
        if (owner != null) dialog.Owner = owner;
        else dialog.WindowStartupLocation = WindowStartupLocation.CenterScreen;
        dialog.ShowDialog(); return dialog.Result;
    }
}
