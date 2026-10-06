using System.IO;
using System.Reflection.PortableExecutable;
using static Csnz.Launcher.Localizer;

namespace Csnz.Launcher;

public static class GameCompatibility
{
    // Original game files are never rewritten. Signing, timestamps, resources
    // and overlays may differ without changing the engine ABI. Native bridges
    // verify the actual code/vtables before installing any hooks.
    public static void ValidateFile(string path, int? imageSize = null)
    {
        if (!File.Exists(path)) throw Error<FileNotFoundException>("Native.GameFileMissing", path);
        try
        {
            using var input = File.OpenRead(path);
            using var reader = new PEReader(input);
            var headers = reader.PEHeaders;
            var pe = headers.PEHeader;
            if (headers.CoffHeader.Machine != Machine.I386 || pe == null || pe.Magic != PEMagic.PE32 || pe.SizeOfImage <= 0)
                throw Error<InvalidDataException>("Native.GameFormat", path);
            if (imageSize.HasValue && pe.SizeOfImage != imageSize.Value)
                throw Error<InvalidDataException>("Native.BuildMismatch", Path.GetFileName(path));
        }
        catch (BadImageFormatException)
        {
            throw Error<InvalidDataException>("Native.GameFormat", path);
        }
    }
}
