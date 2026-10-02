# CSNZ desktop authentication bridge 1.0.0

This is launcher infrastructure, not a weapon mod. It is embedded and extracted by the desktop EXE, and never patches game files on disk.

The original CSOLauncher wrapper routes `/login` through `ChattingManager::PrintToChat`. On the observed failing direct connection this callback ran with populated credentials and a ready chat manager, but no UMsg authentication frame was sent; only the native ID 3 packet was sent. Buffered diagnostic send observation retained original timing. A logging proxy hid the failure, so proxy-only tests were insufficient.

The bridge replaces only that wrapper and sends the normal UMsg 67 / LobbyChat 1 / NUL-terminated `/login` body with the engine's existing packet serializer and live game connection, preserving transport sequence, TLS and the server's authentication decision. It then calls the native login continuation using its verified **two-argument stdcall ABI (RET 8)**, rather than the stale upstream three-argument declaration. No authentication reply is fabricated. Unknown binary versions fail closed.

Credentials arrive in a 36-byte initialization structure (`uint32 size; char account[16]; char password[16]`) through private child-process memory, never the command line. Temporary buffers are cleared. A non-secret per-process event reports only that the request was sent or failed, not that the server accepted it.

Build with `build.cmd` using MSVC x86 and the included `../native-mod-source/third_party/minhook` sources. Runtime profile: launcher SHA256 `8C2EDF9A2C64885E87E4622C63E8015B38B793C260F9E12DC602AD5A62397CFD`; engine SHA256 `52345CBE9B52F75B1C1DA70254C718D78F2EFF746AC8397D10251A9D134A32E9`. Full hashes are checked before launch; PE/entry/epilogue guards are checked in-process. The helper is never applied to a user's already-running game.

The bridge and weapon DLL have separate entry points and cache directories. Closing the desktop launcher does not disconnect the game or unload either component. Current support is CSNZ0930 only; remote TLS functionality uses the engine transport but was not exercised by the local regression suite.
