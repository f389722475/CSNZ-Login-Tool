# CSNZ GigaBreakLE — native C/C++ 0.7.4-r2

当前 0.7.4 修复逻辑的 **Windows x86 原生移植**。目标是保持已接受的功能边界，
去掉 JS / Frida / Python 运行时，不借这次移植新增武器行为。

## 这是什么格式

`src/giga_break_le.cpp` 是武器逻辑；`src/game_abi.h` 是实测的 CSNZ0930 GoldSrc
ABI 与扩展结构偏移；`src/native_runtime.cpp` 是 DLL 和原生入口挂钩。
代码直接调用当前游戏的 FireBullets3、TakeDamage、TraceLine、PlaybackEvent、
临时模型和 Studio 骨骼接口，使用 x86 `__thiscall` / `__cdecl` / `__fastcall`。

这不是原版 HLSDK 的替换 `mp.dll`，也没有伪造 GetEntityAPI 导出。
CSNZ 的武器类、玩家对象和 TEMPENTITY 已扩展，不能套用 CS 1.6 SDK 类布局。
在未获得完整 CSNZ 工程和类定义的情况下，交付方式是配套原生扩展 DLL，
而不是声称已重建整个客户端/服务端。源文件是可编译的 C/C++，不是 JS 包装器。

## 构建

Windows，Visual Studio 2022 Build Tools，安装“使用 C++ 的桌面开发”和 Windows SDK。
直接运行 `build.cmd`，自动寻找已安装的 x86 编译环境；也可在 x86 Native Tools
Command Prompt 运行它。不要从 x64 编译环境启动。

产物：`build/bin/GigaBreakLE.dll`、`build/bin/CSNZ_GigaBreakLE.exe`。
使用 C++17、`/MT`、`/EHa`、SSE2；所需 C 依赖 MinHook 1.3.4 已随源码附带。
不需要 CMake、Python、Node.js、Frida 或任何游戏二进制来编译。

`build/bin/CSNZ_GigaBreakLE.exe --self-check` 仅加载本地 DLL 并检查导出，
不启动游戏、不安装挂钩。没有单独的大型测试工程。

打包：构建后运行 `powershell -NoProfile -ExecutionPolicy Bypass -File package.ps1`。
默认输出至本源码目录的 `dist`；可用 `-DistRoot` 指定另一绝对目录。
生成两个独立 ZIP：Source 和 Deploy，已有同名 ZIP 时拒绝覆盖。

## 实现位置

| 文件 | 内容 |
| --- | --- |
| `src/giga_break_le.cpp` | 射击、范围伤害、状态、充能、防御、僚机和局部模型平衡 |
| `src/game_abi.h` | x86 调用签名、向量、TraceResult 和安全内存访问声明 |
| `src/build_profile.h` | 当前构建的指令入口与重定位信息；不是文件哈希 |
| `src/native_runtime.cpp` | 9 个原生挂钩、版本门禁、线程边界、停用清理和 DLL 导出 |
| `src/launcher.cpp` | 原生启动器，只加载到自己新启动且版本匹配的游戏进程 |
| `package/` | 可移植部署脚本与用户说明，不包含个人配置 |
| `third_party/minhook/` | 上游 v1.3.4 的 x86 所需文件及完整许可 |

## 关键边界

- 仅匹配 ID 726 和已知 LE vtable；保留游戏原有函数链，不直接写目标 HP。
- 版本按 PE machine / timestamp / SizeOfImage；入口按 16 字节指令并正确重定位。
  不生成 SHA256，不把磁盘文件校验当作版本接口。
- 所有游戏逻辑在引擎回调线程执行。启动工作线程只做初始化、等待和控制。
  状态由可重入锁保护，原生伤害调用可在同一线程进入防御挂钩。
- 最终伤害挂钩是函数中段，单独的 x86 naked stub 保存 GPR、EFLAGS、x87/SSE，
  恢复后经 MinHook trampoline 回到原指令；不是错误地把函数中段当普通函数返回。
- DLL 入口不做挂钩或启动线程。加载器完成 LoadLibraryW 后才调用 GigaBreakLE_Start。
  系统加载函数通过其实际所属模块和 RVA 解析远端地址，考虑 KernelBase 转发和 ASLR。
- 停用后在服务端和客户端帧各自清理；不在运行中的回调栈下强行卸载 DLL。
  DLL 及游戏模块被保留至进程退出。地图/模式完整生命周期仍属未验收范围。
- 日志仅初始化、失败、停用与汇总，不移植旧版逐发遥测钩子。
- 护盾恢复量、快手次数与多人同步仍保持已有“部分完成”状态，不新增推测实现。

## 验证层级

本次仅做 MSVC x86 编译、导出/依赖检查及独立 DLL 加载检查，另以安装预检查
核对目标游戏构建；未启动游戏验收原生版。旧 JS 版曾有的实机结果不自动适用于
本移植。部署说明见 `package/README_zh-CN.txt`。

## 许可和发布

本包不包含游戏 DLL/EXE、反编译游戏源码、模型、贴图、账号、数据库、日志或
作者机器的游戏路径。模组本体的发布许可由发布者决定，未擅自设置开源许可证。
MinHook 及内含组件的许可见 `third_party/minhook/LICENSE.txt`。
