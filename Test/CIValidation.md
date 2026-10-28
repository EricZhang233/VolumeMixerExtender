# CI 构建复查（2026-10-29）

基于当前 1.0.1 工作区，在全新源码目录、无项目构建产物及依赖缓存的条件下执行配置、Debug 构建、全部 CTest 和 Release 打包；未复用已安装 SDK 中的 XAML 诊断 DLL。

## 修复

- Windows 运行器与 CMake 生成器统一为 Visual Studio 2026；global.json 约束 .NET 10.0 正式 SDK。
- XAML 诊断组件改从微软官方 Microsoft.Windows.SDK.CPP 10.0.26100.3916 NuGet 包获取，固定包和 DLL 的 SHA-256，检查微软签名与 x64 架构，不依赖运行器额外安装的诊断工具。
- 每次构建同步诊断 DLL，避免增量构建未重新链接主程序时遗漏更新依赖。
- 配置、CTest 和 Release 包检查均校验诊断组件；验证任务保留 CMake 配置日志。

## 结果

| 检查 | 结果 |
| --- | --- |
| 无缓存依赖获取与 CMake 配置 | 通过 |
| 全新 Debug 构建，包括管理器及面板 | 通过 |
| CTest | 12/12 通过，报告 obj/ci-check/test-results.xml |
| Release 构建及打包 | 通过 |
| 包成员、产品版本、与构建产物逐文件哈希 | 通过 |
| 诊断组件微软签名、x64 架构 | 通过；错误的 x86 组件被拒绝 |
| 诊断 DLL 缓存损坏 | 检出并从已校验官方包恢复 |
| VB-CABLE 文件、微软 CAT 与 SYS/INF 成员 | 通过 |
| 全部 GitHub Actions 工作流 | actionlint 1.7.12 通过 |
| PowerShell 脚本语法 | 通过 |

验收包：output/ci-verified-1.0.1/VolumeMixerExtender_1.0.1.zip。

SHA256：C5FD04420013ACBCE1007D7F2D3F9E6CF493BC4F97F839E729F5A3AC7AF670A8。

本轮验证在本地隔离目录执行，未提交、推送、修改标签或发布 Release。云端任务须在包含修复的提交上运行；旧提交的任务不能获得这些修复。GitHub 上传、发布说明更新和转正流程已检查工作流配置，但未实际发布验证。实体机声音与驱动交互验收仍按 RecordingAcceptance.md 执行。
