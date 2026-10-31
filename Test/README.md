# 项目测试

在仓库根目录执行：

```powershell
dotnet test
```

这是唯一对外测试入口，不需要参数、分区或其他命令。根目录测试工程自动准备 CMake 测试配置、编译 Debug 测试产物，然后完整执行所有 CTest 和发布工作流契约。

需要 Windows、Visual Studio 2026 C++ 工具链、Windows SDK、CMake、PowerShell 7、Node.js 和 .NET 10 SDK。测试会打开管理器冒烟检查窗口，需要可交互桌面。

测试覆盖 Core、外部宿主、诊断组件、驱动包和 WPF 管理器。任一阶段失败或超时，dotnet test 返回非零退出码；成功不会自动提交、推送或发布。普通 build/run 和远端发布不运行测试套件。

## 结果

报告位于 `Test/artifacts/<时间与编号>/`：

- `summary.md` 与 `summary.json`：阶段状态及日志入口。
- `00.log` 等：命令与完整输出。
- `test-results.xml`：全部 CTest 的 JUnit 结果。

测试构建缓存位于 `obj/local-tests`，不改变普通构建目录的测试开关，也不打包 Release。内部 PowerShell、CTest 和 Node.js 脚本由 dotnet test 自动调用，无需分别执行。

真实音频设备、不同缩放、应用重启后的系统行为和实际 GitHub 发布仍需后续实机验收。远端保留依赖签名、哈希、架构和包完整性校验，测试不作为远端构建门槛。
