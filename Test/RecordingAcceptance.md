# 录制模式功能收尾验收

## 自动化流程

在仓库根目录用 PowerShell 7 执行，构建与测试按顺序完成，避免同时构建同一输出目录。

```powershell
cmake --preset vs18-x64 -DBUILD_TESTING=ON -DVMEX_BUILD_RECORDING_DRIVER=ON
cmake --build obj/vs18-x64 --config Debug --target vmex_tests vmex_manager_ui_tests vmex
ctest --test-dir obj/vs18-x64 -C Debug --output-on-failure --output-junit functional-check.xml
```

CI 使用 windows-2025-vs2026 运行器与 vs18-x64 预设，执行同一 CTest 测试集。新增 core.guard 已纳入默认测试集。

| 测试 | 覆盖内容 | 不能代替的验收 |
| --- | --- | --- |
| core.recording | 各启动阶段失败、恢复失败、禁用失败、重试、重复启停、监听故障；严格端点角色识别与状态判定 | Windows 实际端点启停、默认设备策略、真实声音 |
| core.guard | 恢复默认→清路由→禁用的顺序与有限重试；实际测试父进程被结束后，保护进程退出并如实报告不存在的测试端点 | 真正 VB-CABLE 在崩溃后的恢复成功率 |
| core.recording 的隔离注册表夹具 | 清除 V 路由及配对元数据，保留其他路由与应用标识 | Windows 音频服务对已退出应用偏好的实际缓存行为 |
| core.audio / core.uninstall | 音量重置与卸载失败处理 | 实体设备音量、默认麦克风行为 |
| core.commands / core.resources / core.config / core.pipe | 参数、资源、配置、管道回复与退出 | 注入系统面板后的实际交互 |
| manager.wpf | 管理器状态呈现、窗口与关于布局、ABI、滚动行为 | 实机 DPI、系统标题栏及真实安装操作 |
| diagnostics.package | 微软 XAML 诊断组件签名与 x64 架构，构建时固定来源与 SHA-256 | 注入系统面板后的实际交互 |
| driver.package / driver.trust | 精简后的 9 文件清单、微软 CAT 与 SYS/INF 成员、安装器/控制面板哈希、缺失与篡改拒绝 | 实体机内核加载、安装器取消和重启后的状态 |

可选的真实应用策略存储检查：

```powershell
./bin/Debug/vmex_tests.exe routing-live
```

该检查只操作测试进程自身的路由并恢复。RDP 的远程会话端点不支持此持久重定向时明确跳过；跳过不能记为实际路由验证通过。不要将 audio-live、routing-live 等环境相关测试作为无硬件 CI 已验收声音的证据。

Release 构建检查使用现有发布脚本，Tag 参数须与 Version.h 一致；PackageDirectory 必须为空或不存在：

```powershell
$versionHeader = Get-Content ./Version.h -Raw
if ($versionHeader -notmatch 'VMEX_VERSION_STRING\s+"([0-9.]+)"') { throw 'Missing version' }
./.github/scripts/build-release.ps1 -Tag "v$($Matches[1])" -Preset vs18-x64 -PackageDirectory ./output/functional-check
```

脚本检查包成员、版本、与本次构建的逐文件哈希，以及官方驱动目录签名。它不安装驱动，不发布 Release。升级版本后同步替换命令中的 Tag。

## 实体机前置条件

Windows 11 26H2 x64；正常安全设置；至少两台实体输出 R1/R2、一个实体麦克风 M。使用标准 VB-CABLE Pack45，官方配置程序的 Enable Loopback Streaming 保持开启。程序的就绪检测验证驱动与端点配置，不验证实际音频内容或供应商私有选项。

准备播放器 A、B、C 和录制软件。确认 R1 为 Console/Multimedia 默认输出，M 为默认输入；记下原来的应用音量、静音和重定向状态。用 Windows 声音控制面板显示已禁用设备，观察全部三个虚拟端点。程序主列表本身会过滤虚拟端点，不能用它证明设备已被禁用。

实体机 SSH 严格只读；下表安装、卸载、配置修改和交互步骤由用户在实体机执行。

## 实体机步骤与通过标准

| 步骤 | 操作 | 预期 |
| --- | --- | --- |
| 1. 未安装 | 未安装驱动时启动程序并打开音量面板；执行 recording status | 录制行不显示；CLI 提供不可用原因；普通音量功能正常 |
| 2. 安装与待机 | 在管理器打开官方安装器，安装、重启，再启动宿主 | 三个端点均禁用；录制开关仍显示为关；实体默认输出和麦克风正常 |
| 3. 开启 | 点击录制开关或执行 recording on | 仅 CABLE Input 启用，另两个保持禁用；默认输出切到 V；R1 能监听 |
| 4. 录制与音量 | 录制默认输出的桌面音频/loopback；调整 A 的音量、静音和 R1 总音量 | A 的控制影响录制和监听；R1 总音量只影响监听；实体麦克风仍可用 |
| 5. 重定向 | A 指向 V；B 指向 R2；C 指向 V 后退出 C | V 只在录制开启时可选；B 的独立路由正常 |
| 6. 切监听与关闭 | 将监听切到 R2，再关闭录制 | 默认恢复最初记住的 R1；A/C 跟随默认，B 仍指向 R2；音量和静音保留；三个虚拟端点均禁用 |
| 7. 重复与持久化 | 连续启停至少 10 次；正常退出并重新启动 | 无重复监听、卡死、残留保护进程；每次从关闭状态开始；待机禁用不隐藏开关 |
| 8. 配置错误 | 在 Windows 禁用整个驱动设备，重开面板；随后修复 | 配置异常时隐藏录制行，修复后自动复查；仅禁用 CABLE Input 不应隐藏开关 |
| 9. 改名 | 把主端点显示名改成包含 CABLE In 16 Ch 的名字，再测试待机和录制 | 原始角色仍正确，录制时只启用主端点，两个其他端点不被误选 |
| 10. 安装器取消 | 录制期间打开卸载器并取消 UAC或取消卸载；再次尝试卸载 | 先退出录制再获取端点操作锁；无锁冲突；取消后恢复操作前的端点状态与恢复记录 |
| 11. 默认选择 | 正常关闭后重开录制；另测首次安装与偏好变化 | 启用 V 后若默认未变则不重写；若默认被自动选为 V，则在监听准备阶段恢复原输出，监听就绪后才主动选择 V |
| 12. 故障 | 录制时拔掉监听设备、结束宿主进程，再测试音频服务重启 | 故障有反馈；恢复默认与路由后禁用 V；恢复失败不能伪报成功；重新开始前处理待清理状态 |
| 13. 恢复记录 | 用户先禁用一个额外端点；执行 driver show；再测试程序卸载清理 | show 只恢复本程序有记录的操作，不启用用户原本禁用的端点；删除配置前恢复本程序的禁用操作 |
| 14. 长时间 | 连续监听并录制至少 30 分钟，覆盖播放器切歌、静音、暂停恢复 | 无持续重音、明显漂移或缓冲持续增长；检查 guard 与 recording 日志中的错误及丢帧 |

每轮结束后核对 Windows 默认输出、默认麦克风、三个虚拟端点状态、A/B/C 的下一次启动路由，并保留本轮宿主、CLI 和 recording-guard 日志。仅有函数返回成功或测试夹具通过，不构成真实出声验收。

## 早期功能检查记录（2026-10-29）

当时工作区已完成 Debug 构建与 11 项 CTest 检查，全部通过；报告位于 obj/vs18-x64/functional-check.xml。

当时源码的 Release 构建、压缩包成员及版本/哈希检查、官方驱动签名校验通过。开发期验收包位于 output/functional-check-final/VolumeMixerExtender_1.0.2.zip；SHA256 为 338B6270D3EBD941C22146CD096CE043062BE251A9547A7E29F48BD1266B5B16。此包不代表后续提交或当前版本的构建结果。

修复安装器先占端点锁再请求宿主关闭录制造成的跨进程冲突；保护进程改为对整个恢复序列执行有限重试，保留有序清理和失败结果。

命令行冒烟检查还覆盖 help、version、driver status、recording status，以及缺少监听设备、无效动作、多余设备参数的拒绝；参数错误应返回退出码 1，并且不能启动宿主或录制。已修复 recording 将参数错误映射为退出码 3 的问题。

当前为 RDP 环境，未安装 VB-CABLE，真实三端点状态切换、出声、录制、多设备路由、实体机安装取消与长期稳定性未验收。上述实体机步骤仍是发布前待完成项。

本轮 routing-live 明确跳过：远程会话输出不能用于应用持久重定向，不计入实机路由验证。
