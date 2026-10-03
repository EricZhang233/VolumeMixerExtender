# 需求

利用已经进过研究和实现的 Xaml 注入，**接管快速设置主面板(L1)的「选择声音输出」按钮**，点击后直接进入自定义页面（用自绘页面替换系统声音输出页的内容区）；两态切换由底栏按钮承担。

复刻 EarTrumpet 的针对不同应用单独调整音量和输出端点的功能，同时修复部分情况下可能丢失所有应用列表导致每个音频端点下方为空白 —— 此功能置于自定义页面 Body 第二顺位

支持调整系统内默认的音频输入\输出设备(两个下拉框) —— 此功能置于自定义页面 Body 第一顺位

支持「录制模式」开关（位于「默认输出设备」标签行右端）：勾选时系统默认输出切到虚拟设备 V、切之前那台默认设备转为监听设备 R；取消则反向切回。虚拟设备 V 不在程序内显示，只由该开关间接控制。

设置页作为自定义页的子页面（自定义页底栏中位进入）：含「开机启动」/「显示驱动名」两个开关与置底的「从系统卸载」（**连击 5 次**确认，相邻两次 ≤ 0.5s；执行内容 = **删除开机自启项 + 重启 shell**，不删程序文件，以回落系统原生行为）。设置页底栏左格 GitHub 地址链接（**`action=open`**）、中格返回、右格先占位。

支持一键把重定向的应用清除重定向(回到默认值) —— 此功能置于 Footer 的左侧

底栏按钮在自定义页显示「SystemMixer」、在系统页显示「MixerExtender」，两态之间来回切换。

---

# 备忘

## 已完成

- [X]  CMake + CMakePresets（VS18 / VS2022-BuildTools 两条生成器），产物落 `bin/<config>`，中间落 `obj/`
- [X]  目录按 EricGameLauncher 编排：根 = 主程序壳（`Program.cpp`/`CliService.*`）+ `Core/`（业务）+ `Components/`（嵌入载荷）
- [X]  `git status` 确认 `Components/` 只放载荷：`inject.launcher` / `inject.tap`
- [X]  注入载荷构建期嵌入 `vmex.exe` 的 RCDATA 资源，`vmex payload` / `vmex payload --extract=` 已验通（launcher 52224 / tap 46080 字节）
- [X]  版本号单一来源 `Version.h` → 驱动 `project(VERSION)`、三个二进制的 VERSIONINFO、CLI 版本输出
- [X]  `text.yaml` 构建期嵌入二进制；`-help` 与 `skill` 由命令注册表自动生成，无第二来源
- [X]  ⛔ **不再有 `SKILL.md`**：发布包只含 `vmex.exe`，接入文档完全内置于程序（`vmex skill`）。
  包内明文说明文件是第二来源，迟早和内置文档不一致 ⇒ 打包链路（`cmake/Package.cmake` +
  `CMakeLists.txt` 两处 `-DVMEX_SKILL_FILE`）与母法 §6 已同步改掉
- [X]  **存储位置已定**（2026-10-03）—— 三层，各有唯一来源：
  —— **用户配置**（要跨重启的东西）：`HKCU\Software\EricSoft\VolumeMixerExtender`（`Core/UserSettings.*`）。
  ⛔ 不再有任何 ini/文件副本充当"用户设置"，⛔ 也不存任何"事实的镜像"（如自启任务状态）
  —— **安装根 = 程序文件 + 部署期配置**：`%LOCALAPPDATA%\VolumeMixerExtender`
  （`platform::GetInstallDirectory()`）：`vmex.exe`、三个载荷、`vmex.ini`。
  ⚠️ **日志已从这里搬走**（原先 `vmex.log` 和程序文件混在一处）
  —— **缓存根（可丢）**：`%TEMP%\eric\VolumeMixerExtender`（`platform::GetCacheDirectory()`），
  与隔壁 EricGameLauncher 的 `%TEMP%\eric\<App>` 同级、同名结构
  —— **日志**：`<缓存根>\log\<role>-<yyyyMMdd-HHmmss>.log`，**一次进程会话一个文件**
  （`role` = `app` / `launcher` / `tap`）—— 对齐隔壁 EricGameLauncher 的 `log\app-<stamp>.log`；
  同秒同角色再来一个进程时加 `-2` 后缀（⛔ 不是删掉别人的会话日志）
  —— **载荷配置**：`vmex_tap.ini` 在**载荷自己所在目录**（launcher 从自身目录读，不是"放哪都行"）
- [X]  **无注释原则**（母法 §1）已落实到产品代码：`Core/`、`Components/`、根目录的 `.h/.cpp` 全无注释
  —— ⚠️ **`docs/poc/` 是例外**（已与 Eric 确认，2026-10-03）：那是 PoC 证据归档，注释保持原样
  —— ⚠️ 改文件只用编辑器工具，⛔ 不用 `Remove-Item`/脚本重写（母法 §2）
- [X]  build >= 26300 门禁（`Platform::VerifySupportedWindowsVersion`），不满足直接拒绝
- [X]  **宿主侧运行时已定并落地（2026-10-04）** —— ⛔ 不做托盘态/窗口态，只有「注入 + 后台进程」：
  `vmex host` 常驻（命名互斥体单实例）+ `Core/TapPipeServer` 收 TAP 动作报文 + `vmex status` 查状态；
  `Core/EndpointPolicyService` 从桩换成真实实现（`IPolicyConfig` 槽 13 已证实）；
  音频 CLI（`devices`/`default`/`redirect`/`clear-redirect`）接线；顺带修掉「进程从未 `CoInitializeEx`」
  与「`--verbose` 被 CLI 拒绝」两个真 bug。证据 `docs/verified-after-injection/15-host-pipe-and-device-policy.md`
- [X]  打包仅本地：Release 构建后自动产出 `output/VolumeMixerExtender_<version>.zip`；CI 只保留 `post_release`（上传产物 + 发完自动清理 output）与 `promote_prerelease`（48 小时后提升），不做 CI 编译
- [X]  修复：CLI 输出在重定向时全丢（`WriteConsoleW` 对非控制台句柄失败），改为控制台走宽字符、管道走 UTF-8
- [X]  **T12 / T13 / T14 前置验证** —— 全部通过：
  `ListContent.Content` 可写且系统内容可原样还原 ✅ / 换内容的最佳时机 = `Footer` 出现即够 ✅ /
  `ComboBox`、`ListView`、`Slider` 等 10 个控件类型全部可激活 ✅
  （证据 `docs/verified-after-injection/10-page-swap-capability.md`）
  —— ⚠️ 实现时注意：元素用 **C++/WinRT 直接激活**（`WUXC::ComboBox c;`），
  不是交付文档早前写的 `IVisualTreeService::CreateInstance`；该处已更正

## 待办

### 自定义页 UI

- [X]  自定义页布局**已定稿** —— 线框稿 `layout-preview.html`，规格落 `docs/design.md` §7.9
  （标签用「默认输出设备」/「默认输入设备」；应用列表不分组；下拉框与列表共用一个滚动区；
  应用行宽度恒定 330px、高度从 44 起按需长高；名称过长最多折两行；端点行单行省略 + `title` 全名；
  「默认输出设备」行右上角右对齐的「录制模式」复选框）
  —— ✅ 正文左右边距 `kBodyInset = 8`，底栏保持 `4`（镜像系统项）
- [X]  默认输入/输出设备两个下拉框（Body 第一顺位）—— ✅ 已落地 `MakeDeviceCombo`（`Page.h`）；
  选中即发 `SETDEFAULT <render|capture> <id>`（宿主侧 3 个 role 全设）
- [X]  「默认输出设备」行右上角**右对齐**的「录制模式」复选框
  （紧凑模板 `MinHeight=0` + 16px 方框；默认模板会把 16px 标签行撑到 32）
  —— **自动、双向对称**：勾选 → 记住当前默认 D、系统默认切到 V、`R := D`、标题 →「监听输出设备」；
  取消 → 系统默认切回 R、标题 →「默认输出设备」
  —— **硬约束：V 永不显示**（枚举 render 端点时过滤掉自己的虚拟端点，两态都要过滤）
  —— **录制模式不持久化（已定论，`design.md §7.9`）**：勾选状态不跨系统重启保存，
  每次启动一律从"未勾选"开始；D 只活在会话内存里
  —— **残留由驱动的「常开开关」结构性解决**（方案 §4.3）：V 的插头默认未插入，
  模式开时才由我们 IOCTL 闭合 ⇒ **不需要 `pendingRestore` / 启动时对账那套机制**
  —— ✅ **R 的唯一来源 = 勾选瞬间的默认设备 D**（复选框自动得出）。
  监听程序**不自带设置页、无 UI**，直接吃宿主传入的 R（原"监听程序设置页手选 R"已取消）
  —— ℹ️ 想换 R：录制模式勾着时，本行下拉框**就是 R 的选择器**（标题已变「监听输出设备」）
  —— ✅ **前置已验（T18）**：`CheckBox` 可激活、`IsChecked` 可往返、`MinHeight(0)` 可设、可挂树
  （证据 `docs/verified-after-injection/11-checkbox-capability.md`）
  —— ⛔ **UI 部分本机可做；"切到 V / 切回 R"要到实体机才能验**（本机没有 V，也没有第二个真实渲染端点）
  —— ⛔ **2026-10-04 修掉一个真 bug（用户实测日志暴露）**：勾选时发的报文是**硬编码设备名**
  `SETDEFAULT render OC Virtual Speaker` —— ① 违反"文本不硬编码"；② **设备名带空格，在按空格切分的报文里被截成 `OC`**，
  宿主于是拿 `OC` 去调策略（`E_INVALIDARG`），**命令看起来发了、实际全是垃圾**
  ⇒ 现在改发哨兵 **`@virtual`**，由宿主自己解析 V 的真实端点 ID（`IAudioDeviceManager::FindVirtualDevice`，
  ⛔ 页面侧枚举被"过滤 V"挡住了，本来也拿不到）；V 不存在时**明确记 `未找到虚拟端点 V, 未切换默认设备` 后放弃**
  —— ⛔ 同时**收紧了报文参数的个数校验**（`SETDEFAULT` 必须 2 段、`SETREDIRECT` 2 或 3 段、`CLEARREDIRECT`/`UNINSTALL` 0 段）：
  以前多余段会被**静默忽略**（正是上面 `OC` 的成因）
- [ ]  ⚠️ **录制模式里"选 R"的语义待定（本机无 V，测不出来）**：按设计，勾着时本行下拉框是 **R 的选择器**，
  但现行实现无论哪种模式都发 `SETDEFAULT <id>` ⇒ 会**把系统默认从 V 切走**（V 一出现，录制模式就自毁）
  —— 需要先定：选 R 只更新页面/宿主的 R（⇒ 还得有一条把 R 告诉**监听程序**的通道），还是允许它顺带改默认输出？
  —— ⚠️ 另一个相关缺口：`GetDefaultDevice` 走的是**过滤 V** 的枚举 ⇒ 录制模式开着时 `defaultRender` 只有 id 没有名字
  （`DeviceInfo.friendlyName` 为空），页面下拉框会处于"没有选中项"的状态
- [X]  **端点音量层**（插在「默认输入设备」与「音量合成器」之间）—— ✅ 已落地 `MakeEndpointRow` +
  采集侧折叠组 `「输入设备（N）」`（`Settings().inputExpanded`，默认不展开）
  —— **每个端点各一条**（不是按通道）；`EnumAudioEndpoints(DEVICE_STATE_ACTIVE)`；**过滤 V**
  —— **采集侧收成一行折叠组「输入设备（N）」，默认不展开**（渲染侧逐条展开）⇒ 输入侧高度封顶
  —— ✅ 滑块架构**已定方案 A：TAP 在 ShellHost 内直接调 WASAPI**，滑块流量**不走 IPC**
  （`action=pipe` 继续只承载"点击/命令"）
  —— ⛔ **安全分界已定（证据 `docs/verified-after-injection/12-audio-interop-safety.md`）**：
  枚举 / 端点音量/静音 / 逐应用音量/静音 **可安全内联**；
  **切默认设备 / 逐应用重定向 / 清空重定向必须走 `action=pipe` 到宿主**；
  —— ✅ **2026-10-04 更正**：`IPolicyConfig` 的**槽 13 就是 `SetDefaultEndpoint`**（`QI(IID F8679F50)` 成功；
  传真实端点 ID → `E_NOINTERFACE`、传其它任何 ID → `E_INVALIDARG` ⇒ 它确实在解析设备 ID）。
  早先记的"`vtable[13]` 访问违规"是**探针槽位映射偏移**，不是接口本身的问题
  —— ⚠️ **仍未定论（实体机）**：为什么对**本机 RDP 虚拟端点**调用会 `E_NOINTERFACE`
  （EarTrumpet 在真实硬件上同路径工作正常）⇒ 需实体机复验
  —— ⚠️ 实测环境（2026-10-03）：本机在 **RDP** 里，唯一渲染端点是 **`远程音频`**（不是之前记的网易虚拟声卡），
  **采集端点 0 个** ⇒ 页面要能优雅处理"0 个采集端点"（别画一个空的输入折叠组）
  —— ✅ **已按此实现**：`capture.empty()` 时不画折叠组，改画一行灰色「无采集设备」（`page.none.capture`），
  且「默认输入设备」下拉框 `IsEnabled(false)` + 占位文本 —— 已由 Eric 实机确认
  —— ⚠️ WASAPI 封装必须**两边共链**（TAP + App 各链一份，⛔ 绝不写两份 ——
  enumerations 顺序 / `DEVICE_STATE` 过滤 / V 过滤迟早不一致）
  —— ⚠️ `RegisterControlChangeNotify` 把外部改动（键盘音量键等）回写 UI；
  回调**不在 UI 线程** ⇒ 必须 `Dispatcher.RunAsync`
  —— ⚠️ 必须用 `Thumb.DragStarted/DragCompleted` 维护 `isDragging`，拖动中忽略外部回写
  （WinUI `Slider` **没有** `IsDragging`）
  —— ⚠️ 新增待验：**T23** 直调往返耗时 / **T24** 通知回调线程
  —— ⚠️ 用 `SetMasterVolumeLevelScalar`（0..1），**不要用 dB 版**
  —— ⚠️ 实测代价：收起态内容需 603px / 可用 306px ⇒ 超出 297px；展开 691px ⇒ 超出 385px
  —— ✅ **溢出已定：接受现状，不压缩**（行高保持 44px，靠共用滚动区消化；四个压缩旋钮全部否决）
- [X]  逐应用音量与输出端点（Body 第二顺位，复刻 EarTrumpet）
  —— ✅ 逐应用音量/静音已落地（TAP 内联 WASAPI，实测出值 22→100）
  —— ✅ **app 图标已落地（2026-10-04）**：`Core/AppIcons` 从进程 exe 提取 HICON → WIC 编码 PNG 缓存到
  `<缓存根>\icons\<hash>.png`，TAP 用 `Image` + `file:///` URI 渲染；取不到时回落"首字母色块"
  （实测日志 `应用图标已提取 …QQMusic.exe -> …\icons\FEA0F34CB67105FB.png`）
  —— ✅ **重定向 UI 已落地（2026-10-04，Eric 定稿：不用列表，用下拉框）**：
  点**整条标题带**（图标 + 名称 + 右侧 chevron，透明底保证空白处也可命中）向下展开 → 一个 `ComboBox`
  （第一项「默认设备」= 清除；其余为渲染端点，走 `显示驱动名` 设置）→ 选中即发 `SETREDIRECT`
  —— ✅ **重定向端点显示已落地（2026-10-04，对齐蓝图 `layout-preview.html`）**：应用行**名称下方常显一行**
  「→ 端点名」（该应用有重定向时才有），11px、**主题强调色**、单行省略、全名进悬停 `title`；
  数据与下拉框**同源**（页面本地 map）⇒ 选/清/重建三处都会跟着变；「显示驱动名」开关同样作用于这行（蓝图要求的"作用面三处"补齐）；
  系统声音行不画（它本来就不可重定向）
  —— ⛔ **下拉框改为"首次展开时才创建"**：原先在父面板还 `Collapsed` 时就建 `ComboBox`，
  实测会让弹出层残留、**整页输入被吃掉**；现在顺序是 先 `Visible` → 再创建 → 再挂树，
  且收起前先 `IsDropDownOpen(false)`
  —— ⛔ **系统声音行（`#system`）不给重定向**（对齐 EarTrumpet `IsMovable = !IsSystemSoundsSession`）：
  `pid=0` 的报文会被宿主判为非法，UI 上也不再出现 chevron/下拉
  —— ⛔ **状态记录只在真的生效时写**：策略调用全失败（本机 RDP 端点必然失败）时不写 `redirects.ini`，
  避免"UI 显示已重定向、实际没生效"的假象
  —— ✅ **底栏「清除重定向」现在有可见反馈（2026-10-04）**：点击 → 清页面本地选择 → `MountPage` 重建正文
  ⇒ 所有下拉框回到「默认设备」（此前命令虽成功但界面毫无变化，看起来像"没用"）
  —— ✅ 下拉当前值改由**页面本地 map 维护**（首次从 `redirects.ini` 播种，选择即更新，清除即清空）：
  ⛔ 不再每次读文件 —— 既省 IO，也消除"宿主删文件 / 页面重建"的竞态
  —— ⚠️ **本机可观察边界**：选「默认设备」→ 真生效（实测 `结果=0`）；选具体端点 → 系统拒绝
  （本机只有 RDP 虚拟端点）⇒ 真搬家只能到实体机验

### 设置页与开机自启

- [X]  **设置页**（自定义页底栏中位进入）已落地：标题「设置」+ 两个 `ToggleSwitch` +
  置底「从系统卸载」（危险色 `#E5484D`，5px 圆角 + 已武装态；见 `13-footer-mount.md` §3.3）
  —— 底栏三格：左 `GitHub`（**`action=open`**，`ShellExecuteW(..., L"open", url, ...)`，颜色保持中性灰）/
  中 `返回` / 右 `MixerExtender`
  —— ✅ **T19 `ToggleSwitch` 可激活已验**（两个开关在注入页里实际点过；证据见 `13-footer-mount.md`）
  ~~T20 后退键 `Cancel`~~ / ~~T21 `ContentDialog`~~ / ~~T22 打开 URL 用哪个 action~~ 均已关闭（⛔ 不要重开）
  —— ✅ 标题行系统后退键**保持系统默认行为，不拦**（两个返回键行为不同是接受的取舍）
  —— ✅ **「从系统卸载」的确认 = 连击 5 次**（不用 `ContentDialog`）：单击 → 文案转
  「再单击{N}次从系统卸载」（N = 还差几次，首次 4）；**相邻两次须 ≤ 0.5s**，超时立刻复位；
  第 5 次单击执行。`hits` 是纯 UI 态（关面板即丢，不需要持久化）；用 `DispatcherTimer` 回 UI 线程
  —— ✅ 卸载文案「从系统卸载」**不改**
- [X]  「显示驱动名」`ToggleSwitch`（默认开）：控制设备名要不要带括号里的驱动名后缀
  —— ✅ **配置持久化 = `HKCU\Software\EricSoft\VolumeMixerExtender`**（`Core/UserSettings.*`，`advapi32`）：
  值名 `ShowDriverName`，切换即写、`CommitTakeover` 时读回；
  ⚠️ 值名是**磁盘契约**，改名 = 静默重置；后续所有要持久化的配置都放这里
  —— **作用面三处必须同步**：两个下拉框的值 / 端点音量每行 / 应用行的"重定向目标端点"
  —— ⛔ **按"元素是不是设备名"判定，不能按"文本以 `(` 结尾"**（应用名里也有括号：
  `Microsoft Teams (工作或学校)`，按文本判定会把应用名削掉）
  —— 取值 Key：设备名 `PKEY_Device_FriendlyName`；应用名走会话/进程信息
  —— ℹ️ 实测**不省高度**（603→603px 不变），只影响折行 —— 是可读性设置，不是省空间设置
- [X]  「开机启动」= **`Run` 键**（2026-10-03 从"计划任务"改过来），
  **实现落点 `Core/AutostartEntry.*`（`IsEnabled` / `Enable` / `Disable`）**，规格见 `docs/design.md` §7.9.2：
  —— 值：`HKCU\Software\Microsoft\Windows\CurrentVersion\Run` 下 `VolumeMixerExtender`
  = `"<InstallRoot>\vmex.exe" host`（⚠️ 路径必须带引号：整条是一根命令行字符串；
  原 `--tray` 已废 —— 见"不做托盘态/窗口态"的新决定）
  —— **登录时**由 explorer 拉起（⛔ 不是开机时 —— 要注入的 ShellHost 是每会话进程）；
  天然继承当前用户**未提权**的令牌 ⇒ 与 ShellHost 同完整性级别，注入契约天然成立
  —— 开关语义：初值 = `Run` 值存在 **&& 未被系统标记为禁用**；开 = 写值 + 清标记；
  **关 = 删值 + 删标记**（⛔ 只删值会留下 `StartupApproved\Run` 里的"已禁用"灰项，下次打开又被判成禁用）
  —— ⛔ **禁用标记必须一起读**：用户在「任务管理器 → 启动」里关掉它时 Windows **不删**我们的 `Run` 值，
  只往 `HKCU\...\Explorer\StartupApproved\Run` 写一个同名 `REG_BINARY`（**首字节低位 = 1 表示禁用**）
  ⇒ 只读 `Run` 值会**画"开"而实际不启动**（同一条规矩：画事实，不画记录）
  —— ⛔ **不存任何副本**：自启开关不是配置，就是"自启项到底在不在"这个事实，⛔ 不写 ini、不写我们自己的 `HKCU` 键
  —— ⛔ `<InstallRoot>\vmex.exe` 不存在时**拒绝注册**（否则留下一个每次登录都失败、还赖在「启动」列表里的项）；
  路径取自 `platform::GetInstallDirectory()`
  —— ⛔ **读侧必须就地**：管道单向，payload 问不了宿主"现在开着吗"，而开关画的就是这个
  —— ✅ 已按 §6 补 CLI：`vmex autostart` 查询 / `vmex autostart on|off`（与设置页开关共用同一份实现）
  —— ✅ 实测（2026-10-03）：注册写入正确值；模拟用户在任务管理器里禁用（`StartupApproved = 0x03`）⇒ 开关读作"关"；
  关闭后 `Run` 与 `StartupApproved` 两个值都清干净
- [ ]  **开机自启端到端验收**（在真实登录场景下）
  —— ⚠️ 前提：`%LOCALAPPDATA%\VolumeMixerExtender\vmex.exe` 必须在（现在**只有 `vmex.ini`** ⇒
  开关点"开"会**故意失败**并记录"找不到 …\vmex.exe，拒绝注册"；验之前先把 exe 部署进去）
  —— 验：① 开设置页日志出现"自启项不存在 ⇒ 开关=关"；
  ② 点开 → `Run` 值出现 + 开关停在"开"；
  ③ 在「任务管理器 → 启动」里禁用 → 重开设置页显示"关"；
  ④ 点关 → 两个值都消失（⛔ 不留灰项）；
  ⑤ 真登录一次，确认 `vmex.exe host` 起来了（⚠️ 会闪一下黑窗，已接受）
- [ ]  「从系统卸载」的实际执行者（连击 5 次确认的 UI 已完成，`Core/UninstallService` 未做 —— 见宿主侧）

### 入口接管与页面识别（已通）

- [X]  入口机制：不做拦截，改为"检测声音页出现 → 保存并替换 `ListContent.Content`"
  —— ✅ 已落地 `Components/inject.tap/Tap.cpp`（TAP COM 对象 + 类工厂 + hook）
  + `Page.h`（自定义页本体，按 `layout-preview.html` 实现，消费 Core 音频接口）
  —— ✅ **已注入验证**：Debug/Release 均编译链接通过（零警告），注入 ShellHost 后接管、往返、关面板再打开都正常
- [X]  页面识别（⛔ 不能只按 `Name=="Footer"` 匹配，否则接管所有 L2 页面）—— 详见
  `docs/verified-after-injection/14-page-identification.md`
  —— ⛔ 所有 L2 页面**共用同一套壳**（`PageWindow`/`PageHeader`/`PageContent`/`Footer` 逐项同名同结构），
  `投影` 连标题控件名（`PageTitleText`）都和声音页一样
  —— ✅ 判据一（入口，快路径）：L1 `VolumeL2Button` 的 `Click` → 5s 窗口内下一个 `Footer` 直接接管；
  ⚠️ **`Win+Ctrl+V` 不走它**（实测）
  —— ✅ 判据二（兜底，全覆盖）：页面自己建的内容里有
  `OutputGroupTitle` / `MixerGroupTitle` / `SpatialGroupTitle` / `ListWithOutputGroupTitle`
  —— ⛔ 这些名字在页面根往下 **~25 层**（两层虚拟化列表），`FindByMarker` 上限 24 会**静默找不到**
  —— ⛔ 识别是深度 32 全树遍历，挂 dispatcher `Low` 上**必须节流**（实测 Low 450ms 能排空 4450 次）；
  但"只靠事件驱动"会漏掉内容落地那一刻（底栏 layout 事件先停）⇒ 链 + 25ms 节流
  —— ✅ 四条路径实测：按钮 / `Win+Ctrl+V` 接管；辅助功能 / 投影 **原样不动**
  —— ⚠️ 判据是「**上次接管的那一页是否仍挂在树上**」，**不是**一次性置位 —— 面板每次打开 XAML 树都重建，
  一次性置位会让第二次打开起**再也不接管**
- [X]  底栏三格挂载（自定义页 / 设置页 / 系统页三态）
  —— ✅ 几何与系统项一致（`40` 高 / 左右各 `4px` 内缩 / 同一行居中）；三态往返实测通过
  —— ⛔ **三个根因**（都已修，别再踩）：① `FindDescendant` 深度上限 **8**，而模型按钮在第 **12** 层
  ⇒ 永远找不到；② 一次性守卫 `std::atomic m_takenOver` **从不复位** ⇒ 关掉再打开面板就**再也不接管**
  （XAML 树每次打开都重建）⇒ 判据改成「上次那一页是否**仍挂在树上**」，⛔ 也不要退回"记住指针身份"；
  ③ 纵向 `StackPanel` 只给子元素 desired height ⇒ 三格贴顶且只有 16px 高 ⇒
  `row.MinHeight(ItemsPanel.ActualHeight())` + 抄模型**显式** `Height(40)` + 左右各 4px 内缩
  —— ℹ️ `Footer` 是**两层嵌套 `ItemsControl`**（完整实测树见 `13-footer-mount.md` §1）
  —— ⛔ 三格用**一个网格单元 + `HorizontalAlignment`（左/中/右）**，⛔ 不用星号列：
  星号列会把中格放到 `中心 + (左宽−右宽)/2`，设置页偏 19px
  —— ⚠️ 底栏三格必须**一格一 handler + 按"当前页"dispatch**，**不能每到一页新挂 `Click`**（handler 会累积）
  —— ✅ **正文与底栏必须同源**：改正文一律走 `MountPage(context, page)`（它同时 `RefreshFooter`），
  ⛔ 不收"顺手 `list.Content(...)`"这种旁路；`MountPage` 现在打日志（页码 + 页名）⇒ 失配在日志里可见
  —— 详见 `docs/verified-after-injection/13-footer-mount.md`

### 宿主侧与 Core（本机可做）

- [X]  **运行时形态已定（2026-10-04，Eric）**：⛔ **不做托盘态、不做窗口态** —— 只有「注入 + 后台进程」，
  状态一律由 `vmex status` 查询
- [X]  **后台进程 + TAP 动作接收端**（2026-10-04）—— 三步链路已通，证据 `docs/verified-after-injection/15-host-pipe-and-device-policy.md`
  —— `Core/TapCommand.*`：`<VERB> <args…>` 解析（`CLICK` / `SETDEFAULT` / `SETREDIRECT` / `CLEARREDIRECT` / `UNINSTALL`），
  未知动词返回空、**参数个数不符**（`SETDEFAULT` 2 / `SETREDIRECT` 2–3 / `CLEARREDIRECT`、`UNINSTALL` 0）
  与参数不足一律由派发侧丢弃并记 `报文参数不合法, 已忽略`
  —— `Core/TapPipeServer.*`：named pipe 服务端（`PIPE_ACCESS_INBOUND` 单实例）
  —— ⚠️ **三个坑照 `08-pipe-action-chain.md` 落实**：`ConnectNamedPipe` 的 `ERROR_PIPE_CONNECTED(110)` 与
  **`ERROR_NO_DATA(232)`** 都算成功（缓冲仍可读）、逐行累积（防半行/多行）、末尾必须 `DisconnectNamedPipe`；
  `Stop()` 用"自连一次"唤醒阻塞线程（不引入 overlapped）
  —— `Core/HostPresence.*`：`Local\VmExt.Host.S<sid>` 命名互斥体 ⇒ **单实例** + 另一个进程可查"在不在跑"
  —— CLI：`vmex host` 常驻（`Ctrl+C` 优雅退出）；`vmex status` 真实回显「后台进程：运行中/未运行」+ 管道名
  —— ✅ 实测：报文四类分支（合法 / 参数不足 / 未知动词 / 尚未实现）全部按预期落日志；第二个 `vmex host` 被拒（`host-running`）
  —— ✅ 实测：`vmex default <id>` / `vmex devices` / `vmex clear-redirect` 接线完成；`clear-redirect` 端到端 `S_OK`
  —— ⚠️ **本轮发现并修掉两个真 bug**：① 进程从未 `CoInitializeEx`（音频命令此前全是桩，故未暴露）
  ⇒ 现主线程 MTA 初始化 + 管道线程每次派发自行初始化；② `--verbose`/`--trace` 被 CLI 当未知选项拒绝
  ⇒ `Program.cpp` 在交给 `CliService` 前剔除这两个全局开关
- [X]  **`Core/EndpointPolicyService` 接 `IPolicyConfig` / `IAudioPolicyConfigFactory`**（2026-10-04）
  —— ✅ 已落地：复用 `Core/AudioInterop.h` 既有封装（宿主进程内执行，⛔ 不在 ShellHost 里试错）
  —— ✅ **槽位之谜有定论**：`CoCreateInstance(PolicyConfigClient)` + `QI(IID F8679F50)` 在 26H2 **成功**；
  **槽 13 = `SetDefaultEndpoint`**（传真实端点 ID → `E_NOINTERFACE`，传其它任何 ID → `E_INVALIDARG`，
  说明它确实在解析设备 ID）⇒ 旧记录"`vtable[13]` 访问违规"是**探针槽位映射偏移**，现行声明不再 AV
  —— ⛔ **本机仍然验不了**：RDP 虚拟端点下"改具体端点"被系统拒绝
  （`SetDefaultEndpoint` → `E_NOINTERFACE`；`SetPersistedDefaultAudioEndpoint` → `E_INVALIDARG`，
  raw/packed 设备 ID 与 `pid=0`/真实 pid 都试过；**`ClearAll` → `S_OK`** 证明调用形态无误）⇒ **实体机复验**
  —— 对应 pipe 报文：`SETDEFAULT` / `SETREDIRECT` / `CLEARREDIRECT`（`AUTOSTART` 已从协议表删除）

- [X]  ⛔ **子系统改造：不做（2026-10-03 决定，原 T25 关闭）**
  —— `vmex.exe` 保持控制台子系统 ⇒ 登录自启时**会闪一下黑窗**，**接受**
  —— ⚠️ 换成 `Run` 键**免不掉**这一下：闪不闪由**镜像声明的子系统**决定，与"谁拉起它"无关
  （从命令行跑不闪，只因继承了 cmd 的控制台）
  —— ℹ️ 真要去掉只有那条改造：`WIN32` 子系统 + `wWinMain` 开头 `AttachConsole(ATTACH_PARENT_PROCESS)`
  （命令行跑 → 附着父控制台；无父控制台 → 只写文件日志），并给 `CliService` 加"没有控制台"分支
- [ ]  **`Core/UninstallService`：「从系统卸载」的执行者**（§5.7.8）
  —— 顺序**不可换**：① 写 `enabled=0` ② 停 Watcher ③ **删除自启项**（`AutostartEntry::Disable()`）④ 重启 shell ⑤ 自己退出
  —— ⛔ **不删程序文件 / 不卸驱动 / 不删配置** —— 语义是"退出接管、回落原生行为"，**不删程序本身**
  —— 由 TAP 经 `action=pipe` 发 `UNINSTALL` 报文触发；**pipe 不可用时 TAP 拒绝执行**（不降级）
  —— ⛔ **必须由 App 执行，不能由 TAP 执行**：TAP 活在 ShellHost 里，重启 shell = 它当场自杀；
  且只有 App 能在杀 ShellHost **之前**先停 Watcher，否则 TAP 会被立刻重新注入回来
  —— ✅ **接收端已就绪**（2026-10-04）：`UNINSTALL` 报文已能被宿主收到并记日志，现在只差"执行体"本身
  —— ⚠️ 重启 explorer 的坑：杀完**必须确认它起来了**（Windows 有 `AutoRestartShell`，但别指望）——
  没起来就自己拉起，否则用户没有桌面和任务栏（`§1.6` 已实测 ShellHost 会随 explorer 重启换新进程）
  —— ⚠️ App 启动时**必须写回 `enabled=1`**，覆盖卸载留下的 0，否则手动重开会出现"在跑但不注入"
  —— ✅ 不用管驱动：**没有端点时 Windows 不会显示它** ⇒ 卸载后无"看得见的残留"
- [ ]  ⚠️ **外部改动回写**：`RegisterControlChangeNotify` → `Dispatcher.RunAsync`（约束 A2）—— **未做**
- [X]  载荷 DLL 自身的日志文本已走 text.yaml（`text::Embedded()` + `LogKey`）
  —— ✅ 已落地：`Tap.cpp` / `Page.h` 的日志全部改成文本键（`log.tap.*` / `log.page.*`）
- [X]  **注入已落地（2026-10-04）** —— `Core/InjectionService` 从桩换成真实实现，`vmex host` 常驻时自动监视并注入
  —— `Core/PayloadDeployment`：按 **载荷内容哈希 + 尺寸** 生成版本目录 `<缓存根>\payload-<hash16>-<launcherSize>-<tapSize>`，
  释放 `vmex_launcher.dll` / `vmex_tap.dll` 并写 `vmex_tap.ini`（UTF-16LE+BOM，复用 `ConfigService`）
  ⇒ 重建后自动换新目录，不会与 ShellHost 里已加载的旧 DLL 抢文件
  —— ⛔ **2026-10-04 修掉一个真 bug**：原来目录名只用**尺寸**，而"尺寸一致就跳过释放"的省写盘逻辑会让
  **同尺寸重建复用旧 DLL**（本轮 tap 恰好两次都是 `2611712` 字节）⇒ 注入的是上一版、日志却一路正常
  ⇒ 目录名加入 `payload::PayloadContentHash()`（FNV-1a，数据本就在内存里，零额外 IO）；实测部署产物与 `bin\Debug` 哈希一致
  —— ⚠️ 副作用：同尺寸每次重建都会生成新目录，`%TEMP%` 会积累旧载荷（**自动清理未做**）
  —— `Core/InjectionService`：`ShellHost.exe` 定位（本会话 + `ProcessIdToSessionId` 校验）、
  远程 `LoadLibraryW`（**远程 kernel32 基址 + 本地 RVA**，⛔ 不用本进程地址）、等 TAP 模块出现（100 ms 轮询）
  —— `Core/InjectionMonitor`：1000 ms 轮询，`tapLoaded` 就不再动手；ShellHost 换进程会自动重新注入
  —— ⚠️ **发现并规避**：ShellHost 里已有 TAP 时**不做二次注入**（否则会出现两个按钮）；若已加载的载荷路径与当前不一致，
  记 `ShellHost 里是旧载荷, 需重启 ShellHost 才能换新` —— 开发期换代码必须重启 ShellHost
  —— ✅ 实测：`vmex inject` → 载荷部署 → 定位 pid → `TAP 已就绪`（约 340 ms）；
  launcher 日志 `注入成功 endpoint=VisualDiagConnection1`；TAP 日志 `自定义页已接管` + `底栏三格已挂载（219 ms）`
  —— ⛔ **`eject` 仍未实现**（需要先注销 XAML 诊断会话，贸然卸载会带走 ShellHost）—— 保持 `NotSupported` 并记日志
- [X]  把 PoC 已验通的逻辑搬进产品：Footer 定位、样式抄写、2 列 Grid 换行、幂等判定（基于"结果存在性"而非指针）、
  `action=pipe` 动作链（`ERROR_NO_DATA`(232) 视为正常）
  —— ✅ 已落地：Footer 定位/接管/幂等判定在 `Components/inject.tap/Tap.cpp`（**必须活在 ShellHost 里**，
  ⛔ 不能搬进 App 进程），管道服务端在 `Core/TapPipeServer.cpp`（110/232 都算成功、逐行累积、末尾 `DisconnectNamedPipe`）
- [X]  `Core/AudioDeviceManager` 接 WASAPI（枚举 / 默认设备 / 端点音量 / 会话枚举 / 会话句柄）
  —— ✅ 已落地：`Core/AudioInterop.h`（detail 层，实测过）+ `Core/AudioDeviceManager.cpp`
  —— ⚠️ **必须做成 TAP 与 App 共链的静态库/仅头实现**（§5.7.9 约束 A1）：已满足（`vmex_core`）
  —— ⚠️ B1–B4 反 EarTrumpet `#1305` 约束已写进实现（不跨时间缓存、以重新枚举为准、失效即重取、每次开面板重枚举）
- [X]  `Components/inject.launcher`：接通 `InitializeXamlDiagnosticsEx` 调用
  —— ✅ 已落地 `Launcher.cpp`：自身目录读 `vmex_tap.ini`、定位 `xamldiagnostics.dll`、
  把配置**直投**给 TAP 模块、等 `Windows.UI.Xaml.dll`、走 `VisualDiagConnection<N>` 探测
  —— ✅ 两个入口：`DllMain` 起线程自走（供"只 LoadLibraryW"的宿主），
  以及同步导出 `VmExtLauncherRun(config)`（宿主拿 HRESULT）
  —— ⛔ **`InitializeXamlDiagnosticsEx` 的签名第 5 参是 `CLSID`（按值），不是字符串** —— 写错不报错，直接毁栈
- [X]  `Components/inject.tap`：`DllGetClassObject` 类工厂 + `Tap` 对象
  —— ✅ `TapFactory` / `CreateFactory()` / `DllGetClassObject`（按 `kTapClassId` 比对）
  —— ✅ `Tap`：`IVisualTreeServiceCallback` + `IObjectWithSite`，`SetSite` 里 QI `IXamlDiagnostics` +
  `IVisualTreeService` → `AdviseVisualTreeChange(this)`

### 重定向状态的真值来源（**明天做 · 2026-10-04 Eric 定方向，本轮先不做**）

- [ ]  ⛔ **结论（Eric 2026-10-04）**：重定向操作的本就是**系统底层维护的那张逐应用持久默认端点表**
  （`IPolicyConfig::SetPersistedDefaultAudioEndpoint` → AudioSrv 的持久化表），**我们不该自己再维护一份镜像**
  ⇒ 目标是**删掉 `redirects.ini` 与 `Core/RedirectStore.*`**，面板改为**现读系统真值**
  —— 相关文档说法一并作废：`16` 号 §2.1/§5 里"页面本地 map""只有生效才写 ini"的说法要回头改写
- [ ]  **「清除重定向」的语义确认（Eric 的判断，与现行实现一致）**：它就是**把系统那张表整体恢复默认**，
  **⛔ 与"要清除的应用此刻开没开"无关** ⇒ 现状已经是这样：`CLEARREDIRECT` → `ClearAllPersistedApplicationDefaultEndpoints()`
  （不需要枚举进程、不需要该应用在跑）；**唯一多余的是"顺手删我们自己的 ini"那一步**，随 ini 一起消失
- [ ]  **前置探针（先验再动手）**：在 **TAP（ShellHost）内**调 `IAudioPolicyConfigFactory`
  （`Windows.Media.Internal.AudioPolicyConfig`；IID `ab3d4648-…`（21H2）/ `2a59116d-…`（downlevel））的
  `GetPersistedDefaultAudioEndpoint(pid, flow, role, &hstring)`，要问清四件事：
  —— ① 在 ShellHost 里**能不能激活并取到值**（12 号已证"可激活、无 AV 风险"，但**get 未验**）；这是唯一红线：**别崩 shell**
  —— ② **没设过时返回什么**：若返回"当前系统默认设备 ID"，则**必须再与当前默认对比**才能判定"是否被重定向"
  （EarTrumpet 就是这么判的），否则会把没重定向的应用画成"已重定向到系统默认"
  —— ③ per-role 差异（`eConsole` / `eMultimedia` / `eCommunications`）——决定读一次还是读三次、以哪个 role 为准
  —— ④ 读的耗时（每次开面板要读 N 个应用 ⇒ 可能得按 `eMultimedia` 单次读）＋ pid 复用/僵尸进程的容错
  —— 参考：`Core/AudioInterop.h` 的 `GetPersistedEndpointForRole` / `GetPersistedEndpoint` 已实现（`detail` 层），
  `IEndpointPolicyService::GetAppDefaultDevice` 也在，但**目前是死代码没人调**
- [ ]  **备选路（只在①' 失败时才考虑）**：宿主读真值 + 新增一条**宿主 → 页面**的回推通道（管道改双向或另开通知）
  —— 读操作留在宿主最安全，但成本高（管道契约、生命周期、时序都要重做）
- [ ]  ⚠️ **不改就会一直存在的三处"记录 ≠ 事实"**（存档，改完自然消失）：
  —— ① **CLI 不写它**：`vmex redirect` / `vmex clear-redirect` 直接改系统状态 ⇒ CLI 改完再开面板**显示旧值**
  （与"CLI 全功能"原则天然冲突）
  —— ② `appKey` 是**小写 exe 全路径** ⇒ 应用升级/换目录后留下**孤儿条目**，永不清理
  —— ③ 用户在系统设置里手改、或被系统清掉 ⇒ 记录过时（现有实现只挡了"策略失败也写"这一种假象）
- [ ]  现状使用面（存档）：**写** = 宿主 `RecordAppRedirect`（仅当策略真的成功）；**读** = TAP 页面
  （下拉框选中值 + 应用行「→ 端点名」）；**删** = `HandleClearRedirect` 删整个文件。
  ⚠️ 本机（RDP）策略全失败 ⇒ `redirects.ini` **至今根本没被创建过**

### 注入链（已通 · 剩余未验）

- [X]  **载荷必须用静态 CRT**（`CMAKE_MSVC_RUNTIME_LIBRARY = MultiThreaded[Debug]`）
  —— ⛔ 动态 CRT 会让注入**必然失败**：ShellHost 只搜自己目录 + System32，
  `vcruntime140*.dll` / `ucrtbase*.dll` 都不在里面 ⇒ `LoadLibraryW` 返回 126
  （PoC 当年全部载荷都是 `/MT`，原因就在这里）
  —— ✅ 已实测：`dumpbin /dependents vmex_tap.dll` 只剩 `api-ms-win-*` + `OLEAUT32.dll`
- [X]  **载荷必须 `/guard:cf`** —— ShellHost 是 CFG 进程，**间接调用**到未注册为
  合法调用目标的镜像会 **fast-fail `0xC0000409`（子码 10 = `FAST_FAIL_GUARD_ICALL_CHECK_FAILURE`）**
  —— ⛔ 症状：进程**当场死**，日志一行都不写（崩在函数入口之前）
  —— ⛔ 我们踩过：`VmExtLauncherRun` 远程调用 → **ShellHost 直接崩**（WER 1000 + 1001 可查）
  —— ✅ 加 `/guard:cf` 后 DLL characteristics 出现 `Control Flow Guard`（`guardN` 245/245）
  —— ℹ️ 只有"从外部**间接调用**载荷导出"才会踩到；`DllMain` 自走那条路不受影响（loader 直接调用）
  —— ⛔ 工具坑：`injector.exe --call` 会让 ShellHost CFG fast-fail（`0xC0000409` 子码 10，日志一行不写）；
  可用的是**不带 `--call`** 的自注入路径（`DllMain → BeginSelfInjection`）
- [X]  **T26** `InitializeXamlDiagnosticsEx` 的**端点名编号** —— ✅ 产品路径已定并实测：
  起点固定 **1**（`endpoint=` 键，缺省 1），launcher 日志 `注入成功 endpoint=VisualDiagConnection1`
  —— ⚠️ **仍未被证实的假设**："端点被占就退到下一个编号"（实测 `VisualDiagConnection2..10000`
  全部返回 `HRESULT_FROM_WIN32(ERROR_NOT_FOUND)` = 0x80070490）⇒ 当前实现**不依赖**该假设（固定 1）
  —— 待验（低优先）：占着 1 再注入第二个实例会怎样；编号是否与其它东西（如进程会话）绑定
  —— ℹ️ PoC 当年只试过 `1` 就成功了，所以这条从来没被区分过
- [X]  **T27** 注入器/宿主**不能用本进程 `LoadLibraryW` 去解析导出地址**
  —— ✅ 产品侧 `Core/InjectionService`：**远程 kernel32 基址 + 本地算出的 RVA**（⛔ 不用本进程地址）
  —— ✅ PoC 侧 `docs/poc/src/injector.cpp`：直接**解析 PE 导出表**取 RVA（零副作用）
  —— ⛔ 该文件同时新增 `--call <导出> [--arg <串>]`，用于同步拿 HRESULT / 已加载进程内重试
- [X]  注入后开面板验证，确认自定义页 + 底栏三格行为与 `layout-preview.html` 一致
  —— ✅ 已由 Eric 实机交互确认（两轮）：页面接管、底栏、逐应用音量/静音、**整条标题带可点开重定向**、
  下拉选「默认设备」= 清除且有可见反馈、`#system` 行不给重定向（pid=0 非法）
  —— ✅ **2026-10-04 补上蓝图里缺的"重定向端点显示"**：应用行名称下方常显一行「→ 端点名」（有重定向时才有）
  —— ⛔ 唯一未通：**改具体音频端点**（本机 RDP 端点下 `SetPersistedDefaultAudioEndpoint` → `E_INVALIDARG`，环境限制）
  —— ℹ️ 逐像素对齐未单独出报告；几何验收见 `docs/verified-after-injection/13-footer-mount.md`
  —— 载荷目录（含日志/配置）建议用 `%TEMP%\<某个目录>`；⛔ 别放 ShellHost 自己目录（不可写）
  —— ℹ️ 面板 band=4 ⇒ `EnumWindows` / `FindWindow` / UIA `RootElement` 都看不到，只能前台窗口截图
  —— ⚠️ UIA 还得先钻到 `FrameworkId == "XAML"` 的**子 HWND**，否则 `Descendants` 一个都找不到
  —— ✅ 底栏三格已挂上，几何验收见 `docs/verified-after-injection/13-footer-mount.md`

### 驱动层（**最后做** · 换实体机验）

> **排期决定（2026-10-03，Eric）**：驱动层放到**最后**做，且**在实体机上**开发与验收。
>
> 原因：**本机没有真实音频硬件**，而且（2026-10-03 重新枚举）本机在 **RDP** 里：
> 全机只有 **1 个渲染端点**，是 RDP 的 **`远程音频`**（`{3.0.0.00000002}.{6C26BA7D-…}`），
> **采集端点 0 个**（ACTIVE/DISABLED/NOTPRESENT/UNPLUGGED 四种 state 全查过）。
> ⚠️ 这**修正**了本节早先记录的「网易虚拟音频设备 / 扬声器 + 麦克风阵列」—— 环境变了，且 RDP 会改变端点集合。
> ⇒ **任何涉及"多设备 / 切默认 / 真实设备 R"的验证都无法在本机完成**（见各处 ⛔ 标记）。
> 证据：`docs/verified-after-injection/12-audio-interop-safety.md`。
>
> 依赖关系：**驱动层的产出（V 端点）是"录制模式"和监听程序的前置**；
> 在 V 存在之前，录制模式只能验 UI、监听程序无从跑起。
> ⚠️ 而且监听程序**连"骨架测试"都得等实体机**：镜像需要 **≥2 个真实渲染端点（两个独立时钟）**，
> 本机只有 1 个。⇒ **整个监听程序（含其技术栈）都属于这一阶段，别提前做。**

- [ ]  **① V 驱动本体**：PortCls 改造 SYSVAD（只留单 Render 端点、去蓝牙/APO、音量节点固定 0 dB 不衰减）—— 方案 §4/§5/§6
- [ ]  **② 插头常开开关**：只注册设备、默认"**未插入**"；宿主 IOCTL 闭合/断开；
  **闭合必须绑定调用方的文件句柄，`IRP_MJ_CLEANUP` 兜底回未插入**（不能只靠主动发"拔掉"IOCTL）
  —— 机制：`KSJACK_DESCRIPTION.IsConnected` + `JACKDESC2_PRESENCE_DETECT_CAPABILITY` +
  `KSEVENT_PINCAPS_JACKINFOCHANGE` + `KSJACK_DESCRIPTION3.ConfigId`，详见方案 §4.3
- [ ]  **③ 监听程序**（V → R）：采集 V 的 WASAPI loopback → 渲染到 R；格式协商 + 时钟漂移重采样 —— 方案 §7
  —— 🅿️ **技术栈开工时再定**（方案 §7.3 现在只是留档的技术备注，⛔ 别当已定决策引用）
  —— 当前工作假设：全 C++ / Core Audio（仓库零 .NET 依赖）
- [ ]  ⛔ **驱动侧前置验证**（方案 §11 的 7–11）：免重启翻转 / 消失即自动回滚 / 重现是否抢默认 / **崩溃兜底**

### 收尾

- [ ]  `.releasenote.md` 已按母法 `releasenoteguide.md` 建立（**内容在母法迁移时清空**，发版前按该指南重写）
- [ ]  等 `install` 类能力落地后，补 `.install.cmd`（隔壁的本地安装入口）
