# 登录器源码入口

主工程：`CSNZ.Launcher/CSNZ.Launcher.csproj`（.NET 8、Windows x86、WPF）。

GitHub 版提供完整工程源码和自编译原生组件，不带 Skill、私人设计文档、账号数据或游戏 NAR。`weapon-mods` 可独立构建和离线检查；重建完整登录器需要另行合法准备与资源清单匹配的 `weapon-mods/local-assets/fixtrike.nar`，自动资源提取/重建流程尚未提供，缺少资源时构建会拒绝。不要将下面的完整本地交付与公开仓库内容混为一谈。

主要实现：
- `MainWindow.xaml` / `.xaml.cs`：界面、武器选择和启停流程。
- `CsnzProtocol.cs`、`AuthBridge.cs`：大厅协议与自动登录桥调用。
- `GameServices.cs`、`DedicatedConsole.cs`、`Multiplayer.cs`：服务端控制及本机/局域网/VPN/互联网/加入模式。
- `NativeWeaponServer.cs`、`WeaponBundle.cs`、`WeaponAssets.cs`：原生武器加载、包校验和本地资源准备。
- `AwakeningPlugin.cs`、`SettingsStore.cs`、`Localizer.cs`、`Strings.json`：觉醒插件、偏好/凭据保护和双语文本。

完整源码还包括根目录下：
- `native-launcher-source/`：C++ 自动登录桥。
- `native-awakening-source/`：C++ 角色觉醒及其完整第三方依赖。
- `weapon-mods/`：12 个武器命名入口、共享核心全部 C++ 源码、配置、MinHook 及离线测试。
- `assets/`、`tests/`、`build.ps1`、`global.json`：界面资源、定向测试和构建入口。

从完整工程根目录运行 `pwsh -NoProfile -File .\build.ps1`，使用随包提供的原生组件重建并发布登录器；加上 `-RebuildNative` 可同时从 C++ 源码重编译上述原生组件。需要 .NET 8 SDK；重编译原生组件还需 Visual Studio 2022 C++ x86/x64 Build Tools 和 Windows SDK。

重新编译原生组件会改变 DLL 内容，包括链接时间戳。若同一 Windows 用户已运行旧版本，自动登录桥的固定版本缓存可能触发“原生补丁缓存与此登录器不匹配”。这是拒绝覆盖旧缓存的保护，不是缺少源码。自行改动原生组件时，应为修改版本更新组件缓存版本，或在独立测试用户/干净环境中验证；不要在游戏运行时删除或覆盖旧缓存。本次源码交付未修改现用程序、旧缓存或运行逻辑。

`dist/CSNZ_Launcher_1.1.1-local1_Source/` 与同名 ZIP 是可单独复制/解压的完整本地源码交付，目录中包含真正的 `src/CSNZ.Launcher/`，不是只有运行 EXE。

这份本地交付为重建当前 EXE 保留了已准备的游戏资源覆盖包，不代表允许公开再分发游戏资源。它不包含账号数据库、保存凭据、正式服务端配置或运行日志。GitHub 上传不包含这份本地 ZIP/目录，含游戏资源的公开运行包发布仍暂停。
