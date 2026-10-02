

# 需求

利用已经进过研究和实现的Xaml注入，创建一个新的声音输出子页面，通过已实现的Footer切换我们自己的页面和系统的默认页面。


复刻EarTrump的针对不同应用单独调整音量和输出端点的功能，同时修复部分情况下可能丢失所有应用列表导致每个音频端点下方为空白 -- 此功能置于自定义页面Body第二顺位


支持调整系统内默认的音频输入\输出设备(两个下拉框) -- 此功能置于自定义页面Body第一顺位


支持一键把重定向的应用清除重定向(回到默认值) -- 此功能置于Footer的左侧


进入自定义页面之后，把Footer的进入指令换成退出指令，可回到系统默认页面。

---

# Agent 备忘

## 已完成

- [x] CMake + CMakePresets（VS18 / VS2022-BuildTools 两条生成器），产物落 `bin/<config>`，中间落 `obj/`
- [x] 目录按 EricGameLauncher 编排：根 = 主程序壳（`Program.cpp`/`CliService.*`）+ `Core/`（业务）+ `Components/`（嵌入载荷）
- [x] `git status` 确认 `Components/` 只放载荷：`inject.launcher` / `inject.tap`
- [x] 注入载荷构建期嵌入 `vmex.exe` 的 RCDATA 资源，`vmex payload` / `vmex payload --extract=` 已验通（launcher 52224 / tap 46080 字节）
- [x] 版本号单一来源 `Version.h` → 驱动 `project(VERSION)`、三个二进制的 VERSIONINFO、CLI 版本输出
- [x] `text.yaml` 构建期嵌入二进制；`-help` 与 `skill` 由命令注册表自动生成，无第二来源
- [x] build >= 26300 门禁（`Platform::VerifySupportedWindowsVersion`），不满足直接拒绝
- [x] 打包仅本地：Release 构建后自动产出 `output/VolumeMixerExtender_<version>.zip`；CI 只保留 `post_release`（上传产物 + 发完自动清理 output）与 `promote_prerelease`（48 小时后提升），不做 CI 编译
- [x] 修复：CLI 输出在重定向时全丢（`WriteConsoleW` 对非控制台句柄失败），改为控制台走宽字符、管道走 UTF-8

## 待办

- [ ] `Core/AudioDeviceManager` 接 WASAPI（当前为桩，`devices` 等命令返回"尚未实现"）
- [ ] `Core/EndpointPolicyService` 接 `IPolicyConfig` + `IAudioPolicyConfigFactory`，只留现代分支
- [ ] `Core/InjectionService` 接目标定位 + 载荷释放 + launcher 装载
- [ ] `Components/inject.launcher`：接通 `InitializeXamlDiagnosticsEx` 调用（现仅解析到导出地址）
- [ ] `Components/inject.tap`：实现 `DllGetClassObject` 的类工厂（现返回 `CLASS_E_CLASSNOTAVAILABLE`）
- [ ] 载荷 DLL 自身的日志文本改走 text.yaml（当前为便于 ShellHost 内置诊断暂用字面量）
- [ ] 把 PoC 已验通的逻辑搬进 Core：Footer 定位、样式抄写、2 列 Grid 换行、幂等判定（基于"结果存在性"而非指针）、`action=pipe` 动作链（`ERROR_NO_DATA`(232) 视为正常）
- [ ] `.releasenote.md` 已按 `.releasenoteguide.md` 建立，后续发版前更新
- [ ] 等 `install` 类能力落地后，补 `.install.cmd`（隔壁的本地安装入口）