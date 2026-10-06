using System.Diagnostics;
using System.IO;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Text.Json.Serialization;
using static Csnz.Launcher.Localizer;

namespace Csnz.Launcher;
[JsonConverter(typeof(JsonStringEnumConverter))]
public enum MultiplayerMode { Local, Lan, Internet, Join }
public static class Multiplayer
{
    public static MultiplayerMode Mode(LauncherSettings s) => s.NetworkMode ?? (CsnzProtocol.IsLoopback(s.Host) ? MultiplayerMode.Local : MultiplayerMode.Join);
    public static bool IsHost(LauncherSettings s) => Mode(s) != MultiplayerMode.Join;
    public static bool NeedsDedicated(LauncherSettings s) => IsHost(s) && (Mode(s) != MultiplayerMode.Local || WeaponBundle.Selected(s).Length != 0);
    public static string AdvertisedAddress(LauncherSettings s) => Mode(s) == MultiplayerMode.Local ? "127.0.0.1" : s.AdvertisedGameAddress.Trim();
    public static string[] LocalAddresses() => NetworkInterface.GetAllNetworkInterfaces().Where(n => n.OperationalStatus == OperationalStatus.Up && n.NetworkInterfaceType != NetworkInterfaceType.Loopback)
        .SelectMany(n => n.GetIPProperties().UnicastAddresses).Select(a => a.Address).Where(a => a.AddressFamily == AddressFamily.InterNetwork && !IPAddress.IsLoopback(a)).Select(a => a.ToString()).Distinct().ToArray();
    public static void Validate(LauncherSettings s)
    {
        if (!Enum.IsDefined(Mode(s)) || Uri.CheckHostName(s.Host) == UriHostNameType.Unknown || s.Port is < 1 or > 65535) throw Error<InvalidOperationException>("Error.Host");
        if (!IsHost(s)) return;
        if (!CsnzProtocol.IsLoopback(s.Host)) throw Error<InvalidOperationException>("Network.HostLoopback");
        if (s.DedicatedWeaponPort is < 1 or > 65535 || s.DedicatedWeaponPort == s.Port) throw Error<InvalidOperationException>("Error.Port");
        if (Mode(s) == MultiplayerMode.Local) return;
        if (!IPAddress.TryParse(AdvertisedAddress(s), out var ip) || ip.AddressFamily != AddressFamily.InterNetwork || IPAddress.IsLoopback(ip) || ip.Equals(IPAddress.Any) || ip.Equals(IPAddress.Broadcast) || (ip.GetAddressBytes()[0] == 0 || ip.GetAddressBytes()[0] >= 224))
            throw Error<InvalidOperationException>("Network.AddressInvalid");
    }
    public static ProcessStartInfo DedicatedStartInfo(LauncherSettings s)
    {
        Validate(s);
        if (!IsHost(s)) throw Error<InvalidOperationException>("Network.JoinNoServer");
        var info = new ProcessStartInfo(Path.Combine(s.GameRoot, "Bin", "CSOHLDS.exe")) { WorkingDirectory = Path.Combine(s.GameRoot, "Bin"), UseShellExecute = false, WindowStyle = ProcessWindowStyle.Hidden };
        foreach (var arg in new[] { "-ip", "127.0.0.1", "-lobbyport", s.Port.ToString(), "-hostip", AdvertisedAddress(s), "-port", s.DedicatedWeaponPort.ToString(), "-pingboost", "4", "-logfile", "csnz-native-weapons-" + DateTime.Now.ToString("yyyyMMdd-HHmmss") }) info.ArgumentList.Add(arg);
        if (s.UseTls) info.ArgumentList.Add("-usessl");
        return info;
    }
}
