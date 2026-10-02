CSNZ Giga Break LE 原生版 0.7.4-native-r2-partial

这是当前 0.7.4 武器修复逻辑的原生 C/C++ 移植，部署包只需 Windows
系统组件；不需要 Python、Frida、Node.js 或额外 VC++ 运行库。
原生 DLL 和启动器都是 32 位，支持在 64 位 Windows 10/11 上运行。

使用
1. 完整解压此部署包。不要只复制其中的 DLL。
2. 退出游戏及旧版 CSNZ_LEGuard，运行 Install.cmd，输入游戏根目录。
   pathfix1：也接受 Bin 文件夹或 Bin/CSOLauncher.exe，自动识别上一级，避免 Bin/Bin。
   默认本地服务器为 127.0.0.1:30002。可带参数安装：
   Install.cmd -GameRoot "你的游戏根目录" -ServerAddress 127.0.0.1 -Port 30002
3. 按原来的方式启动本地服务器，然后运行本包内的 Start_Mod.cmd。
   使用游戏正常的登录界面。本包不包含或导入账号、密码、自动登录设置。
4. 启动器显示 READY 表示原生挂钩已安装；payload/native-runtime.log 记录状态。
   READY 不是本版所有游戏内功能已经验收的声明。
5. 停用用 Stop_Mod.cmd；清理在下一次游戏/客户端帧执行。如果暂停，请恢复
   一次或直接退出游戏。DLL 和无作用的转发挂钩保留到进程退出，避免运行中卸载。
   启用另一版本前必须完全退出游戏。不要同时使用 JS 版与原生版。

支持版本
仅 CSNZ0930：CSOLauncher.exe 构建时间 2026-09-30（中国时间），mp.dll 和
client.dll 构建时间 2026-07-14（UTC）。按 PE 架构、时间戳、映像大小匹配，
同时检查已知调用点指令和引擎 API 初始化状态。无文件哈希清单，无 SHA256。
不匹配、已有其他补丁覆盖调用点或模块路径不对时拒绝启用，不猜测新地址。

内容与边界
- 普通射击、原生穿透伤害、辅助瞄准、连射爆炸、蓄力攻击、近战/牵引、冲撞、
  僚机攻击和头顶原模型位置平衡，按现有逻辑移植。
- 防御窗口与致命伤害护盾按现有逻辑移植；持有但未装备时继续充能。
- 保留原生伤害与事件路径；不直接改写目标生命值，不改武器原始模型或贴图。
- 护盾回复量仍未解决；不编造治疗量。不强制“快手”4/11 次，不覆盖原攻击计时。
- 只面向当前本地 listen-server 和本地客户端。多人同步、远端专用服和跨版本
  生命周期未验收，不宣称通用 HLSDK / Metamod 插件兼容性。
- 本原生移植完成编译与独立 DLL 加载检查；没有把旧 JS 版实机结果当作本版
  实机结果。实际对局效果仍需用户验证。

安装和卸载
Install.cmd 只在本包的 payload/native.ini 保存你的本机路径、地址和端口，
重复配置会另存旧 INI。不覆盖任何原始游戏 DLL、EXE、资源、数据库或存档。
先验证路径、PE 版本和 DLL 加载，通过后才写入配置；失败不破坏已有配置。
Start_Mod 与 Stop_Mod 同样兼容旧 INI 中误填的 Bin 路径，缺少配置时明确提示先安装。
Stop_Mod 仅向已运行的原生会话发出停止事件，不再要求磁盘上的游戏 DLL 版本仍一致。
本包可放在游戏外。卸载时退出游戏，删除自己解压的这个包即可，无游戏文件需还原。
不要把运行后生成的 native.ini、配置备份或 native-runtime.log 再公开打包。

文件
payload/GigaBreakLE.dll        原生 C++ 武器扩展，内含 MinHook C 实现
payload/CSNZ_GigaBreakLE.exe    原生 x86 启动、加载和停用程序
MinHook-LICENSE.txt          第三方许可
NOTICE.txt                  发布范围说明

源代码在独立 Source ZIP；本部署 ZIP 不含游戏原件、测试工程或开发环境。
