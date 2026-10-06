using static Csnz.Launcher.Localizer;
using System.Buffers.Binary;
using System.IO;
using System.Net;
using System.Net.Security;
using System.Net.Sockets;
using System.Text;
using System.Text.RegularExpressions;
using System.Security.Authentication;

namespace Csnz.Launcher;
public record RegistrationResult(bool Success, UiText MessageText)
{ public string Message => MessageText.ToString(); }

// Small independent wire-format client; never reads/writes the game's account database.
public static class CsnzProtocol
{
    public static readonly byte[] Greeting = Encoding.ASCII.GetBytes("~SERVERCONNECTED\n");
    public static bool IsLoopback(string host) => string.Equals(host, "localhost", StringComparison.OrdinalIgnoreCase) || (IPAddress.TryParse(host, out var ip) && IPAddress.IsLoopback(ip));
    public static void ValidateCredentials(string account, string password, bool registering)
    {
        if (!Regex.IsMatch(account, "^[a-zA-Z0-9]{5,15}$")) throw Error<InvalidOperationException>("Error.Account");
        // Original client wraps credentials into a whitespace-delimited chat command.
        if (password.Length is < 5 or > 15 || password.Any(c => c < 33 || c > 126 || c is '\"' or '\\'))
            throw Error<InvalidOperationException>("Error.Password");
        if (registering && password.All(char.IsDigit)) throw Error<InvalidOperationException>("Error.NumericPassword");
    }
    public static byte[] Frame(byte sequence, ReadOnlySpan<byte> body)
    {
        if (body.Length is < 1 or > 65531) throw Error<InvalidDataException>("Error.PacketLength");
        var bytes = new byte[body.Length + 4]; bytes[0] = 0x55; bytes[1] = sequence;
        BinaryPrimitives.WriteUInt16LittleEndian(bytes.AsSpan(2), (ushort)body.Length); body.CopyTo(bytes.AsSpan(4)); return bytes;
    }
    private static byte[] VersionBody(uint timestamp)
    {
        var bytes = new byte[12]; bytes[0] = 0; bytes[1] = 67;
        BinaryPrimitives.WriteUInt16LittleEndian(bytes.AsSpan(2), 26);
        BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(4), timestamp); return bytes;
    }
    public static async Task<bool> ProbeAsync(string host, int port, CancellationToken ct)
    {
        try
        {
            using var timeout = CancellationTokenSource.CreateLinkedTokenSource(ct); timeout.CancelAfter(TimeSpan.FromSeconds(2));
            using var tcp = new TcpClient(); await tcp.ConnectAsync(host, port, timeout.Token);
            var banner = new byte[Greeting.Length]; await tcp.GetStream().ReadExactlyAsync(banner, timeout.Token);
            return banner.AsSpan().SequenceEqual(Greeting);
        }
        catch (Exception e) when (e is SocketException or IOException or OperationCanceledException) { return false; }
    }
    public static async Task<RegistrationResult> RegisterAsync(LauncherSettings settings, string account, string password, uint timestamp, CancellationToken ct, bool allowUnencryptedRemote = false)
    {
        ValidateCredentials(account, password, true);
        if (!settings.UseTls && !IsLoopback(settings.Host) && !allowUnencryptedRemote)
            throw Error<InvalidOperationException>("Error.RemoteTls");
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(ct); deadline.CancelAfter(TimeSpan.FromSeconds(12));
        var token = deadline.Token;
        using var tcp = new TcpClient { NoDelay = true }; await tcp.ConnectAsync(settings.Host, settings.Port, token);
        var net = tcp.GetStream(); var banner = new byte[Greeting.Length]; await net.ReadExactlyAsync(banner, token);
        if (!banner.AsSpan().SequenceEqual(Greeting)) throw Error<InvalidDataException>("Error.Banner");
        Stream stream = net;
        SslStream? tls = null;
        try
        {
            if (settings.UseTls)
            {
                // This server sends its plaintext greeting BEFORE beginning TLS.
                tls = new SslStream(net, true);
                await tls.AuthenticateAsClientAsync(new SslClientAuthenticationOptions { TargetHost = settings.Host, EnabledSslProtocols = SslProtocols.Tls12 | SslProtocols.Tls13 }, token);
                stream = tls; // Use normal Windows certificate/name validation; never accept-all.
            }
            await stream.WriteAsync(Frame(1, VersionBody(timestamp)), token);
            var version = await ReadFrameAsync(stream, 1, token);
            if (version.Length != 2 || version[0] != 0 || version[1] != 0)
                throw Error<InvalidDataException>("Error.ProtocolVersion");
            var command = Encoding.ASCII.GetBytes($"/register {account} {password}\0");
            var body = new byte[command.Length + 2]; body[0] = 67; body[1] = 1; command.CopyTo(body, 2);
            try { await stream.WriteAsync(Frame(2, body), token); }
            finally { Array.Clear(command); Array.Clear(body); }
            for (int seq = 2; seq < 10; seq++)
            {
                var reply = await ReadFrameAsync(stream, (byte)seq, token);
                if (reply.Length < 3 || reply[0] != 67 || reply[1] != 10) continue;
                var end = Array.IndexOf(reply, (byte)0, 2);
                if (end < 0) throw Error<InvalidDataException>("Error.RegisterTruncated");
                var text = Encoding.UTF8.GetString(reply, 2, end - 2);
                return MapRegistrationReply(text);
            }
            throw Error<InvalidDataException>("Error.RegisterUnknown");
        }
        finally { if (tls != null) await tls.DisposeAsync(); }
    }
    public static RegistrationResult MapRegistrationReply(string text) => text switch
    {
        "You have successfully registered." => new(true, Msg("Registration.Success")),
        "User with this username already exists." => new(false, Msg("Registration.Duplicate")),
        "You have exceeded the account limit for one IP" => new(false, Msg("Registration.Limit")),
        "Username must contain at least 5 characters and not more than 15, English letters." => new(false, Msg("Registration.Account")),
        "Password must contain at least 5 characters and not more than 15, not only numbers" => new(false, Msg("Registration.Password")),
        "DB_QUERY_FAILED" => new(false, Msg("Registration.Database")),
        _ => new(false, Msg("Registration.Unknown"))
    };
    public static async Task<byte[]> ReadFrameAsync(Stream stream, byte sequence, CancellationToken ct)
    {
        var header = new byte[4]; await stream.ReadExactlyAsync(header, ct);
        int length = BinaryPrimitives.ReadUInt16LittleEndian(header.AsSpan(2));
        if (header[0] != 0x55 || header[1] != sequence || length is < 1 or > 65531)
            throw Error<InvalidDataException>("Error.PacketFormat");
        var body = new byte[length]; await stream.ReadExactlyAsync(body, ct); return body;
    }
}
