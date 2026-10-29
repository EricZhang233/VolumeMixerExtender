# CI 构建复查（2026-10-29）

基于当前 1.0.1 工作区，使用与 CI 对齐的 vs18-x64 预设、Visual Studio 2026、Windows SDK 10.0.26100.0 和 .NET SDK 10.0.403，分别执行 Debug、Release 构建与完整自动化测试，再构建和验证 Release 包。本轮是本地验证，未启动 GitHub 托管运行器；本轮构建复用了本地依赖缓存。

## 修复

- Windows 运行器与 CMake 生成器统一为 Visual Studio 2026；global.json 约束 .NET 10.0 正式 SDK。
- XAML 诊断组件改从微软官方 Microsoft.Windows.SDK.CPP 10.0.26100.3916 NuGet 包获取，固定包和 DLL 的 SHA-256，检查微软签名与 x64 架构，不依赖运行器额外安装的诊断工具。
- 每次构建同步诊断 DLL，避免增量构建未重新链接主程序时遗漏更新依赖。
- 配置、CTest 和 Release 包检查均校验诊断组件；验证任务保留 CMake 配置日志。
- CI 验证同时覆盖 Debug、Release，支持分支推送、PR、手动选择分支或提交，以及发布标签调用；两种配置分别上传报告。
- 补充 V 音量与默认输出锁定、面板连续选择和 R 排序、异目录宿主定位 Core 的回归测试；管理器测试同时检查底部有提示时的滚动行为。
- 录制进入时使用原默认输出作为监听 R，退出时使用当前 R 作为默认输出；覆盖连续切换、再次开启和启动失败回滚，崩溃保护目标随成功的监听切换更新。
- 录制切换完成后缓冲 800ms，设备通知静默窗口为 400ms；两次快照至少相隔 250ms 且一致后才提交，读取失败或瞬态变化保留旧列表。实体设备行不变时复用原控件、计时器和音量订阅，只更新标题与应用区，避免整体禁用造成的变暗及列表入场动画。

## 结果

| 检查 | 结果 |
| --- | --- |
| CMake 配置与官方依赖完整性校验 | 通过 |
| Debug、Release 构建，包括管理器及面板 | 均通过 |
| Debug CTest | 15/15 通过，报告 obj/vs18-x64/buffered-Debug.xml |
| Release CTest | 15/15 通过，报告 obj/vs18-x64/buffered-Release.xml |
| Release 构建及打包 | 通过 |
| 包成员、产品版本、与构建产物逐文件哈希 | 通过 |
| 诊断组件微软签名、x64 架构 | 通过 |
| VB-CABLE 文件、微软 CAT 与 SYS/INF 成员 | 通过 |
| 全部 GitHub Actions 工作流 | actionlint 1.7.12 通过 |
| PowerShell 脚本语法 | 通过 |
| Release CLI 帮助与内置说明 | 通过，已包含退出时使用当前监听 R 的规则 |

验收包：output/recording-buffer-ci-verified/VolumeMixerExtender_1.0.1.zip。

SHA256：F81FD1CB7FA95C4A71B74A9C420FB2ED2D9D56E700D0F59800FEA2B28B743FE3。

本轮未提交、推送、修改标签、发布 Release 或向桌面复制软件包。云端任务须在包含修复的提交上运行；旧提交的任务不能获得这些修复。GitHub 上传、发布说明更新和转正流程已检查工作流配置，但未实际发布验证。实体机声音、系统面板交互和真实驱动启停未在本轮执行，仍按 RecordingAcceptance.md 验收；自动化测试使用隔离夹具，不能替代这些实机检查。
