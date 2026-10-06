using static Csnz.Launcher.Localizer;
using System.IO;

namespace Csnz.Launcher;

// Always use this executable's own payload, never a DLL found in the CWD or game folder.
public static class NativeBundle
{
    internal static string Extract(string component, string version, string[] names)
    {
        var directory = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "CSNZLauncher", "Native", component, version);
        Directory.CreateDirectory(directory);
        foreach (var name in names)
        {
            using var input = typeof(NativeBundle).Assembly.GetManifestResourceStream("Native." + name)
                ?? throw Error<InvalidDataException>("Error.BundleMissing", name);
            using var memory = new MemoryStream(); input.CopyTo(memory);
            var expected = memory.ToArray(); var target = Path.Combine(directory, name);
            if (!File.Exists(target))
            {
                // Publish only a complete file. Never overwrite a loaded DLL or another version.
                var temporary = target + "." + Guid.NewGuid().ToString("N") + ".tmp";
                try
                {
                    using (var output = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                    { output.Write(expected); output.Flush(true); }
                    try { File.Move(temporary, target); }
                    catch (IOException) when (File.Exists(target)) { /* another instance won; compare below */ }
                }
                finally { if (File.Exists(temporary)) File.Delete(temporary); }
            }
            if (!File.ReadAllBytes(target).AsSpan().SequenceEqual(expected))
                throw Error<InvalidDataException>("Error.BundleMismatch", directory);
        }
        return Path.Combine(directory, names[0]);
    }
}
