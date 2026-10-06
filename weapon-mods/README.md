# CSNZ 原生武器模块 — 本地编译版

当前版本：`1.0.0-native-local1`，Windows x86 / C++17。

从 2026-10-04 已人工验收的统一服务端候选迁移，不含 JS 解释器、Frida 或 Python 运行时。**原候选的验收不自动等于本次 C++ 移植实机通过。** 当前完成编译、DLL 加载/导出、x86 ABI 和定向离线策略验证；新原生版游戏内战斗、房间切换及多人显示仍待人工复验。

## 目录与 DLL

每把武器有独立命名目录，包含 `module.cpp`、`weapon.json` 和同名 DLL：

| 目录 | ID |
| --- | --- |
| GigaBreak / GigaBreakLE | 725 / 726 |
| Lycaon | 613 |
| Brionac / LuminousBrionac / LuminousBrionacLE | 4088 / 4128 / 4129 |
| SpaceArbalest | 591 |
| GravityRepulsor / AbyssRepulsor | 547 / 609 |
| Naberius / ArcaneNaberius | 568 / 692 |
| FrostViper | 597 |

未修改的参照物 `Arbalest`（537）已移出运行包，保存在 weapons restoration 的 `reference-weapons/Arbalest`。当前是 12 个修复模块。Giga 两项采用这次修复后的服务端实现，不是登录器旧版客户端 `0.7.4`。

命名 DLL 是武器选择入口，真正的家族逻辑位于 `shared/src`；`shared/CSNZWeaponCore.dll` 统一持有函数入口、伤害/状态仲裁和网络特效，避免同家族或跨家族重复安装挂钩。使用时必须成套保留共享核心，不能只复制一个小 DLL。

## 构建与检查

本目录就是完整、独立可编译的武器工程，包括共享核心、12 个武器入口、头文件、MinHook、配置、工具和测试，无需再寻找下一层 `src`。详见 `README_SOURCE.md` 的导出说明。`tools/export_source.ps1` 可向新的目录导出不含 DLL/游戏资源的源码快照，拒绝覆盖已有内容；该快照与本目录不会自动同步。

需要 Visual Studio 2022 C++ Build Tools（x86 和 Windows SDK）。

```bat
build-native.cmd
build-tests.cmd
```

正常编译不需要游戏文件、Python、Node.js 或 Frida。`tools/compile_profile.py` 仅用于开发时把不可执行的配置数据生成 C++ 字面量，生成结果已随源码提供。`profiles/accepted-0930.json` 是冻结的构建/属性/资源合同。

检查覆盖：x86 cdecl/stdcall/thiscall、共享回调顺序、递归、线程、x87/SSE/64 位返回值；记录的 Frost 轨迹样本和 HUD 字节；Divine 充能、连段、Bezier 与尾迹分段；全部 13 个 DLL（12 个武器入口加共享核心） 的真实加载/导出及错误宿主拒绝。它们不代替实际游戏验收。

## 加载边界

- 只支持已核对的 CSNZ0930 `CSOHLDS.exe`、`mp.dll`、`hw.dll`，同时检查文件身份、运行时代码重定位和虚表。
- 只在新启动、尚未进入地图的本地专用服务端注册模块；运行中的地图拒绝直接挂接。
- 游戏客户端不加载这些武器 DLL，不修改原游戏 EXE/DLL；登录器的自动登录桥和角色觉醒仍是独立功能。
- 停用在引擎线程清理；核心驻留到进程退出，不热卸载。异常会停用模块并保留日志，不继续假报就绪。
- 状态码：0 已加载，1 已登记，2 已挂接等待地图，3 地图资源就绪，4 停用中，5 清理完成且驻留，100 失败。3 不代表战斗验收通过。
- 日志位于核心旁的 `logs/native-weapons-PID.log`，不记录账号或密码。

## 本地资源与发布边界

`local-assets/fixtrike.nar` 是由用户已安装游戏资源准备的 174 项覆盖包，供本地完整测试使用；已有基础条目在此前资源合并中保留。它不是获准公开再分发的资源包，已被登录器仓库的 `.gitignore` 排除，GitHub 不带该文件。原生武器源码和自编译 DLL 可以独立构建/校验，但完整登录器仍要求匹配的本地覆盖资源；自动从使用者安装准备资源的公开流程尚未提供，因此含这些资源的运行包不发布。

登录器首次准备会先询问，再备份配置和已知基础资源包。未知自定义包拒绝覆盖；不读写账户数据库或背包。资源备份位于所选游戏目录的 `CSNZLauncherBackups`。

MinHook 许可证见 `shared/third_party/minhook/LICENSE.txt`。第一方修复源码未擅自增加新的开源许可。
