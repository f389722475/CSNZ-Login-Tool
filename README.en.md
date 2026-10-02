# CSNZ Desktop Launcher 1.0.7

[简体中文](README.md) | **English**

A standalone Windows x86 / .NET 8 WPF desktop application with a SaaS-style interface. No BAT files, Python, Frida, browser, or separate .NET installation is required to use it.

## Usage

Use the **CN / EN** button in the top-right corner, to the left of the server address, to switch instantly between Simplified Chinese and English. The active language is highlighted in purple, and your selection is saved automatically. Translation covers login, registration, weapon mods, settings, help, confirmation dialogs, server status, and error messages. Standard buttons in the system folder picker still follow the Windows display language.

Run `dist/CSNZ_Launcher_1.0.7/CSNZ登录器.exe`. The native patch and licenses are embedded, so you can also copy and run the EXE on its own; a separate `Native` folder is no longer required.

When the patch is enabled for the first time, the launcher extracts its embedded files to `%LOCALAPPDATA%\CSNZLauncher\Native\GigaBreakLE\0.7.4-native-r2`. Administrator privileges are not required. It does not search the working directory or game directory for unknown DLLs. If the cached files do not match, loading is refused; existing files are not overwritten. Patch runtime logs are also stored in this directory.

The game directory is no longer hard-coded. Place the EXE in the game root directory (the folder containing `Bin`) or in `Bin` itself. Detection uses the EXE's location, not the startup working directory. If you move it to another complete game installation, the adjacent installation takes priority; an explicitly selected external directory is still retained. If the game cannot be found, select its root directory under **Settings**. A `Bin` directory or `Bin/CSOLauncher.exe` path is also accepted. The launcher does not copy, overwrite, or modify original game files, server configuration, or startup scripts.

- Opening the launcher automatically starts `game directory/Server/CSNZ_Server.exe` in the background, without a console window.
- The top-right indicator shows a red dot when stopped, a spinner while starting, and a green dot when ready. A spinner remains visible during graceful shutdown, and the stopped state is shown only after the server exits. The Chinese UI retains the labels `Stop`, `Starting`, `Ready`, and `Stoping`. Readiness requires a real CSNZ protocol banner response, not just a running process.
- An existing server is reused. The launcher does not take ownership of it and will not stop it.
- Graceful shutdown uses the original server's `shutdown` console command and waits for a normal exit. A timeout does not trigger a forced termination. Stopping the server is disabled while the game is running.
- When closing the launcher, you can choose to gracefully stop the server it started or leave that server running. The launcher will not terminate a running game.
- Automatic login is handled by the embedded `CSNZLauncherBridge.dll`, independently of the weapon-mod toggle. At the game's original login callback, it sends a standard `/login` authentication packet over the game's existing connection, rather than relying on the chat UI, which could discard the command. The server still validates the password normally; incorrect passwords are rejected. The `-disableauthui` option remains enabled, so credentials do not need to be entered again. First-time character creation for a new account still takes place in the game. The launcher does not log in through a separate session beforehand, grant duplicate login rewards, or rewrite the database.
- The automatic-login component strictly checks the SHA-256 hashes of the current `CSOLauncher.exe` and `hw.dll`, and verifies the PE, function entry point, and current two-argument ABI at runtime. Unknown versions are refused; original game binaries are not overwritten. The component is embedded in the EXE and extracted to `%LOCALAPPDATA%\CSNZLauncher\Native\LauncherAuth\1.0.0`.
- Registration uses the selected server's actual network protocol. Success is displayed only after an explicit success response. Duplicate accounts, invalid input, registration limits per IP, and database errors have separate messages. The launcher does not directly read or write the account database.
- Usernames must contain 5–15 ASCII letters or digits. Passwords must contain 5–15 printable ASCII characters; spaces, double quotes, and backslashes are not supported. A registration password cannot consist entirely of digits. These are the field constraints of the current server authentication commands.
- Remember username, remember password, show/hide password, password confirmation, clear saved credentials, server address/port/TLS settings, and minimize after launch are supported.
- The `LE 0.7.4-native-r2` native patch is enabled by default and can be disabled under **Weapon Mods**. It strictly checks the current CSNZ0930 PE version and ABI. A mismatch prevents the patch from starting rather than applying offsets anyway. The old LEGuard must not run at the same time.

## Password and Network Boundaries

Configuration is stored in `%LOCALAPPDATA%\CSNZLauncher\settings.json`. The username and preferences are readable. A password is saved only when **Remember password** is selected, and is encrypted using **Windows DPAPI for the current user**, bound to the server address, port, TLS mode, and username. Changing servers clears the input and previous credentials. Disabling password retention removes the encrypted password value.

Automatic-login credentials **no longer appear on the game's command line**. The launcher temporarily passes them to the game process it has just started, and the component clears its copy after sending the authentication request. Credentials are not written to plaintext files or logs. The same Windows user or an administrator may still be able to read process memory; DPAPI does not make secrets inaccessible to administrators. The original launcher's stdout/stderr are discarded rather than saved.

For registration outside the game, unencrypted connections are allowed only for `localhost` or loopback IP addresses. Remote registration requires TLS and successful system validation of the certificate and hostname; there is no accept-any-certificate bypass. Remote non-TLS login through the original game first requires explicit confirmation. The server's TLS configuration and the launcher's setting must match. `ready` means a CSNZ banner was received; it does not mean the account is authenticated or the TLS certificate has been validated.

The server has no email/SMS password-recovery interface. **Forgot password** therefore directs users to the administrator. It does not pretend to send a recovery email or bypass account verification.

## Build

The .NET 8 SDK is required. Prebuilt embedded native components are included in the source package. Rebuilding them with `-RebuildNative` also requires Visual Studio 2022 C++ x86 Build Tools.

```powershell
pwsh -NoProfile -File .\build.ps1
```

The self-contained output is written to `dist/CSNZ_Launcher_1.0.7`. Administrator privileges are not required by default at runtime. Launcher source is in `src/CSNZ.Launcher`; targeted tests are in `tests/Launcher.Smoke`. Those tests explicitly require an isolated server directory and must not use the production database.

`assets/Native/GigaBreakLE.dll` is this project's own native weapon patch. Its complete, rebuildable source is in `native-mod-source`. The automatic-login component's source is in `native-launcher-source` and reuses the bundled MinHook. The MinHook license is included. Original game DLLs, asset archives, server executables, account data, and upstream reverse-engineering output are not included.

## Protocol References

Out-of-game registration is implemented as a small, independent client using only the public wire format. It does not copy the server's account database or password-processing code. The upstream versions checked were:

- [JusicP/CSNZ_Server](https://github.com/JusicP/CSNZ_Server/tree/6e5275cb4bf4178b391a1db09dd604c8eaa1420b): `src/net/tcpserver.cpp` (connection banner precedes TLS), `src/net/sendpacket.cpp` (4-byte header), `src/manager/usermanager.cpp` (version and registration requirements), `src/manager/channelmanager.cpp` (registration requests and results), and `src/servercommands.h` (graceful server shutdown).
- [JusicP/Launcher_CSNZ](https://github.com/JusicP/Launcher_CSNZ/tree/9b1872aa32b748a1fb95e129807f50008057318b): the old automatic-login call in `hook.cpp` was used only to investigate the failure. Version 1.0.6 no longer passes credentials on the command line.
- The local `Server/Documentation/Client Launcher Info.txt` and `Server/ChangeLog.txt`, version `30.09.2026_EoS`. The old `-register` argument has been removed upstream; this obsolete entry point is not used.

Protocol sequence: raw ASCII banner `~SERVERCONNECTED\n` → optional TLS handshake → `0x55 | seq:u8 | bodyLen:u16LE | body`. A version packet is sent first (launcher version 67 / game version 26 / current client.dll timestamp). After a successful version response, the client sends UMsg 67, LobbyChat 1, and a NUL-terminated `/register` command, then waits for an explicit registration result in UMsg 67 / MsgBox 10. Receiving handles TCP fragmentation and enforces length limits. Unknown responses and timeouts are not treated as success.

## Verification

### 1.0.7 — Chinese/English Switching

- Embedded bilingual resources require no online translation, external language files, or restart. All 189 strings are centralized in `src/CSNZ.Launcher/Strings.json`, with matching placeholders in Chinese and English.
- Switching languages does not rebuild the page, clear the username/password/confirmation fields, change password visibility, or submit unsaved settings. It saves only the language preference, without re-encrypting the password or changing the authentication binding, server address, or mod toggle.
- Existing registration results, errors, and asynchronous server-status messages retain message identifiers and update when the language changes. Yes/No/Cancel/OK buttons in confirmation dialogs follow the selected language, independently of the Windows language.
- Navigation starts at a consistent position in both languages, with aligned buttons and input fields. English remember options and the recovery link use separate rows to avoid overlap in narrow windows. The directory browse button has enough space for English text.
- All 35 offline localization checks passed, including 12 bilingual page renders using production WPF controls at 1100×760, 1200×820, and 1600×900, error messages, input preservation, language persistence, and dialog buttons. The 5 graceful-shutdown and 13 portable-path regression checks also passed.
- This update changes only launcher presentation and text. The native automatic-login component, weapon-patch binaries, and game protocol are unchanged. These UI checks are not presented as new gameplay or network-authentication acceptance tests.

### 1.0.6 — Automatic Login and Portable Paths

- The original failure was reproduced in the installed game directory: the initial connection and native Login packet were present, but no actual `/login` authentication packet was sent. Only packet IDs/lengths and boolean results were captured, not credentials.
- The fix does not rewrite any EXE/DLL in `Bin`, use proxy forwarding, rely on arbitrary delays, or bypass server password validation. The authentication component and Giga Break LE can work independently.
- Path detection and x86/x64 process-path verification passed 13 targeted checks without touching saved encrypted credentials. The EXE's icon resources were already complete. Startup only notifies Explorer to refresh that EXE; it does not delete the system icon cache or restart Explorer.
- The gray C icon cached for the old local path required one additional Shell icon-cache invalidation notification. The character icon was then confirmed in Explorer. This maintenance step did not delete cache files or change system settings, and is not part of every startup. See the [Microsoft SHChangeNotify documentation](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shchangenotify) for the notification used.
- Fixed WPF Closing re-entry when closing the window after graceful server shutdown. The final close is scheduled after the current event returns, rather than calling Close synchronously inside Closing.
- Runtime acceptance used an isolated account database and synthetic accounts; the production account database was not read or written for those tests. Direct-connection checks with the actual `D:\CS\CSNZ0930` client passed: authentication and channel entry worked with weapon mods both enabled and disabled, and an incorrect password was explicitly rejected. The real game UI then loaded the notice, inventory items, and shop list instead of remaining at Connecting to server. No purchases were made and no survey was submitted.
- With the single EXE moved to a new directory containing Chinese characters and spaces, and launched from `C:\Windows\System32`, it correctly detected the adjacent game. The actual client in the new location successfully logged in using a synthetic password containing `@`, `-`, and `!`. Only valid manually selected paths are retained; detection does not depend on a hard-coded default path.
- The transparent-forwarding check in 1.0.2 did not cover the original direct-connection timing. This release added direct-connection acceptance without a proxy. Combat behavior and remote TLS are outside the scope of this local login fix's acceptance tests.

### 1.0.5 — Login Page Layout

- The title bar now shows only CSNZ. The left-side welcome text and old illustration were replaced by the original background image supplied by the user, embedded in the EXE without modifying the original image.
- The image fills its area while preserving its aspect ratio and prioritizing the character on the left, without horizontal or vertical stretching. The image, server controls, and login form use consistent corner radii and spacing.
- The two columns share their layout: the image's top edge aligns with the login card's top edge, and the server controls' bottom edge aligns with the login card's bottom edge. The login button sits near the bottom of the card. Both columns grow together when registration or error messages appear.
- Real WPF layout renders passed at 1200×820, 1100×760, and 1600×900, plus a registration-error case. The bottom edges differed by less than 0.1 DIP. Synthetic previews do not read account settings or start the server or game.

### 1.0.4 — Shutdown State and Branding Icon

- Graceful shutdown has a separate internal Stopping state. The UI label in this release was `Stoping`, as requested. Repeated operations are disabled while waiting for normal exit; a failure restores the state rather than forcibly terminating the process.
- The top-left logo and application icon were replaced with a high-resolution redraw of the user's cstrike.ico. The “Game Workspace” text was removed, and CSNZ and the icon were vertically centered. The original ICO was preserved.
- The high-resolution source image is stored at assets/cstrike-hd-v1.png, with an actual size of 1254 × 1254. A cstrike-hd-v1.ico containing 16–256-pixel frames is also provided. It was produced with the built-in image-generation tool. No model was specified or claimed to be image2.5, and the actual output was not described as 4K.

### 1.0.3 — UI Simplification

- Status labels use initial capitals, with vertically aligned dots and text.
- Removed the long weapon-card description, bottom information panel, login-card footer note, and footer text, while retaining necessary error feedback.
- The weapon page shows the number of loaded weapons and updates it as selections change. Selections still take effect on the next game launch; authentication, persistence, and native weapon logic are unchanged.

### Weapon Mods Page and Consistent Naming

- Sidebar order: Login Center → Weapon Mods → Settings → Help. Weapon selections are saved automatically and loaded on the next launch; they do not hot-switch a running game.
- **Giga Break LE** is currently the only weapon with a loadable, partially reconstructed implementation, so the list contains one item. The other five investigated weapons are not presented as working fixes.
- The mod's display name is consistently Giga Break LE, and code/file identifiers use `GigaBreakLE` or `giga_break_le`. DLL names, native source filenames, exported functions, resource versions, launch-control events, and release naming were updated together.
- The original game's `ef_beamgunle_wingman.mdl` resource key and the legacy mutex key preventing old and new mods from loading together are compatibility contracts and retain their original values. Historical backups and older release packages were not rewritten.
- Real desktop checks confirmed that deselecting/selecting the mod immediately persists the setting; it was restored to enabled afterward. The renamed DLL initialized to ready in an isolated real-game run. After normalizing naming differences, its weapon-logic source matched the previous version.

### 1.0.2 — Automatic Login

- Tests using a complete isolated game copy, a separate real server, and synthetic accounts confirmed that the game automatically sent `/login` once, with an exact username/password match. The server explicitly accepted the login, and the game automatically reached character naming for a new account.
- `tests/AutoLogin.E2E` observed the game's own session through transparent local forwarding, recording only packet types and match booleans, not credentials or raw packets. Isolated test data and game files are not included in the source package.
- Added `-disableauthui` to disable the separate VGUI login window. This does not introduce a second authentication session, fake login success, or modify the server or original game files.
- An incorrect password was explicitly rejected on the same actual game path. Hiding the login window does not let invalid accounts bypass authentication. Automatic login also passed a combined check with the newly named Giga Break LE mod enabled.

### 1.0.1 — Fixes

- Fixed input-control templates applying Padding twice, which reduced the text area height to zero. Username, masked password, visible password, and settings inputs use black text and carets. Removed the introduction card at the bottom of the sidebar.
- Embedded the native patch in the EXE while preserving weapon logic and PE/ABI checks. Standalone EXE preflight checks do not read account settings, start the server or game, or install hooks.
- Run `CSNZ登录器.exe --check-native <game root directory> <absolute path to a new report file>`. Exit codes: 0 for success, 1 for failure, and 2 for invalid arguments or report paths. Existing report files will not be overwritten.
- Verified synthetic WPF renders for three input variants (text area restored from 0 to 21 DIP), DLL extraction and preloading from a standalone EXE in a path containing Chinese characters and spaces, preflight against the actual game installation, the updated window, and server `ready`. No real account was submitted and the game was not launched in these checks.

### 1.0.0 — Earlier Verification

- Release build, x86 native DLL preloading, and export/PE version checks passed.
- An isolated real CSNZ_Server passed automatic startup, protocol readiness, successful registration, duplicate-account rejection, incorrect-password rejection, new-account login acceptance, and normal shutdown. The test database was separate from the production database; production accounts were not read or modified.
- DPAPI encryption/decryption, binding-mismatch rejection, absence of plaintext passwords in configuration, input constraints, packet construction, and launch arguments without a shell passed a total of 17 targeted checks. Evidence is in `evidence/smoke-result.txt` and is not distributed in release packages.
- The desktop window was opened and the local production server was confirmed to reach `ready` automatically, then stopped normally by the launcher. No user account was used to log in, and no gameplay test was started.
- These are the historical verification boundaries for 1.0.0. Version 1.0.2 added actual in-game automatic-login checks. **The native patch's combat behavior still requires user acceptance**; account login or DLL loading is not treated as a complete combat test.
