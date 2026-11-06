母法：https://github.com/EricZhang233/EricRepoRule —— 需要完全读取并遵守此仓库所有规范，本文件其余部分为本仓库私有规范。

---

## 本仓库私有规范

### 1. 壳层与 Core 落位（补充母法第 7 条）
本仓库为 C++20 / CMake 工程，壳层与 Core 的对应关系固定如下：
- 宿主壳层 `vmex.exe`：根目录 `Program.cpp`，无控制台常驻，只做进程生命周期编排。
- 管理器壳层 `vmex_manager.exe`：`Manager/`，使用 C#、WPF 与 .NET Framework 4.8，只做界面与异步调用；主进程与自启入口不依赖管理器或 .NET。
- UI 层 `vmex_tap`：注入载荷（`Components/inject.tap`），只做 XAML 页面渲染。
- 注入入口 `vmex_launcher`：注入载荷（`Components/inject.launcher`），只做把 TAP 与诊断模块装进目标进程。
- 业务逻辑单一存在于 `Core/`，被各壳层共用；禁止壳层与 Core 之间平行实现，禁止壳层先行实现。

### 2. 文本资源（补充母法第 8 条）
本仓库为 C++ 工程，面向用户的字符串集中存于根目录 `text.yaml`，构建期嵌入，运行期经 `Core/TextService` 取用，禁止在代码中硬编码。

日志使用调用点固定英文字符串，不走 i18n，不在 `text.yaml` 保存日志模板。日志系统对齐 EricGameLauncher：有界异步写入、关键级别兜底、分卷与留存清理、脱敏、调用位置、操作关联与耗时、退出刷盘；独立更新器遵循同一规则。

### 3. 无注释原则的例外（补充母法第 1 条）
`docs/poc/` 为 PoC 证据归档，内容与注释保持原样，不适用无注释原则。

### 4. 版本唯一来源（补充母法第 10 条）
版本号唯一来源为根目录 `Version.h` 的 `VMEX_VERSION_STRING` 与 `VMEX_PRODUCT_NAME`。

### 5. 按需引入的母法规范
- `github-workflows.md`：采用 `post_release.yml` 与 `promote_prerelease.yml`。
- 发布实现按用户要求对齐 EGL：标签检出后构建并验证 Release 包，上传成功后发布说明；失败退回草稿。远端不运行测试套件，测试不作为远端构建门槛。无需将本地 output 压缩包提交进仓库。
- `devrules.md`：用户配置存于 `HKCU\Software\EricSoft\VolumeMixerExtender`，缓存与日志根为 `%TEMP%\eric\VolumeMixerExtender`。

### 6. 无 CLI 架构（替代母法第 6 条）

本项目不提供 CLI 壳层、公开命令解析或内置命令行文档。管理能力由 WPF 管理器直接调用 Core；面板经本机管道调用 Core。录制保护使用后台宿主的内部入口，静默更新器直接调用 Core 完成停机和注入清理。

### 7. 独立本地测试流程

- test 唯一对外入口为仓库根目录的 `dotnet test`，一次完整执行全部自动测试，不要求参数、分区或其他入口。普通 build/run 不自动执行测试，测试通过不自动提交、推送或发布。
- CMake 默认 BUILD_TESTING=OFF，dotnet test 内部自动准备 Debug 测试产物，执行全部 CTest 和发布契约检查。根目录测试 csproj 仅用于编排，不改变原生程序的 CMake 构建方式。
- 发行构建保留依赖签名、哈希、架构及发布包完整性校验，这些校验不运行 Core、管理器等功能测试套件。
