# 原生武器完整源码交付

`src/` 是本次补齐的独立、可编译源码副本，不是空目录或反编译产物。它保留已有工程布局，所有相对包含和构建脚本都可独立使用：

- `shared/src/`：共享运行时、挂钩、伤害/状态/特效仲裁及全部武器家族实现，包括 `giga/giga_gameplay.cpp` 和 `giga_core_v1.inc`。
- `shared/include/`：公共头文件；`shared/plugin.cpp` 和 `.def`：命名入口及导出定义。
- 12 个武器命名目录：各自的 `module.cpp`、`weapon.json`。Arbalest 537 仅作外部参照，不在本交付内；SpaceArbalest 591 保留。
- `shared/third_party/minhook/`：完整依赖源码、许可证及作者声明。
- `profiles/`、`tests/`、`tools/`、`catalog.json`、`asset-manifest.json` 和三个构建脚本：冻结配置、生成工具及离线检查。

## 从 src 编译

需要 Visual Studio 2022 C++ Build Tools、x86 工具链及 Windows SDK。在 `src` 目录运行：

```bat
build-native.cmd
build-tests.cmd
```

第一条生成 12 个命名武器 DLL、`shared/CSNZWeaponCore.dll` 和新的 `bundle-manifest.json`。第二条运行离线 ABI/配置/DLL 合同检查。正常编译不要求游戏安装目录、Python、Node.js 或 Frida。

此源码交付不带预编译 DLL、`.nar` 游戏资源、账号数据库、日志或旧构建目录。DLL 检查不等于实机战斗或多人联机验收，也不会自动部署生成的文件。

## 原件与更新边界

本次仅增加源码交付，根目录原有 `shared/`、各武器源码、构建脚本及已用 DLL 均保留。`src/` 是当前版本快照，不会自动与作者工作目录互相覆盖；在其中修改后，生成的 DLL 也只位于该副本内。

再次导出应指定新的目录，脚本拒绝覆盖已有源码或 ZIP：

```powershell
pwsh -NoProfile -File .\tools\export_source.ps1 -OutputDirectory "D:\目标目录\新版源码"
```

第一方修复代码未擅自添加开源许可；第三方许可随包保留。
