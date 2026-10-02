using System.ComponentModel;
using System.Globalization;
using System.IO;
using System.Net.Sockets;
using System.Security.Authentication;
using System.Text.Json;
using System.Windows;

namespace Csnz.Launcher;

// Keep message identity and arguments, not the rendered language. Existing
// feedback and asynchronous server status can then change language in place.
public sealed record UiText(string Key, params object[] Arguments)
{
    public override string ToString() => Localizer.Text(Key, Arguments);
}

public static class Localizer
{
    public sealed record Translation(string Zh, string En);
    private const string ErrorTextKey = "CSNZ.LocalizedMessage";
    private static readonly Dictionary<string, Translation> catalog = LoadCatalog();
    private static string language = "zh-CN";
    public static string Language => Volatile.Read(ref language);
    public static IReadOnlyDictionary<string, Translation> Catalog => catalog;
    public static string Normalize(string? value) => value == "en" ? "en" : "zh-CN";
    public static UiText Msg(string key, params object[] arguments) => new(key, arguments);
    public static string Text(string key, params object[] arguments)
    {
        if (!catalog.TryGetValue(key, out var entry)) throw new ArgumentException("Unknown localization key: " + key);
        var format = Language == "en" ? entry.En : entry.Zh;
        return arguments.Length == 0 ? format : string.Format(CultureInfo.InvariantCulture, format,
            arguments.Select(a => a switch { UiText text => text.ToString(), Exception error => Describe(error).ToString(), _ => a }).ToArray());
    }
    public static void Apply(string? value)
    {
        Volatile.Write(ref language, Normalize(value));
        if (Application.Current is { } app)
        {
            app.Dispatcher.VerifyAccess();
            foreach (var key in catalog.Keys) app.Resources["Ui." + key] = Text(key);
        }
    }
    // Retain the original exception type for callers, while keeping a reusable
    // language-neutral message for the UI. No protocol or native behavior changes.
    public static T Error<T>(string key, params object[] arguments) where T : Exception
    {
        var text = Msg(key, arguments);
        return WithText((T)Activator.CreateInstance(typeof(T), new object[] { text.ToString() })!, text);
    }
    public static T WithText<T>(T exception, UiText text) where T : Exception
    { exception.Data[ErrorTextKey] = text; return exception; }
    public static UiText Describe(Exception error)
    {
        if (error.Data[ErrorTextKey] is UiText text) return text;
        return error switch
        {
            OperationCanceledException => Msg("Error.Canceled"),
            AuthenticationException => Msg("Error.Tls"),
            SocketException => Msg("Error.Connection"),
            Win32Exception e => Msg("Error.Windows", e.NativeErrorCode),
            UnauthorizedAccessException => Msg("Error.Access"),
            IOException => Msg("Error.IO"),
            _ => Msg("Error.System", error.GetType().Name, $"0x{error.HResult:X8}")
        };
    }
    private static Dictionary<string, Translation> LoadCatalog()
    {
        using var stream = typeof(Localizer).Assembly.GetManifestResourceStream("Localization.Strings.json")
            ?? throw new InvalidOperationException("Missing localization catalog");
        return JsonSerializer.Deserialize<Dictionary<string, Translation>>(stream)
            ?? throw new InvalidOperationException("Invalid localization catalog");
    }
}
