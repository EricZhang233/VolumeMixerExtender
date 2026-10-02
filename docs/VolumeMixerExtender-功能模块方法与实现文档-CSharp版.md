# VolumeMixerExtender 功能模块方法与实现交付文档

| 项 | 值 |
|---|---|
| 文档版本 | 1.0 |
| 日期 | 2026-10-03 |
| 目标平台 | Windows 11 build 26300.9550（实测环境） |
| 主语言 | C#（控制面） + 原生 C++（in-proc 两段） |
| 姊妹文档 | `VolumeMixerExtender-功能模块方法与实现文档-Cpp版.md`（C++ 轨道；§1.0 有两条轨道的选型对照表） |
| 文档状态 | 架构与契约已定稿；PoC 已跑通；文档中标 ⚠️ 的条目待 V 阶段验证 |
| 来源 | 全部结论来自 `docs/verified-after-injection/` 的实测记录，映射表见 [附录 B](#附录-b-poc--产品代码映射表) |

---

## 0. 文档说明

### 0.1 读者与用途

本文是**功能模块级交付文档**，面向要接手实现/维护本项目的开发者。它回答四件事：

1. **有哪些模块**（进程内/进程外、托管/原生、职责边界）；
2. **每个模块有哪些方法**（完整签名、参数、返回、异常、线程约束、幂等性）；
3. **方法之间怎么串**（时序、状态机、契约、失败降级）；
4. **怎么验证和排障**（分级验证、验收用例、症状-原因-动作表）。

本文**不是**用户手册，也**不是**研究笔记。研究过程与全部原始实测数据在：
- `Win11-QuickSettings-XAML-Injection-Notes.md`（仓库根，§0-§11 全部探测结论）
- `docs/design.md` §6（方案选型、证伪过程、最终落地）
- `docs/reference/`（原始输出）

### 0.2 标记约定

本文中每个技术断言都带一个标记，请按标记决定信任程度：

| 标记 | 含义 | 处理方式 |
|---|---|---|
| ✅ | **已实测验证** | 可直接依赖 |
| ⚠️ | **设计推断/未验证** | 编码前必须先做对应 V 阶段验证，不得直接依赖 |
| ⛔ | **硬限制**（OS/体系结构决定，不可消除） | 设计必须绕开，不要试图解决 |

### 0.3 术语表

| 术语 | 含义 |
|---|---|
| **面板** / ControlCenter | Win11 快速设置窗口，窗口类 `ControlCenterWindow`，标题「快速设置」 |
| **声音输出页** | 面板内按「音量」进入的全屏页（`FullScreenPage`），底栏即注入点所在 |
| **底栏** / Footer | 声音输出页底部的 `ItemsControl`，`AutomationId`/`Name` = `Footer`，几何 `(2189,1417) 358x48` |
| **模型按钮** | 底栏里已有的「更多音量设置」按钮，几何 `(2193,1420) 94x40`，用于取样式与位置基准 |
| **ShellHost** | `ShellHost.exe`，托管面板 XAML 的宿主进程。**注入目标**。父进程是 `sihost.exe` ✅ |
| **band / banded window** | 非默认窗口 band。面板 `GetWindowBand = 4`，因此 `EnumWindows`/`FindWindow`/UIA `RootElement` 都看不到它 |
| **TAP** | Type Activation Provider。XAML 诊断框架用来与调试器/工具通信的进程内 COM 对象。本项目自写一个 |
| **in-proc 两段** | 必须在 ShellHost 进程内执行的两段代码：Launcher（调用诊断入口）与 TAP（接收树事件并改树） |
| **L1 / L2** | 快速设置的两层 UI：L1 = 主面板（快捷开关网格），L2 = 全屏子页（声音输出页等） |
| **XamlRoot / island** | 一个独立的 XAML 内容根。ShellHost 里同时存在多个 |

### 0.4 交付物清单

| # | 交付物 | 类型 | 位置（安装后） | 必须交付 |
|---|---|---|---|---|
| D1 | `VmExt.App.exe` | 托管可执行（托盘宿主 + 配置界面） | 根目录 | 是 |
| D2 | `VmExt.Core.dll` | 托管类库（配置/日志/常量/P-Invoke） | 根目录 | 是 |
| D3 | `VmExt.Service.dll` | 托管类库（编排/监视/IPC 服务端） | 根目录 | 是 |
| D4 | `VmExt.Launcher.dll` | **原生** DLL（in-proc 第 1 段） | `native\` | 是 |
| D5 | `VmExt.Tap.dll` | **原生** DLL（in-proc 第 2 段，COM in-proc server） | `native\` | 是 |
| D6 | `vmext-tap.ini` | TAP 配置（文本，可热重载） | `config\` | 是 |
| D7 | `appsettings.json` | 应用配置 | `config\` | 是 |
| D8 | `install.ps1` / `uninstall.ps1` | 安装/卸载脚本 | `tools\` | 是 |
| D9 | `xamldiagnostics.dll` | **来自 Windows SDK**，不是我们写的 | 由 SDK 路径指向，不随包分发 | 前置依赖 |
| D10 | 本文档 + 研究笔记 | 文档 | `docs\` | 是 |

### 0.5 明确不做的事

以下在 v1 **明确不做**，写在文档里是为了避免后续被当成缺陷：

1. 不做多用户/多会话同时注入（只处理**当前会话**；检测到其它会话的 ShellHost 直接跳过）。
2. 不做「面板已打开时立刻生效」的保证（面板每次打开都重建 XAML 元素，本方案在**下次打开**时生效）。
3. 不做 TAP 的卸载（⛔ 见 §10.1）。「禁用」通过让 TAP 什么都不做实现。
4. 不 hook shell 的任何函数、不改 ShellHost 的任何代码段、不写注册表 hook 项。
5. 不依赖 Windhawk / ExplorerPatcher / 任何第三方 mod 框架。
6. 不做驱动（本机无 WDK；本方案也不需要）。

---

## 1. 系统总览

### 1.1 目标功能（一句话）

在 Win11 快速设置 →「声音输出」页的**底栏右侧空位**注入一个**原生 XAML 按钮**（真元素，不是悬浮层），点击后执行配置好的动作。

### 1.2 为什么必须分两层（硬性技术约束）

这一节是整份文档的地基，**它决定了 C# 能做什么、不能做什么**。三条约束互相独立，缺一条这个架构就可以简化：

| # | 约束 | 后果 |
|---|---|---|
| C1 | ✅ `InitializeXamlDiagnosticsEx` 由**目标进程自己的** `Windows.UI.Xaml.dll` 导出，**只能在目标进程内调用**。没有任何 out-of-proc 版本 | 必须有一段代码跑在 ShellHost 里 ⇒ 必须有 DLL 注入 |
| C2 | ✅ TAP 是被 XAML core 用 `LoadLibrary` + `DllGetClassObject(CLSID)` 加载的**原生 COM in-proc server**，要求导出 `DllGetClassObject`、手搓 `IUnknown`/`IVisualTreeServiceCallback` vtable | 这段**不能是托管代码**（除非 CLR hosting，见下） |
| C3 | ⛔ ShellHost 是**原生、非 .NET** 进程。要让托管代码在里面跑，必须把 CLR 运行时（`hostfxr`/`coreclr`）也塞进去 | 把整个 .NET 运行时塞进 shell 宿主进程：启动延迟、崩溃面、版本耦合全都不可接受 |

⇒ **结论（本项目的架构决定）**：

```text
托管侧（C#）：所有"控制面" —— 进程监视、注入、配置、日志、IPC、UI、生命周期
原生侧（C++）：只有两段必须 in-proc 的代码 —— Launcher（C1）与 TAP（C2）
```

原生侧的代码量很小（PoC 实测各约 200 / 630 行），而且**已经跑通**，可直接从 `docs/poc/` 移植。

**被否决的两个"纯 C#"方案**（记录在此，避免以后有人重新提）：

| 方案 | 为什么否决 |
|---|---|
| NativeAOT 把 TAP 编成本机 DLL（无 CLR 依赖，理论可行） | 要手写 `DllGetClassObject`、`IClassFactory`、`IUnknown`、`IVisualTreeServiceCallback` 的**裸 vtable** 与 ABI 布局。NativeAOT 的 COM 支持是 `ComWrappers` 路线，跟"手搓 vtable"不搭。实现/调试成本远高于直接写 C++ |
| CLR hosting（`hostfxr` + `nethost`）把 .NET 运行时载入 ShellHost | 等于让 shell 宿主进程多背一整套运行时：冷启动变慢、GC/线程池与 shell 争抢、.NET 版本升级影响 shell 稳定性。**在别人的进程里塞运行时是不可接受的风险** |

### 1.3 进程与线程模型

```text
┌─ 用户会话 (Session N) ──────────────────────────────────────────────────────────┐
│                                                                                │
│  ┌─ VmExt.App.exe（C#，Medium IL，我们自己的进程）─────────────────────────┐   │
│  │  主线程      : WinForms 消息循环（托盘 + 设置窗 + 接收 TAP 通知的隐藏窗）│   │
│  │  Watcher 线程: 监视 ShellHost.exe 生命周期（阻塞在进程句柄上，0 CPU）   │   │
│  │  注入线程    : 只在 Watcher 触发时短暂工作                              │   │
│  │  IPC 线程池  : 命名管道服务端，接收 TAP 的点击事件                      │   │
│  │  健康线程    : 每 30s 校验一次（TAP 心跳 / ShellHost 身份），0 CPU 量级  │   │
│  └────────────┬──────────────────────────────────────────────────────────┘   │
│               │ CreateRemoteThread(LoadLibraryW("VmExt.Launcher.dll"))        │
│               ▼                                                                │
│  ┌─ ShellHost.exe（原生，Medium IL，sihost.exe 的子进程）──────────────────┐   │
│  │                                                                        │   │
│  │  [注入的] VmExt.Launcher.dll                                           │   │
│  │    DllMain → CreateThread(Worker)                                      │   │
│  │    Worker 线程: 等 Windows.UI.Xaml.dll → GetProcAddress →               │   │
│  │                 InitializeXamlDiagnosticsEx(..., initData)             │   │
│  │                                                                        │   │
│  │  [被 XAML core 加载的] VmExt.Tap.dll                                   │   │
│  │    DllMain → 日志 + 读配置                                             │   │
│  │    IObjectWithSite::SetSite  → QI IXamlDiagnostics / IVisualTreeService│   │
│  │    AdviseVisualTreeChange    → 之后由 XAML core **在 UI 线程**回调:    │   │
│  │       OnVisualTreeChange(...)  → 命中 Footer → 改树加按钮             │   │
│  │                                                                        │   │
│  │  [面板的] ControlCenterWindow (band=4) → 子窗口 InputSite (XAML 根)     │   │
│  └────────────────────────────────────────────────────────────────────────┘   │
└────────────────────────────────────────────────────────────────────────────────┘
```

**线程铁律**（违反会崩 shell）：

| 规则 | 原因 |
|---|---|
| `OnVisualTreeChange` 回调运行在 **ShellHost 的 XAML UI 线程**（✅ 实测 tid 恒定）。任何改 XAML 的操作必须在这个线程上 | XAML 对象有线程亲和性 |
| Launcher 的 `Worker` 是**独立线程**（不是 DllMain 线程） | DllMain 里做重活会死锁在 loader lock |
| 改 XAML 必须在**布局完成之后**（`ActualWidth > 0`），否则拿到 0/NaN 并静默错位 | ✅ 实测踩坑：算出 354px 离谱边距 |
| 需要延后执行时用 `CoreDispatcher::RunAsync`，不要 `Sleep` | UI 线程不能阻塞 |
| TAP 的点击动作**不得阻塞 UI 线程**（用 `PostMessage` 或独立线程） | 否则面板卡死 |

### 1.4 组件清单与依赖矩阵

| 模块 | 所在层 | 进程 | 依赖 | 被谁调用 |
|---|---|---|---|---|
| `VmExt.Core.Constants` | 托管 | App | 无 | 所有托管模块 + 生成原生配置 |
| `VmExt.Core.Logging` | 托管 | App | 无（自写） | 所有托管模块 |
| `VmExt.Core.ConfigStore` | 托管 | App | System.Text.Json | App / Service |
| `VmExt.Core.NativeMethods` | 托管 | App | 无（P/Invoke） | Interop 层 |
| `VmExt.Interop.ShellHostLocator` | 托管 | App | NativeMethods | Watcher / Injector / Health |
| `VmExt.Interop.ShellHostWatcher` | 托管 | App | Locator | Orchestrator |
| `VmExt.Interop.RemoteInjector` | 托管 | App | NativeMethods, Locator | Orchestrator |
| `VmExt.Interop.DiagnosticsRuntimeLocator` | 托管 | App | 无（文件系统） | Orchestrator |
| `VmExt.Interop.TapProbe` | 托管 | App | NativeMethods | Orchestrator / Health |
| `VmExt.Service.InjectionOrchestrator` | 托管 | App | 上面全部 | App |
| `VmExt.Service.HealthMonitor` | 托管 | App | TapProbe, Locator | App |
| `VmExt.Service.EntryActionServer` | 托管 | App | NativeMethods（管道 API 可托管封装） | App |
| `VmExt.App.TrayHost` / `SettingsForm` | 托管 | App | Core/Service | 用户 |
| `VmExt.Launcher.dll` | 原生 | **ShellHost** | `Windows.UI.Xaml.dll`（目标内） | 注入后自启线程 |
| `VmExt.Tap.dll` | 原生 | **ShellHost** | cppwinrt 头（仅头文件，链接 `WindowsApp.lib`） | XAML core（`DllGetClassObject`） |

### 1.5 一次完整生命周期的时序

```text
App 启动
  │
  ├─ 读配置 appsettings.json / 生成 vmext-tap.ini
  ├─ DiagnosticsRuntimeLocator.Locate()        ← 找不到 xamldiagnostics.dll 就报错并停在"未就绪"
  ├─ EntryActionServer.Start()                 ← 起命名管道服务端（TAP 点击会写进来）
  ├─ ShellHostWatcher.Start()                  ← 拿到当前 ShellHost（若有）并挂等待
  │
  ▼  （ShellHost 存在）
InjectionOrchestrator.TryInject(shellHost)
  ├─ TapProbe.IsTapAlive(pid)?  ── 是 ─→ 跳过（幂等，防重复注入）
  ├─ RemoteInjector.Inject(pid, "VmExt.Launcher.dll")         ← 写远端路径 + CreateRemoteThread
  └─ 轮询 TapProbe 直到 TAP 心跳出现（超时 10s → 记失败）
  │
  ▼  （用户按 Win+Ctrl+V 或点任务栏音量）
ShellHost 创建声音输出页 XAML 树
  └─ XAML core 回调 Tap.OnVisualTreeChange(Add, Name="Footer")
       └─ InjectIntoRow()：包 Grid + 建按钮 + 复制样式 + 绑 Click
  │
  ▼  （用户点按钮）
Click → 依配置：
      action=exec → BuildCommandLine(exe, args) → CreateProcessW(exe, cmd, …, exe 所在目录)
      action=pipe → 写 \\.\pipe\VmExt.Tap.<session> 一行 "CLICK <entryId>"
        └─ App 的 EntryActionServer 收到 → 执行动作 / 显示 UI
  ▼
（用户关闭面板）→ XAML 树销毁 → 按钮随之消失（无残留）
（用户再次打开面板）→ 重新走上面的回调 → 若 enabled=0 则什么都不做
```

### 1.6 ShellHost 生命周期与重注入（产品必须处理）

✅ 实测：`ShellHost.exe` 的父进程是 **`sihost.exe`**，不是 explorer。它会随 shell 重启（explorer 重启 / sihost 重启 / 自身崩溃）而换成**新进程**。

```text
loop:
    1. 等 ShellHost.exe 出现（可同时存在多个候选 → 用 session 过滤）
    2. 对每个候选：TapProbe.IsTapAlive(pid)?
         是 → 跳过
         否 → RemoteInjector.Inject  → 等 TAP 心跳
    3. WaitForSingleObject(进程句柄)   ← 阻塞，0 CPU
    4. 进程退出 → 回到 1
```

**关键点**：TAP 活在 ShellHost 里，ShellHost 一换进程 TAP 就没了。所以产品要的不是"重启 explorer"，而是上面这个**重注入循环**。

---

## 2. 跨进程契约（Interface Contract）

这一节的每一条都是**托管侧与原生侧之间的接口**，改动必须双侧同步，并且要升版本号。

### 2.1 常量全表

| 常量 | 值 | 谁用 | 可变性 |
|---|---|---|---|
| TAP CLSID | `{A7C5F1E2-9B34-4D6E-8F21-5C0D3E7A9B44}` ✅（PoC 已验证可用） | Launcher 传入 + TAP 返回 | ⛔ 改动需同时改两侧二进制 |
| 诊断端点名前缀 | `VisualDiagConnection` ✅（OS 约定，不可改） | Launcher 拼接 | ⛔ 固定 |
| Launcher DLL 名 | `VmExt.Launcher.dll` | Injector 注入 | 可改（配置项） |
| TAP DLL 名 | `VmExt.Tap.dll` | Launcher 传入 | 可改（配置项） |
| 管道名 | `\\.\pipe\VmExt.Tap.S<sessionId>` | TAP 客户端 / App 服务端 | 可改（随**配置串**传） |
| 单例互斥体名 | `Local\VmExt.Tap.Singleton.<pid>` | TAP 自检（防重复注入） | 可改（随**配置串**传） |
| **TAP 导出名** | `VmExtTapProvideInitData` ✅（PoC 已验证可用） | Launcher 直投配置 | ⛔ 改动需同时改两侧（§2.2.1） |
| **initData 硬上限** | `259` 字符（实测值） | `ConfigStore.BuildInitData` 自检 + 告警 | ⛔ OS 行为，不可改（§2.2.1） |
| 诊断运行时 DLL 名 | `xamldiagnostics.dll` | DiagnosticsRuntimeLocator 探测 | ⛔ 固定 |
| 目标进程名 | `ShellHost.exe` | Locator | ⚠️ 理论上可能变（见 §10.2） |
| 目标元素名 | `Footer` | TAP 匹配 | ⚠️ 软依赖（见 §10.2） |
| 点击消息号（若用窗口消息模式） | `WM_APP + 0x2100` | TAP → App | 可改 |
| 日志目录 | `%LOCALAPPDATA%\VolumeMixerExtender\logs` | 双方 | 可改 |

### 2.2 配置下发（两条通道，主次分明）

> ✅ **本节已在 PoC 上实测完毕**（T1）。完整长度扫描表、BSTR 所有权、端到端链路验证见
> `verified-after-injection/06-initdata-channel-limit.md`。下面写的是**实测之后的定稿设计**。

| 通道 | 角色 | 容量 | 实测结论 |
|---|---|---|---|
| **直投**（Launcher 直接调 TAP 的导出） | ★ **配置主干** | **无限制** | 已验证 **4000 字符逐字符无损** |
| `InitializeXamlDiagnosticsEx` 第 6 参数 `wszInitializationData` | **面包屑**（人类可读，便于事后从日志核对"本该是什么"） | ⚠️ **正好 259 字符** | ≤259 逐字符一致（含中文）；≥260 **静默返回空串** |
| initData（直投失败时的兜底） | 降级配置来源 | 259 字符 | 已实现并显式记日志 |

#### 2.2.1 直投通道（主通道）★

`InitializeXamlDiagnosticsEx` 的第 6 个参数**确实**能被 `IXamlDiagnostics::GetInitializationData()`
原样读回 —— 但**上限正好 259 字符**，而且超限时是**静默失败**：`InitializeXamlDiagnosticsEx`
返回 `S_OK`，`GetInitializationData` 也返回 `S_OK`，只是拿到的 BSTR 长度为 0。
一个 `cfg=<完整安装路径>` 就可能上百字符，259 装不下真实配置，而**上限不可协商**。

所以配置走"直投"：**Launcher 与 TAP 在同一个进程里**，Launcher 直接 `LoadLibraryW` 加载 TAP DLL，
`GetProcAddress` 拿到导出的函数指针，把配置字符串交给它：

```cpp
// ① 直投（必须在 InitializeXamlDiagnosticsEx 之前）
HMODULE hTap = LoadLibraryW(tapDllPath);                 // 与 XAML core 之后加载的是同一个模块
auto provide = (void (WINAPI*)(const wchar_t*))
               GetProcAddress(hTap, "VmExtTapProvideInitData");
if (provide) provide(config.c_str());                    // ← TAP 的全局变量就位
// ★ 刻意**不** FreeLibrary：多持一个引用确保模块不被卸载，全局变量不会随卸载丢失

// ② 再发起注入（initData 只作面包屑）
HRESULT hr = InitializeXamlDiagnosticsEx(endpoint, GetCurrentProcessId(),
                                         xamldiag, tap, CLSID_VcxTap, config.c_str());
```

**为什么成立**：先 `LoadLibraryW` 时模块已被加载、全局变量可写；XAML core 之后加载同一模块时
拿到的是同一个 `HMODULE`（`LoadLibrary` 引用计数），所以 TAP 的 `DllGetClassObject` 被调用时，
配置**已经就位**。

**跨进程契约新增项**（这是本项目"契约"的一部分，两侧必须一致）：

| 项 | 值 | 备注 |
|---|---|---|
| 导出名 | `VmExtTapProvideInitData` | 定稿，两侧都不可单方面改 |
| 签名 | `void WINAPI VmExtTapProvideInitData(const wchar_t*)` | 不返回错误码：失败通过**日志**暴露 |
| 调用时机 | **先直投，后 `InitializeXamlDiagnosticsEx`** | 顺序反了配置就丢了 |
| 生命周期 | 调用方**不得** `FreeLibrary` 该 DLL | 否则全局变量随卸载丢失 |
| 入参 | 见 §2.2.2 的 `k=v;` 串 | 与 initData **同一个字符串** |

> **为什么"同一个字符串"很重要**：两条通道内容一致，才能拿日志里的哈希直接对比 ——
> 这是"配置有没有被改坏"的最省事自检。PoC 就是这么验的（两侧打印 `fnv1a64`，要求逐位相同）。

实现要点（原生侧）：配置存进一个 `std::wstring` 全局变量 + 一把 `CRITICAL_SECTION`；
`ExportedProvide` 里进入临界区写入，`DllGetClassObject` 之前用 `ProvidedConfig()` 取一份副本。
`DllGetClassObject` 可能被调用多次，**取值要做成幂等的读操作**。

#### 2.2.2 载荷格式

**格式**：单行、UTF-16、`k=v` 用 `;` 分隔。**刻意不用 JSON** —— 原生侧要手写解析器毫无价值，而 `k=v` 用 `wcstok_s` + `wcsncmp` 十行就能搞定。

```text
ver=1;cfg=C:\Users\eric\AppData\Local\VolumeMixerExtender\config\vmext-tap.ini;log=C:\...\logs\tap.log;pipe=\\.\pipe\VmExt.Tap.S2;mutex=Local\VmExt.Tap.Singleton.17080
```

| 键 | 必需 | 类型 | 说明 | 缺省行为 |
|---|---|---|---|---|
| `ver` | 是 | int | 协议版本。TAP 不认识的版本 → 静默不注入（只打日志） | 视为 1 |
| `cfg` | 是 | 绝对路径 | TAP 配置 INI。**每次注入前重新读**，实现热重载 | 相对路径按 TAP DLL 所在目录解析 |
| `log` | 否 | 绝对路径 | TAP 日志文件。缺省为 TAP DLL 同目录 `tap.log` | 同目录 `tap.log` |
| `pipe` | 否 | 管道名 | 有则 `action=pipe` 可用 | 无（用 `exec` 的条目会失败并记日志） |
| `mutex` | 否 | 内核对象名 | 单例互斥体。缺省用 `Local\VmExt.Tap.Singleton.<pid>` | 自动拼 |

> ★ **`cfg` 必须由通道自己传**。若"配置里说配置在哪"就形成循环依赖了 ——
> 所以 `cfg` 是**唯一必须由通道送达**的键：TAP 先拿到 `cfg`，其余全部从那个 INI 读。
> 这样配置文件**放哪都行**（安装目录、`%LOCALAPPDATA%`、U 盘），不必与 DLL 同目录。

**约束与陷阱**：

1. ⛔ **initData 通道的硬上限是 259 字符**（实测值，见 §2.2.1）。`BuildInitData` 必须做长度校验：
   超限时**仍然照传**（功能不受影响，配置由直投通道送达），但要打**警告日志**说明"面包屑会被丢弃",
   否则将来排查时会误以为"配置没送过去"。
2. ⛔ **不要用返回值判断 initData 是否送达** —— 超限时 `hr` 仍是 `S_OK`。
   判断"配置到了没有"只能看 TAP 侧的日志（`直投通道: 有/无`）。
3. ⚠️ 分隔符 `;` 与 `=` **不得出现在值里**（路径里一般不会）。若必须，改为 URL 编码 —— 但 v1 直接禁止。
   安装路径含这类字符时 `BuildInitData` 的 `EnsureSafe` 应在启动自检阶段就抛错（§1.9 已列）。
4. 该字符串由 Launcher 构造；Launcher 自身怎么知道这些值？→ 通过 **`VmExt.Launcher.ini`**（注入前由 C# 写在 Launcher DLL 同目录）。这样注入参数只有 DLL 路径一项，简单且可调试。
5. ⚠️ **C# 轨道特有的坑**：直投靠的是"导出名 + 签名"这个**字符串契约**，编译器帮不上忙 ——
   导出名写错、签名写错、忘了 `extern "C"`（导致名字被 C++ 修饰）都不会有编译错误，
   只会运行时 `GetProcAddress` 返回 `NULL`。所以：**约定必须只在一处定义**（C# 侧的常量类 vs
   原生侧的 `#define`），并且单元测试里要断言"导出的符号名与约定的字符串逐字符相同"。
   （C++ 轨道可以用共享头文件把这件事变成编译期保证 —— 这是 §1.0 选型对照里 C++ 的一项优势。）
6. ⚠️ 直投发生在**注入器自己**的线程里。`InitializeXamlDiagnosticsEx` 会加载模块并同步遍历 XAML 树，
   不要在中间插入耗时操作；`provide()` 必须是**快速写全局变量**，不要在里面读文件/开日志文件。

### 2.3 TAP 配置 `vmext-tap.ini`

放在 `config\`，**C# 写、原生读**，用 `GetPrivateProfileStringW` 读（零解析代码）。**每次命中 `Footer` 时重新读** ⇒ 改配置后**下次打开面板生效，无需重新注入**。

```ini
[vmext]
; 1 = 注入按钮；0 = 什么都不做（"反注入"的开关）
enabled=1

; ---- 条目 1（v1 只支持 1 个，schema 预留 N 个）----
entry1.id=volumemixer
entry1.text=音量合成器
; exec = TAP 直接 CreateProcessW；pipe = 通知 C# 由 C# 决定
entry1.action=pipe
; ★ action=exec 时用这两项：exe 是**真实 exe 的绝对路径**（不是 .lnk），args 可选。
;   刻意**不提供**"一整条命令行"的写法 —— 用户填一整条命令行就有人会写成
;   不加引号（会被前缀试探，可能启动错的程序）或写成 .lnk（err=193）。拆开后由产品加引号，用户无从写错。
entry1.exe=
entry1.args=
; 右侧内边距（像素）。实测：底栏左边距也是 4，所以默认 4 对称
entry1.inset=4
; 按钮高度。0 = 跟随模型按钮的显式 Height（推荐，见 §5.3）
entry1.height=0
```

**逐字段与本项目实测事实的对应**：

| 字段 | 依据 |
|---|---|
| `enabled` | 反注入开关。关闭后 TAP 仍然常驻但不再改树（⛔ TAP 无法卸载，见 §10.1） |
| `entry1.text` | 注入后 UIA 读到的 `Name` 就是它（PoC 用 `TestLink`） |
| `entry1.action=exec` | PoC 用 `CreateProcessW(L"winver.exe")` ✅ 已实测能拉起进程 |
| `entry1.action=pipe` | ⚠️ 设计新增，需 V4 验证 |
| `entry1.inset=4` | ✅ 实测：底栏 x=2189，模型按钮 x=2193 → 左边距 4；右边距取 4 后 `TestLink` 右边缘 2543 = 2547-4 |
| `entry1.height=0` | ✅ 实测：模型按钮 `Height` 是显式 `40.0`、`MinHeight=0.0`，直接读即可定高，不必测量 |

> ⛔ **`entry1.exe` 不能指向快捷方式（`.lnk`）** —— ✅ 已用本机工具链实测（`lnkprobe.cpp`）：
>
> ```text
> CreateProcessW(L"\"C:\...\x.lnk\"")   => FAILED  GetLastError=193  (ERROR_BAD_EXE_FORMAT)
> CreateProcessW(L"\"C:\Windows\System32\cmd.exe\"")  => OK  pid=16852     ← 对照
> ```
>
> 快捷方式的解析（目标重定向、参数、工作目录、环境变量）是 **shell** 的职责，`CreateProcessW` 不做这件事。
> 所以"启动什么"有三种可用形态（都经由 `entry1.exe` / `entry1.args` 表达）：
>
> | 形态 | 写法 | 适用 |
> |---|---|---|
> | 直接 exe | `entry1.exe=C:\...\VolumeMixerExtender.exe` | ★ 首选（自己的程序） |
> | ★ 交给 shell 解析 | `entry1.exe=C:\Windows\explorer.exe` + `entry1.args=ms-settings:apps-volume` | 需要 shell 参与（URI 协议、打开文件夹） |
> | ★★ 目标只能以快捷方式表达 | `entry1.exe=C:\Windows\explorer.exe` + `entry1.args="C:\...\某个.lnk"` | Store 应用等（`.lnk` 不能直接当 exe） |
>
> ⇒ **`action=exec` 不会自动获得"双击快捷方式"的效果。** 需要那种效果就必须显式走 `explorer.exe`。

> ⚠️ **一条容易踩的坑：exe 路径必须加引号。**
> `CreateProcessW` 在 `lpApplicationName = NULL` 时会对**未加引号的含空格命令行逐段前缀试探**，
> 前缀位置存在同名 exe 就启动那个。✅ 已复现：
>
> ```text
> 命令行（不加引号）: C:\...\Temp\a b c\click probe.exe
>   前缀处放了 C:\...\Temp\a.exe  ->  实际启动的是那个（argv[0] = ...\Temp\a）
>   前缀处没有文件               ->  才落到 ...\a b c\click probe.exe
> ```
>
> 即"平时能跑，前缀处一旦存在同名文件就会静默启动别的程序"。
> ⚠️ 这是**正确性**问题（启动错程序），不是安全问题 —— TAP 是同用户上下文，
> 能在前缀处放文件的人本来就能以你的身份执行代码，没有权限边界被跨越（决策记录见
> `verified-after-injection/07-click-execution-constraints.md` §5）。
>
> **⬆ 这也是上面配置 schema 把 `command` 拆成 `exe` + `args` 的原因**：
> 让用户填一整条命令行、还得记得加引号，这种设计本身就是 bug 的温床。
> 正确的写法（PoC 已按此改并回归通过）：
>
> ```cpp
> std::wstring cmdline = BuildCommandLine(e.exe, e.args);   // 内部会给 e.exe 加引号
> std::vector<wchar_t> cmd(cmdline.begin(), cmdline.end()); cmd.push_back(L'\0');
> CreateProcessW(e.exe.c_str(),        // ★ 显式 lpApplicationName，杜绝前缀试探
>                cmd.data(),           // ★ 可写缓冲
>                nullptr, nullptr, FALSE, 0, nullptr,
>                nullptr,              // 工作目录：留给被启动的程序自己处理（见下方 📌）
>                &si, &pi);
> ```
>
> 另外两条实测约束：
> * ⛔ **不要在点击处理里等子进程**（`WaitForSingleObject`）—— 那会阻塞 ShellHost 的线程，点一下卡整个任务栏。
>
> 📌 **工作目录不属于本设计的规定范围**：`lpCurrentDirectory` 传 `nullptr`，子进程的工作目录
> 由系统继承（实测会是 `C:\Windows\System32`）。**被启动的程序应当自己按模块路径
> （`GetModuleFileNameW`）解析自己的配置/资源，而不是依赖工作目录** —— 这是接入程序自己的事，
> 交付文档不做规定；本模块只负责"把这条命令行按原样启动"。

### 2.4 IPC 协议（`action=pipe` 模式）

| 项 | 值 |
|---|---|
| 传输 | 命名管道，**消息模式**（`PIPE_TYPE_MESSAGE`）还是字节模式？→ **字节模式 + `\n` 分行**，最简单 |
| 方向 | **单向**：TAP → App。App 不回包（避免 TAP 等回复而阻塞 UI 线程） |
| 服务端 | C# `NamedPipeServerStream`，`PipeDirection.In`，同时最多 1 个实例循环 Accept（TAP 的写入很短，够用） |
| 客户端 | 原生 `CreateFileW` + `WriteFile` + `CloseHandle` |
| 报文 | `CLICK <entryId> <unixMillisUtc>\n`，字段空格分隔，`entryId` 不含空格 |
| 超时 | 客户端：`WaitNamedPipeW` 超时 200ms；建管道失败就**记日志并放弃**（不重试、不弹窗） |
| 线程 | ⚠️ TAP 侧**必须**在独立线程里写（或至少保证 200ms 上限），避免卡住 XAML UI 线程 |

> ✅ **本节已实测完毕**（T3，2026-10-03）。结论：报文逐字节正确；**UI 线程实测未被拖住**
> （`Invoke` 往返 7–14 ms）；管道 IO 确实在独立线程（tid 可证）；服务端缺失时**瞬时降级**且有日志。
> 详见 `verified-after-injection/08-pipe-action-chain.md`。

**⚠️ 服务端实现的三个坑**（第一条是实测**新发现**，只写 `ERROR_PIPE_CONNECTED` 会漏）：

| 情形 | 错误码 | 正确处理 |
|---|---|---|
| 客户端已连上、仍开着 | `ERROR_PIPE_CONNECTED` (110) | ✅ 当作成功，进读循环 |
| 客户端**连上又已关闭**（TAP 写完就 `CloseHandle`） | **`ERROR_NO_DATA` (232)** | ⚠️ **不能当致命错误**：缓冲里的数据**仍可读**（实测 32 字节报文完整读回）。当错误处理 ⇒ **静默丢一次点击**，且最容易发生在"App 刚启动"的窗口里 |
| 读的时候对端关闭 | `ERROR_BROKEN_PIPE` (109) | ✅ 正常的**流结束**信号，不是错误 |

> 📌 C# 轨道的 `NamedPipeServerStream` 内部已经处理了这些（`WaitForConnectionAsync` 帮你做了
> `ConnectNamedPipe` 的语义），所以**这条坑只影响原生服务端**。但要知道它存在 ——
> 一旦将来为了性能把服务端改成原生实现，就会立刻踩上。

**为什么不用窗口消息（`PostMessage` 到 App 的隐藏窗）**：
- 优点：非阻塞，天然免疫"App 卡住"。
- 缺点：`HWND` 会在 App 重启后失效，而 TAP 无法感知；管道名反而是稳定的、可重连的。
- **结论**：v1 用管道；把窗口消息列为备选（常量表里已留消息号）。若实测发现管道写入会阻塞 UI 线程，就切到 `PostMessage`。

### 2.5 日志契约

三份日志，**都是 UTF-8/ANSI 文本、按行追加、可被别的进程同时读**：

| 文件 | 写入者 | 内容 | 关键约定 |
|---|---|---|---|
| `app-YYYYMMDD.log` | C# | 编排/注入/健康/IPC 全部事件 | 单行 `ISO8601 \| LEVEL \| thread \| tag \| msg` |
| `launcher.log` | 原生 Launcher | 启动 → 等模块 → 解析导出 → 逐端点尝试 → 结果 | 单行 `[HH:mm:ss.fff tid=N] msg` |
| `tap.log` | 原生 TAP | 加载 → SetSite → 命中 Footer → 注入步骤 → 点击 | 同上 |

**三条硬性日志约定**（这三条是排障体验的关键，源自 PoC 的踩坑）：

1. ⛔ **必须用 `CreateFileW(FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, ...)` 写原生日志**，不要用 CRT 的 `FILE*`（`ccs=UTF-8` 的流在本机实测会静默只写一个 BOM，全部日志丢失）。`FILE_SHARE_READ|WRITE` 是让 App 能在 TAP 持有句柄时仍然读到日志。
2. **写日志必须持锁**（`CRITICAL_SECTION`）。TAP 的日志会被多个线程写。
3. **日志只追加、不轮转**（在 TAP 内轮转会造成复杂性与丢失风险）。量级很小（每次开面板几十行）。App 侧可在启动时按大小截断。

### 2.6 会话与完整性级别契约

| 检查项 | 要求 | 依据 |
|---|---|---|
| 会话 | App 与 ShellHost 的 `SessionId` **必须相同**（`ProcessIdToSessionId`） | 跨会话注入无意义且通常失败 |
| 完整性级别 | 两侧都是 Medium IL | ✅ 实测：`OpenProcess(ALL_ACCESS)` 成功 |
| 保护进程 | ShellHost **不是** PPL | ✅ 实测 |
| 架构 | App 与 ShellHost 位宽一致（这里都是 x64） | 跨架构注入不可能 |
| UAC | **不需要提权**。全部装到 `%LOCALAPPDATA%`，全部 per-user | 设计决定，避免 elevation |

⛔ 一旦将来 ShellHost 变成 PPL 或换到更高 IL，本方案整体失效 —— 见 §10.2 的监控项。

---

## 3. 托管侧（C#）功能模块

### 3.0 解决方案与项目结构

```
VolumeMixerExtender.sln
├─ src/
│  ├─ VmExt.Core/            net8.0-windows  类库   （无 UI、无第三方依赖）
│  │     Constants.cs  Logging.cs  ConfigStore.cs  NativeMethods.cs
│  ├─ VmExt.Interop/         net8.0-windows  类库   （只依赖 Core）
│  │     ShellHostLocator.cs  ShellHostWatcher.cs  RemoteInjector.cs
│  │     DiagnosticsRuntimeLocator.cs  TapProbe.cs
│  ├─ VmExt.Service/         net8.0-windows  类库   （只依赖 Core+Interop）
│  │     InjectionOrchestrator.cs  HealthMonitor.cs  EntryActionServer.cs
│  └─ VmExt.App/             net8.0-windows  WinExe  （托盘 + 设置窗）
│        Program.cs  TrayHost.cs  SettingsForm.cs  appsettings.json
├─ native/
│  ├─ VmExt.Launcher/        vcxproj (C++17, DLL, /MT)
│  │     launcher.cpp
│  └─ VmExt.Tap/             vcxproj (C++17, DLL, /MT)
│        tap.cpp  tap.def（或 /EXPORT:）
└─ tests/
   └─ VmExt.Tests/           net8.0        xUnit（Core 与纯逻辑部分）
```

**项目级约定**：

| 项 | 值 | 理由 |
|---|---|---|
| TFM | `net8.0-windows` | 本机已装 SDK 8.0.425（LTS）。`net10.0-windows` 也可，但 LTS 更稳 |
| `<Nullable>enable` | 开 | 本模块大量"可能拿不到目标"的返回值，用可空类型表达 |
| `<Platforms>x64</Platforms>` | 只 x64 | 与 ShellHost 位宽一致，避免跨架构注入问题 |
| `<AllowUnsafeBlocks>` | 关 | 全部 P/Invoke 可用 `IntPtr`/`Marshal` 表达，不需要 unsafe |
| 第三方依赖 | **零** | 托盘用 `System.Windows.Forms`（随 SDK 自带），JSON 用 `System.Text.Json` |
| 原生部分 | `/MT` 静态 CRT | 免去装 VC++ Redistributable 的前置要求 |
| 目标框架前置 | 需要 .NET 8 Desktop Runtime | 或 publish self-contained（文档推荐后者，见 §7.2） |

---

### 3.1 `VmExt.Core.Constants`

**职责**：把 §2.1 的常量表变成代码，**只此一处**。
**不负责**：不做任何 IO、不做校验（另见 §3.3）。

```csharp
namespace VmExt.Core;

public static class Constants
{
    // ===== in-proc 契约（改动必须同步原生侧二进制）=====
    public static readonly Guid TapClsid = new("A7C5F1E2-9B34-4D6E-8F21-5C0D3E7A9B44");
    public const string DiagEndPointPrefix = "VisualDiagConnection";
    public const string TargetProcessName      = "ShellHost.exe";
    public const string TargetProcessNameNoExt = "ShellHost";
    public const string DiagnosticsRuntimeDll  = "xamldiagnostics.dll";

    // ===== 随安装位置变化的（由用户配置覆盖）=====
    public const string LauncherDllName = "VmExt.Launcher.dll";
    public const string TapDllName      = "VmExt.Tap.dll";
    public const string LauncherIniName = "VmExt.Launcher.ini";
    public const string TapIniName      = "vmext-tap.ini";

    // ===== 运行时拼出来的名字 =====
    public static string PipeName(int sessionId)  => $@"\\.\pipe\VmExt.Tap.S{sessionId}";
    public static string TapMutexName(int pid)    => $@"Local\VmExt.Tap.Singleton.{pid}";
    public static string EndPoint(int index)      => $"{DiagEndPointPrefix}{index}";

    // ===== 时间常量（全部有出处，不要随手改）=====
    public static readonly TimeSpan InjectWaitTimeout   = TimeSpan.FromSeconds(20); // PoC 用 20s ✅
    public static readonly TimeSpan TapAliveTimeout     = TimeSpan.FromSeconds(10); // 注入后等 TAP 自报
    public static readonly TimeSpan InitialScanTimeout  = TimeSpan.FromSeconds(30); // Launcher 等 Xaml.dll
    public static readonly TimeSpan HealthInterval      = TimeSpan.FromSeconds(30);
    public static readonly TimeSpan NewProcessPollGap   = TimeSpan.FromMilliseconds(500);

    // ===== 协议版本 =====
    public const int InitDataVersion = 1;

    // ===== 配置下发（§2.2）=====
    /// <summary>TAP 的配置直投导出名。⛔ 与原生侧必须逐字符一致（无编译期检查）。</summary>
    public const string TapProvideInitDataExport = "VmExtTapProvideInitData";

    /// <summary>
    /// initData 通道的实测硬上限（字符数）。超过它，OS 会**静默**丢弃整个串：
    /// InitializeXamlDiagnosticsEx 与 GetInitializationData 都返回 S_OK，只是拿到空串。
    /// 超限不是错误（配置走直投通道），但必须打警告，否则排查时会被误导。
    /// </summary>
    public const int InitDataHardLimit = 259;
}
```

**注意**：`TapClsid` 在原生侧是硬编码的 `static const CLSID`，**不是**从注册表读的（✅ 实测：TAP 由 XAML core 直接 `LoadLibrary` + `DllGetClassObject`，**无需注册表注册**）。所以这个 GUID 只要求两侧字面量一致。

---

### 3.2 `VmExt.Core.Logging`

**职责**：给托管侧一个零依赖、可并发、可被外部查看的按天日志。
**不负责**：不读原生日志（原生日志由排障工具直接看，见 §9.3）。

```csharp
public enum LogLevel { Trace, Debug, Info, Warn, Error, Fatal }

public interface ILogSink
{
    void Write(LogLevel level, string tag, string message, Exception? ex = null);
}

public sealed class FileLogSink : ILogSink, IDisposable
{
    public FileLogSink(string directory, string filePrefix, LogLevel minimum);
    public void Write(LogLevel level, string tag, string message, Exception? ex = null);
    public void Dispose();
}

public static class Log
{
    public static ILogSink Sink { get; set; }        // 默认 NullLogSink
    public static void Info (string tag, string msg);
    public static void Warn (string tag, string msg);
    public static void Error(string tag, string msg, Exception? ex = null);
}
```

| 方法 | 参数 | 返回 | 异常 | 线程 | 备注 |
|---|---|---|---|---|---|
| `FileLogSink..ctor` | `directory` 创建目录；`filePrefix` 文件名前缀；`minimum` 过滤级别 | — | `IOException`（目录不可建）→ 调用方降级为 `NullLogSink` | 任意 | 内部用 `FileStream(FileShare.ReadWrite)` |
| `Write` | 见签名 | void | **不抛**（写失败吞掉并置内部故障标志） | **线程安全**（内部 lock） | 日志永不因 IO 失败影响主流程 |
| `Log.Info/Warn/Error` | — | void | 不抛 | 任意 | 转发到 `Sink` |

**实现要点**

1. **写失败绝不抛**。日志模块是观测设施，不能成为故障源。内部记一个 `_faulted` 标志，失败后降级为静默。
2. 文件名 `app-{yyyyMMdd}.log`；启动时若当天文件 > 8 MB 则重命名为 `.1` 后新建（**只在 App 侧轮转**，原生侧不轮转，见 §2.5）。
3. 格式（固定列，便于 grep）：
   `2026-10-03T01:45:24.456+08:00 | INFO  | t12 | Orchestrator | 已注入 ShellHost pid=17080`
4. **不要把日志写进 `%TEMP%`**（会被清理工具删掉，排障时最需要它）。

---

### 3.3 `VmExt.Core.ConfigStore`

**职责**：读 `appsettings.json`、**生成** `vmext-tap.ini` 与 `VmExt.Launcher.ini`。
**不负责**：不改 Shell 任何设置；不写注册表。

```csharp
public sealed record AppSettings
{
    public bool     Enabled          { get; init; } = true;   // 总开关
    public string?  DiagnosticsDll   { get; init; }            // null = 自动探测（§3.8）
    public string   InstallRoot      { get; init; } = DefaultInstallRoot();
    public string   NativeSubdir     { get; init; } = "native";
    public string   ConfigSubdir     { get; init; } = "config";
    public string   LogSubdir        { get; init; } = "logs";
    public LogLevel MinimumLogLevel  { get; init; } = LogLevel.Info;
    public List<EntryDefinition> Entries { get; init; } = new();
    public static string DefaultInstallRoot() =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                     "VolumeMixerExtender");
}

public sealed record EntryDefinition
{
    public string Id      { get; init; } = "entry1";
    public string Text    { get; init; } = "音量合成器";
    public EntryAction Action { get; init; } = EntryAction.Pipe;
    public string? Exe { get; init; }            // Action=Exec 时必填：真实 exe 的绝对路径（⛔ 不是 .lnk）
    public string? Args { get; init; }           // Action=Exec 时可选：附加参数（原样拼在 exe 之后）
    public int Inset      { get; init; } = 4;    // 像素
    public int Height     { get; init; } = 0;    // 0 = 读模型按钮的显式 Height
}

public enum EntryAction { Exec, Pipe }

public static class ConfigStore
{
    public static AppSettings       LoadAppSettings(string jsonPath);
    public static void              SaveAppSettings(string jsonPath, AppSettings s);
    public static string            BuildInitData(AppSettings s, int sessionId, int shellHostPid, string tapIniPath, string tapLogPath);
    public static void              WriteTapIni(string tapIniPath, AppSettings s);
    public static void              WriteLauncherIni(string launcherIniPath, string tapDllPath, string diagnosticsDllPath, string initData);
}
```

| 方法 | 参数 | 返回 | 异常 | 副作用 |
|---|---|---|---|---|
| `LoadAppSettings` | 路径 | `AppSettings`（文件不存在 → 默认值 + 落盘一份模板） | `JsonException`（格式错 → 抛出，由调用方展示错误并退出） | 可能创建文件 |
| `SaveAppSettings` | 路径, 值 | void | `IOException` | 覆盖写 |
| `BuildInitData` | 见签名 | §2.2 格式的 `k=v;` 串 | — | 无 |
| `WriteTapIni` | 路径, 设置 | void | `IOException` | 覆盖写。⛔ **必须用 UTF-16LE + BOM 写**（`new UnicodeEncoding(false, true)`）。⚠️ 早期这里写的是"必须用 `Encoding.UTF8` 无 BOM" —— **那是错的**：UTF-8 无 BOM 会让中文值**静默变乱码**（T2 实测）。当时的理由"否则读不到第一行"本身没错（UTF-8 **带** BOM 确实会破坏第一个节名），但结论应该是"不要用 UTF-8"，而不是"用无 BOM 的 UTF-8"。见 §4.3 坑 13 |
| `WriteLauncherIni` | 路径, 三个路径, initData | void | `IOException` | 覆盖写；**注入前必须完成**。⛔ 同样必须 **UTF-16LE + BOM** —— 原生 Launcher 也是用 `GetPrivateProfileStringW` 读它（`initdata` 里可能含中文路径） |

**`BuildInitData` 的精确实现**（这是两侧契约的唯一来源）：

```csharp
public static string BuildInitData(AppSettings s, int sessionId, int shellHostPid,
                                  string tapIniPath, string tapLogPath)
{
    // 顺序固定，便于人工核对；值里禁止出现 ';' 和 '='（见 §2.2 约束 2）
    EnsureSafe(tapIniPath); EnsureSafe(tapLogPath);
    return string.Join(';', new[]
    {
        $"ver={Constants.InitDataVersion}",
        $"cfg={tapIniPath}",
        $"log={tapLogPath}",
        $"pipe={Constants.PipeName(sessionId)}",
        $"mutex={Constants.TapMutexName(shellHostPid)}",
    });
}
private static void EnsureSafe(string v)
{
    if (v.IndexOfAny(new[] { ';', '=' }) >= 0)
        throw new ArgumentException($"路径含保留字符 ';' 或 '='，请改安装位置: {v}");
}
```

⚠️ **这里有个部署陷阱**：如果 `InstallRoot` 含 `;` 或 `=`（用户目录名里带分号），整个契约失效。所以 `EnsureSafe` 必须**在启动自检时调用**并把错误明确显示给用户，而不是等到注入失败。

**`WriteTapIni` 的实现要点**：

```csharp
public static void WriteTapIni(string path, AppSettings s)
{
    var sb = new StringBuilder();
    sb.AppendLine("[vmext]");
    sb.AppendLine(s.Enabled ? "enabled=1" : "enabled=0");
    for (int i = 0; i < s.Entries.Count; i++)
    {
        var e = s.Entries[i];
        var p = $"entry{i + 1}";
        sb.AppendLine($"{p}.id={e.Id}");
        sb.AppendLine($"{p}.text={e.Text}");
        sb.AppendLine($"{p}.action={(e.Action == EntryAction.Exec ? "exec" : "pipe")}");
        sb.AppendLine($"{p}.exe={e.Exe ?? ""}");
        sb.AppendLine($"{p}.args={e.Args ?? ""}");
        sb.AppendLine($"{p}.inset={e.Inset}");
        sb.AppendLine($"{p}.height={e.Height}");
    }

    // ★★ 必须是 UTF-16LE + BOM。**不能**用 UTF-8（带不带 BOM 都不行）——
    //    ✅ 实测（T2，见 verified-after-injection/09-ini-encoding.md）：
    //      UTF-8 无 BOM -> 键能匹配上，但中文值被 GetPrivateProfileStringW 按系统 ACP 解读
    //                     => **静默乱码**（ASCII 部分正常，所以很难发现）
    //      UTF-8 有 BOM -> BOM 字节污染节名，**连键都找不到** => 设置被静默忽略、回落到默认值
    //      UTF-16LE 无 BOM -> 被当 ANSI 读，同样乱码
    //      UTF-16LE + BOM  -> ✅ 唯一与系统区域设置无关的正确写法
    //    File.WriteAllText 会写入该编码的前导码（preamble），UnicodeEncoding(bigEndian:false,
    //    byteOrderMark:true) 的前导码就是 FF FE，所以这一行产出的正是 UTF-16LE + BOM。
    File.WriteAllText(path, sb.ToString(), new UnicodeEncoding(bigEndian: false, byteOrderMark: true));
}
```

| 约束 | 原因 |
|---|---|
| 值里不能有换行 | INI 单行语义 |
| ⛔ **写完必须验证文件头两字节是 `FF FE`** | 这是"中文会不会静默变乱码"的唯一判据。写错编码不会有任何报错，只会**静默乱码**（UTF-8 无 BOM）或**静默丢设置**（UTF-8 有 BOM） |
| ✅ **`text` 含中文时，`GetPrivateProfileStringW` + UTF-16LE+BOM 已实测正确往返** | 中文按钮文字（T2 实测：7 个中文字的码点逐位一致） |
| ⛔ **不要用 `WritePrivateProfileStringW` 往"全新文件"里写**（若将来为了减少代码而改用它） | ✅ 实测：全新文件会被写成 **ANSI（本机 GBK）**，不带 UTF-16 BOM —— 本机能读对只因 ACP=936，换区域设置就乱码。对已带 BOM 的文件它才会保持 UTF-16LE |

---

### 3.4 `VmExt.Core.NativeMethods`

**职责**：全部 P/Invoke 与只读的结构/枚举定义，集中隔离。
**不负责**：不做业务判断（如"这个 pid 是不是我们要的"）。

```csharp
internal static class NativeMethods
{
    // ---------- 句柄与权限 ----------
    [Flags] public enum ProcessAccess : uint
    {
        CreateThread       = 0x0002,
        VmOperation        = 0x0008,
        VmWrite            = 0x0020,
        VmRead             = 0x0010,
        QueryInformation   = 0x0400,
        Synchronize        = 0x00100000,
    }
    [Flags] public enum AllocationType : uint { Commit = 0x1000, Reserve = 0x2000, Release = 0x8000 }
    public enum MemoryProtection : uint { ReadWrite = 0x04 }
    public const uint MemRelease   = 0x8000;
    public const uint WaitObject0  = 0x00000000;
    public const uint WaitTimeout  = 0x00000102;
    public const uint Infinite     = 0xFFFFFFFF;
    public const uint Th32CsSnapModule = 0x00000008 | 0x00000010; // MODULE | MODULE32

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr OpenProcess(ProcessAccess access, bool inherit, int pid);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr VirtualAllocEx(IntPtr h, IntPtr addr, IntPtr size, AllocationType type, MemoryProtection prot);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool WriteProcessMemory(IntPtr h, IntPtr addr, byte[] buf, IntPtr size, out IntPtr written);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr CreateRemoteThread(IntPtr h, IntPtr sa, IntPtr stack, IntPtr start, IntPtr param, uint flags, IntPtr tid);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern uint WaitForSingleObject(IntPtr h, uint ms);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool GetExitCodeThread(IntPtr hThread, out IntPtr exitCode);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool VirtualFreeEx(IntPtr h, IntPtr addr, IntPtr size, uint freeType);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool CloseHandle(IntPtr h);

    // ---------- 模块枚举（远程 LoadLibraryW 地址的关键）----------
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr CreateToolhelp32Snapshot(uint flags, int pid);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern bool Module32FirstW(IntPtr snap, ref MODULEENTRY32W entry);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern bool Module32NextW(IntPtr snap, ref MODULEENTRY32W entry);
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct MODULEENTRY32W
    {
        public uint   dwSize;
        public uint   th32ModuleID;
        public uint   th32ProcessID;
        public uint   GlblcntUsage;
        public uint   ProccntUsage;
        public IntPtr modBaseAddr;
        public uint   modBaseSize;
        public IntPtr hModule;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string szModule;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)] public string szExePath;
    }

    // ---------- 本进程内取模块基址/导出地址 ----------
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr GetModuleHandleW(string name);
    [DllImport("kernel32.dll", CharSet = CharSet.Ansi, SetLastError = true, BestFitMapping = false)]
    public static extern IntPtr GetProcAddress(IntPtr hModule, string procName);

    // ---------- 会话 ----------
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool ProcessIdToSessionId(int pid, out int sessionId);

    // ---------- 命名对象（TAP 心跳 / 单例）----------
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr OpenMutexW(uint access, bool inherit, string name);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr OpenEventW(uint access, bool inherit, string name);
    public const uint Synchronize = 0x00100000;

    // ---------- 命名管道（客户端侧，仅排障用）----------
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern bool WaitNamedPipeW(string name, uint timeoutMs);
}
```

**`GetRemoteModuleBase` 的正确实现**（这是 §3.7 的基石，**不要图省事**）：

```csharp
/// <summary>读目标进程里某个模块的加载基址。失败返回 IntPtr.Zero。</summary>
public static IntPtr GetRemoteModuleBase(int pid, string moduleName)
{
    IntPtr snap = CreateToolhelp32Snapshot(Th32CsSnapModule, pid);
    if (snap == new IntPtr(-1)) return IntPtr.Zero;
    try
    {
        var e = new MODULEENTRY32W { dwSize = (uint)Marshal.SizeOf<MODULEENTRY32W>() };
        if (!Module32FirstW(snap, ref e)) return IntPtr.Zero;
        do
        {
            if (string.Equals(e.szModule, moduleName, StringComparison.OrdinalIgnoreCase))
                return e.modBaseAddr;
        } while (Module32NextW(snap, ref e));
        return IntPtr.Zero;
    }
    finally { CloseHandle(snap); }
}
```

> **为什么必须这样算远程 `LoadLibraryW` 地址**（这一段是踩坑总结，很多注入器示例是错的）：
>
> 常见错误写法是直接 `GetProcAddress(GetModuleHandleW("kernel32.dll"), "LoadLibraryW")` 拿到**本进程**的地址，然后把它当远程地址用。
> ASLR 原则上让每个进程的模块基址独立。实践里系统 DLL（尤其 kernel32）在同一 boot 上往往恰好处在同一基址，所以这种写法**经常能跑**——
> PoC 就是这么写的并且成功了，但这是**巧合而非保证**（换 IL、换架构、开强制 ASLR、不同 boot 都可能失效）。
>
> 正确算法：**本地算 RVA，远程加基址**。
> `remoteAddr = remoteKernel32Base + (localLoadLibraryW - localKernel32Base)`。
> 这样只依赖"同一份 kernel32 二进制在同一台机器上导出表布局一致"，这是**同架构同 boot 下成立的**。

---

### 3.5 `VmExt.Interop.ShellHostLocator`

**职责**：找到**本会话的** ShellHost 进程；给出判断依据。

```csharp
public sealed record ShellHostProcess(int Pid, int SessionId, DateTime StartTimeUtc, string? ImagePath);

public static class ShellHostLocator
{
    public static IReadOnlyList<ShellHostProcess> FindAll();          // 全部会话
    public static IReadOnlyList<ShellHostProcess> FindInCurrentSession();
    public static ShellHostProcess? FindInCurrentSession(int pid);
    public static int CurrentSessionId { get; }
}
```

| 方法 | 返回 | 异常 | 备注 |
|---|---|---|---|
| `FindAll` | 可能为空列表 | **不抛**（`Process` 查询失败逐条吞掉并记日志） | ⚠️ 一定同时存在多个（其它登录会话） |
| `FindInCurrentSession` | 同上 | 不抛 | 这是业务用的那个 |
| `FindInCurrentSession(pid)` | `null` 若不存在或不属于本会话 | 不抛 | 健康检查用 |
| `CurrentSessionId` | int | 首次访问缓存 | 用 `ProcessIdToSessionId(GetCurrentProcessId())` |

**实现要点**

1. 用 `Process.GetProcessesByName(Constants.TargetProcessNameNoExt)` —— **不要带 `.exe`**（`ProcessName` 不含扩展名，带 `.exe` 会永远查不到）。这是最容易犯的低级错误。
2. `Process.SessionId` 在跨会话时可能抛 `InvalidOperationException`（已退出）→ 逐条 try/catch。
3. `StartTimeUtc` 会抛（进程已退出/权限不足）→ 失败时给 `DateTime.MinValue`，不要让它崩。
4. **不要**只取第一个结果。实测环境里同时存在多个 `ShellHost` 的情况是正常的（多用户），必须按 session 过滤。
5. `ImagePath` 用 `Process.MainModule?.FileName`（需要 `QueryInformation` 权限；失败给 null，不是致命）。

---

### 3.6 `VmExt.Interop.ShellHostWatcher`

**职责**：长时间、**接近 0 CPU** 地监视 ShellHost 的产生与消亡，并触发注入。
**不负责**：不执行注入（那是 §3.10 的事）。

```csharp
public sealed class ShellHostWatcher : IDisposable
{
    public event Action<ShellHostProcess>? ShellHostStarted;
    public event Action<int /*pid*/, DateTime /*startedUtc*/>? ShellHostExited;

    public ShellHostWatcher(TimeSpan newProcessPollGap);   // 默认 Constants.NewProcessPollGap
    public void Start();          // 幂等：重复调用无副作用
    public void Dispose();        // 停线程并释放句柄；可重入
}
```

| 方法 | 线程 | 副作用 | 备注 |
|---|---|---|---|
| `Start` | 任意 | 起一个后台线程（`IsBackground = true`） | 内部先做一次全量扫描并**为已存在的 ShellHost 触发 `ShellHostStarted`**，这样"App 晚启动"的场景不需要额外代码路径 |
| `Dispose` | 任意 | 触发 abort 事件 → 线程退出 → 释放所有进程句柄 | 必须在 App 退出时调用，否则句柄泄漏 |

**线程循环（伪代码，这是本类全部价值所在）**

```text
handles = [ abortEvent ]
foreach shellHost in ShellHostLocator.FindInCurrentSession():
    handles += OpenProcess(Synchronize, shellHost.Pid)      // 只为等它退出
    alive[handle] = shellHost

while not aborted:
    if alive.Count > 0:
        idx = WaitForMultipleObjects(handles, timeout = 5s)     // ★ 阻塞，0 CPU
        if idx == WAIT_TIMEOUT: continue                        // 超时只是为了让循环能看 abortEvent
        if idx == 0: break                                      // abort
        victim = alive[handles[idx]]
        raise ShellHostExited(victim)
        CloseHandle(handles[idx]); remove from alive/handles
        continue
    else:
        // 没有活着的 ShellHost：低速轮询
        WaitForSingleObject(abortEvent, 500ms)
        foreach p in ShellHostLocator.FindInCurrentSession():
            if not alive.ContainsKey(p.Pid):
                h = OpenProcess(Synchronize, p.Pid)
                handles += h; alive[h] = p
                raise ShellHostStarted(p)
```

**为什么这样设计**

| 设计点 | 理由 |
|---|---|
| 用 `WaitForMultipleObjects` 等**进程句柄**而不是轮询进程列表 | ✅ 0 CPU。轮询进程列表在空闲时也会每秒几十次系统调用 |
| 分开"有活着的"和"没有"两条路径 | 有活着的进程时等句柄（高效）；一个都没有时只能轮询（此时是短暂的，ShellHost 很快会出现） |
| `timeout = 5s` 而不是 `INFINITE` | 让循环有机会检查 abort 标志（也可以用第 0 个 handle 当 abort 事件，这里两者都有，双保险） |
| 后台线程 + `IsBackground` | App 退出时不会因为该线程卡住进程 |
| 事件在 watcher 线程上触发 | **订阅者必须自己切线程**（§3.10 用 `Task.Run`/队列），不要在事件处理器里做耗时 UI 操作 |

⚠️ **`WaitForMultipleObjects` 上限 64 个句柄**。正常情况下本会话只有 1 个 ShellHost，但如果外部工具频繁重启 shell，理论上有堆积风险。实现时在 `alive.Count >= 60` 时走一次"清理已退出句柄 + 重建数组"的分支。

---

### 3.7 `VmExt.Interop.RemoteInjector`

**职责**：把一个原生 DLL 注入到目标进程。（**唯一的"侵入性"动作**）

```csharp
public enum InjectError
{
    None = 0,
    TargetGone,          // 进程已退出
    OpenProcessFailed,   // 权限/不存在的具体 Win32 错误在 Win32Error 里
    AllocFailed,
    WriteFailed,
    ModuleBaseFailed,    // 远程 kernel32 基址取不到
    RemoteResolverFailed,// 本地导出地址取不到
    ThreadCreateFailed,
    ThreadTimeout,
    RemoteLoadReturnedNull,  // 注入成功但 LoadLibraryW 返回 NULL（DLL 自身没加载起来）
}

public readonly record struct InjectionResult(
    InjectError Error, int Win32Error, IntPtr RemoteModuleHandle)
{
    public bool Ok => Error == InjectError.None;
    public static InjectionResult Fail(InjectError e, int win32 = 0) => new(e, win32, IntPtr.Zero);
}

public static class RemoteInjector
{
    public static InjectionResult Inject(
        int pid, string dllAbsolutePath, TimeSpan timeout);
}
```

**完整实现（本文档里最需要照抄的一段）**

```csharp
public static InjectionResult Inject(int pid, string dllAbsolutePath, TimeSpan timeout)
{
    // 0) 前置校验：路径必须绝对且存在（CreateRemoteThread 里没法给相对路径）
    string full = Path.GetFullPath(dllAbsolutePath);
    if (!File.Exists(full)) return InjectionResult.Fail(InjectError.RemoteResolverFailed);

    var access = ProcessAccess.CreateThread | ProcessAccess.VmOperation | ProcessAccess.VmWrite
               | ProcessAccess.VmRead | ProcessAccess.QueryInformation | ProcessAccess.Synchronize;

    IntPtr hProc = NativeMethods.OpenProcess(access, false, pid);
    if (hProc == IntPtr.Zero)
    {
        int err = Marshal.GetLastWin32Error();
        return InjectionResult.Fail(
            err == 87 /*ERROR_INVALID_PARAMETER = 进程已退出*/ ? InjectError.TargetGone
                                                              : InjectError.OpenProcessFailed, err);
    }

    IntPtr remote = IntPtr.Zero, hThread = IntPtr.Zero;
    try
    {
        // 1) 把 DLL 路径（UTF-16 + NUL）写进目标进程
        byte[] pathBytes = Encoding.Unicode.GetBytes(full + "\0");
        remote = NativeMethods.VirtualAllocEx(hProc, IntPtr.Zero, (IntPtr)pathBytes.Length,
                                             AllocationType.Commit | AllocationType.Reserve,
                                             MemoryProtection.ReadWrite);
        if (remote == IntPtr.Zero)
            return InjectionResult.Fail(InjectError.AllocFailed, Marshal.GetLastWin32Error());

        if (!NativeMethods.WriteProcessMemory(hProc, remote, pathBytes, (IntPtr)pathBytes.Length, out _))
            return InjectionResult.Fail(InjectError.WriteFailed, Marshal.GetLastWin32Error());

        // 2) 算远程 LoadLibraryW 地址 = 远程 kernel32 基址 + 本地 RVA
        IntPtr remoteK32 = NativeMethods.GetRemoteModuleBase(pid, "kernel32.dll");
        if (remoteK32 == IntPtr.Zero)
            return InjectionResult.Fail(InjectError.ModuleBaseFailed, Marshal.GetLastWin32Error());

        IntPtr localK32 = NativeMethods.GetModuleHandleW("kernel32.dll");
        IntPtr localFn  = NativeMethods.GetProcAddress(localK32, "LoadLibraryW");
        if (localK32 == IntPtr.Zero || localFn == IntPtr.Zero)
            return InjectionResult.Fail(InjectError.RemoteResolverFailed);

        long rva        = (long)localFn - (long)localK32;
        IntPtr remoteFn = new IntPtr((long)remoteK32 + rva);

        // 3) 远程线程执行 LoadLibraryW(path)
        hThread = NativeMethods.CreateRemoteThread(hProc, IntPtr.Zero, IntPtr.Zero,
                                                  remoteFn, remote, 0, IntPtr.Zero);
        if (hThread == IntPtr.Zero)
            return InjectionResult.Fail(InjectError.ThreadCreateFailed, Marshal.GetLastWin32Error());

        // 4) 等它完成（LoadLibraryW 会跑目标 DLL 的 DllMain，所以我们等的其实是"DllMain 返回"）
        uint w = NativeMethods.WaitForSingleObject(hThread, (uint)timeout.TotalMilliseconds);
        if (w == NativeMethods.WaitTimeout) return InjectionResult.Fail(InjectError.ThreadTimeout);
        if (w != NativeMethods.WaitObject0)  return InjectionResult.Fail(InjectError.ThreadTimeout);

        // 5) 返回值就是远程 HMODULE；为 0 说明 LoadLibraryW 失败（DLL 没加载起来）
        if (!NativeMethods.GetExitCodeThread(hThread, out IntPtr hMod) || hMod == IntPtr.Zero)
            return InjectionResult.Fail(InjectError.RemoteLoadReturnedNull,
                                        Marshal.GetLastWin32Error());

        return new InjectionResult(InjectError.None, 0, hMod);
    }
    finally
    {
        if (hThread != IntPtr.Zero) NativeMethods.CloseHandle(hThread);
        if (remote  != IntPtr.Zero) NativeMethods.VirtualFreeEx(hProc, remote, IntPtr.Zero, NativeMethods.MemRelease);
        NativeMethods.CloseHandle(hProc);
    }
}
```

**必须知道的四件事**

1. **`LoadLibraryW` 在 `DllMain` 里是受限的**。所以 `VmExt.Launcher.dll` 的 `DllMain` **只做一件事**：`CreateThread(Worker)` 然后立刻返回（✅ PoC 就是这么做的）。任何真活儿放到 `Worker` 线程 —— 否则死锁/超时。
2. **`DllMain` 会在什么线程上跑？** 在远程线程上，且进程被挂起在 loader lock 里（XP 之后没那么极端，但约束仍在）。所以 `Worker` 里要先 `Sleep(200)` 再干活（PoC 实测需要）。
3. **超时 20s 是给 `WaitForSingleObject` 的上限，不是"注入耗时"**。实测注入到 `vcxlaunch` 返回是**毫秒级**。超时一般意味着目标进程的 loader lock 被别的东西占着。
4. **`ERROR_INVALID_PARAMETER (87)` 是"进程已退出"的实际信号**（不是 `ERROR_NOT_FOUND`）。所以在 `OpenProcess` 失败时把 87 映射成 `TargetGone` 是必要的 —— 否则编排器会对一个已死的 pid 疯狂重试。

**测试要点**（不需要真的注入 shell 也能测）：
- 用一个自己写的 dump 进程当靶子：注入一个什么都不做的 DLL，断言 `Error == None` 且 `RemoteModuleHandle != 0`。
- 注入一个不存在的路径的 DLL → 断言 `RemoteLoadReturnedNull`。
- 注入到一个已退出的 pid → 断言 `TargetGone`。

---

### 3.8 `VmExt.Interop.DiagnosticsRuntimeLocator`

**职责**：找到 SDK 里的 `xamldiagnostics.dll`（**不是我们的文件，不随包分发**）。

```csharp
public sealed record DiagnosticsRuntime(string Path, string Source);

public static class DiagnosticsRuntimeLocator
{
    public static DiagnosticsRuntime? Locate(string? explicitPath);
    public static string DescribeSearchFailure();   // 返回"我找过哪些地方"的多行文本
}
```

**探测顺序（按优先级；找到即返回，并记 `Source` 供日志）**

| 顺序 | 来源 | 具体 |
|---|---|---|
| 1 | 用户显式配置 | `appsettings.json` 的 `DiagnosticsDll`。存在即用；不存在则**报错而不是继续**（用户明确指了路径却不对，静默回退会让人困惑） |
| 2 | 环境变量 | `%WindowsSdkDir%bin\%WindowsSDKVersion%x64\XamlDiagnostics\xamldiagnostics.dll`（装了 VS/SDK 的 shell 里通常有这两个变量） |
| 3 | 注册表 | `HKLM\SOFTWARE\Microsoft\Windows Kits\Installed Roots` → `KitsRoot10` → `bin\*\x64\XamlDiagnostics\xamldiagnostics.dll` |
| 4 | 常见路径通配 | `%ProgramFiles(x86)%\Windows Kits\10\bin\*\x64\XamlDiagnostics\xamldiagnostics.dll` |
| 5 | 本机实测已知路径 | `C:\Program Files (x86)\Windows Kits\10\bin\x64\XamlDiagnostics\xamldiagnostics.dll`（✅ PoC 用的就是这条） |

**实现要点**

1. 多版本共存时**取版本号最大的**（目录名形如 `10.0.26100.0`；用 `Version.TryParse` 比较，失败则字符串比较兜底）。
2. 只做**存在性判断**，不尝试 `LoadLibrary` 验证（那是 Launcher 在目标进程里做的事）。校验放在目标进程里的原因：只有在那里才能确认它能被那侧的 XAML core 加载。
3. `DescribeSearchFailure()` 的输出要**直接展示给用户**（含每一条尝试过的路径和结果）。这是最常见的部署失败，含糊的报错会浪费大量时间。
4. ⚠️ **`xamldiagnostics.dll` 的版本与 `Windows.UI.Xaml.dll` 版本的兼容性未验证**。PoC 用的是 SDK v10.0.14393.33 的 `xamldiagnostics.dll` 配 `Windows.UI.Xaml.dll` 10.0.26100.8972，**能用** ✅。把这个组合记进 §10.2 的监控项。

---

### 3.9 `VmExt.Interop.TapProbe`

**职责**：判断某个 ShellHost 里**是否已经有一个活着的 TAP**。这是幂等注入的唯一依据。

```csharp
public static class TapProbe
{
    /// <summary>TAP 是否已在本进程内初始化完成。同步、快速（打开一个内核对象）。</summary>
    public static bool IsTapAlive(int shellHostPid);

    /// <summary>轮询等到 TAP 就绪，或超时。</summary>
    public static bool WaitUntilAlive(int shellHostPid, TimeSpan timeout);
}
```

**原理**：TAP 在 `SetSite` 里成功拿到 `IXamlDiagnostics`/`IVisualTreeService` 之后，**创建一个命名互斥体并长期持有**：

```text
CreateMutexW(nullptr, FALSE, L"Local\\VmExt.Tap.Singleton.<pid>")
  - 若 GetLastError() == ERROR_ALREADY_EXISTS → 本实例是重复注入，记日志后**直接停止工作**（不 advise、不注入）
  - 否则持有句柄到进程退出
```

App 侧 `OpenMutexW(SYNCHRONIZE, FALSE, name) != NULL` 即"TAP 活着"。用 `Local\` 命名空间 = 会话隔离，正好匹配"我们只关心本会话"。

| 为什么用内核对象而不是文件/管道探测 | 说明 |
|---|---|
| 进程退出后**内核对象自动消失** | 文件会留下假阳性（ShellHost 崩溃后文件还在） |
| 探测成本 ~µs | 健康检查每 30s 一次也无所谓 |
| 不可能被误删 | 比"心跳文件 + 时间戳清洗"简单可靠 |
| ⭐ **同时解决了重复注入** | 一个 ShellHost 里无论有几个 Launcher 跑过，只有第一个 TAP 会工作 |

**`IsTapAlive` 实现**

```csharp
public static bool IsTapAlive(int shellHostPid)
{
    IntPtr h = NativeMethods.OpenMutexW(NativeMethods.Synchronize, false,
                                        Constants.TapMutexName(shellHostPid));
    if (h == IntPtr.Zero) return false;
    NativeMethods.CloseHandle(h);
    return true;
}
```

**`WaitUntilAlive` 实现**：`Stopwatch` + 50ms 间隔轮询；总超时默认 `Constants.TapAliveTimeout`（10s）。**不要**用 `WaitForSingleObject` 等互斥体（TAP 是持有者而不是释放者，等它会被立刻满足而给出错误结论）。

⛔ **注意"活着"≠"注入了按钮"**。`IsTapAlive` 只说明 TAP 在跑。用户看到的按钮是否存在，取决于 TAP 的 `enabled` 配置与 `Footer` 是否真的出现。UI 上要区分这两个状态（见 §3.13 的状态显示）。

---

### 3.10 `VmExt.Service.InjectionOrchestrator`

**职责**：把 §3.5-3.9 串起来，形成一个**幂等、可恢复、有状态**的流程。整个产品的大脑。

```csharp
public enum OrchestratorState
{
    Stopped = 0,
    LocatingRuntime,     // 找 xamldiagnostics.dll
    WaitingForShellHost,
    Injecting,
    WaitingForTap,
    Ready,               // TAP 已就绪，等待/已发生按钮注入
    Faulted,             // 需要用户干预（如找不到诊断运行时）
}

public sealed class InjectionOrchestrator : IAsyncDisposable
{
    public InjectionOrchestrator(AppSettings settings, ILogSink log);
    public OrchestratorState State { get; }
    public int? CurrentShellHostPid { get; }
    public string? LastError { get; }
    public event Action<OrchestratorState>? StateChanged;

    public Task StartAsync(CancellationToken ct);
    public Task<bool> ForceReinjectAsync(CancellationToken ct);  // 托盘菜单"重新注入"
    public ValueTask DisposeAsync();
}
```

**状态机（完整转移表）**

| 当前状态 | 事件 | 动作 | 新状态 |
|---|---|---|---|
| `Stopped` | `StartAsync` | `DiagnosticsRuntimeLocator.Locate` | 成功→`WaitingForShellHost`；失败→`Faulted` |
| `WaitingForShellHost` | `ShellHostStarted(p)` | 转 `Injecting`，跑 `TryInjectAsync(p)` | 见下 |
| `Injecting` | `TapProbe.IsTapAlive(p.Pid)` == true | 记"已存在 TAP，跳过" | `Ready` |
| `Injecting` | 注入返回 `Ok` | 转 `WaitingForTap`，`WaitUntilAlive` | 成功→`Ready`；超时→重试 |
| `Injecting`/`WaitingForTap` | 注入失败（可重试类） | 退避后重试（1s→2s→4s…上限 30s） | `Injecting` |
| `Injecting`/`WaitingForTap` | 失败且 `TargetGone` | 清状态 | `WaitingForShellHost` |
| `Any` | `ShellHostExited(pid)` | 清 `CurrentShellHostPid` | `WaitingForShellHost` |
| `Ready` | `HealthMonitor` 发现 `!IsTapAlive` | 强制重注入 | `Injecting` |
| `Faulted` | 用户改了配置并重启 | — | `Stopped` |
| `Any` | `DisposeAsync` | watcher.Dispose + 取消所有 Task | `Stopped` |

**`TryInjectAsync` 实现（幂等是重点）**

```csharp
private async Task<bool> TryInjectAsync(ShellHostProcess target, CancellationToken ct)
{
    // ① 幂等前置检查 —— 没有这一步，App 重启一次就会多出一个 TAP，出现两个按钮
    if (TapProbe.IsTapAlive(target.Pid))
    {
        _log.Write(LogLevel.Info, nameof(InjectionOrchestrator),
                   $"ShellHost pid={target.Pid} 已有 TAP，跳过注入");
        return true;
    }

    // ② 每次注入前重写两个 ini（配置可能刚被用户改过）
    ConfigStore.WriteTapIni(_tapIniPath, _settings);
    ConfigStore.WriteLauncherIni(
        _launcherIniPath,
        Path.Combine(_installRoot, _settings.NativeSubdir, Constants.TapDllName),
        _diagnostics.Path,
        ConfigStore.BuildInitData(_settings, target.SessionId, target.Pid, _tapIniPath, _tapLogPath));

    // ③ 注入
    string launcher = Path.Combine(_installRoot, _settings.NativeSubdir, Constants.LauncherDllName);
    var r = await Task.Run(() => RemoteInjector.Inject(target.Pid, launcher, Constants.InjectWaitTimeout), ct);
    if (!r.Ok)
    {
        _log.Write(LogLevel.Error, nameof(InjectionOrchestrator),
                   $"注入失败 pid={target.Pid} err={r.Error} win32={r.Win32Error}");
        return false;
    }

    // ④ 等 TAP 自己报活（真的初始化完成了才算成功，注入成功≠TAP 就绪）
    bool alive = await Task.Run(() => TapProbe.WaitUntilAlive(target.Pid, Constants.TapAliveTimeout), ct);
    if (!alive)
    {
        _log.Write(LogLevel.Error, nameof(InjectionOrchestrator),
                   $"pid={target.Pid} 注入成功但 TAP {Constants.TapAliveTimeout.TotalSeconds}s 内未就绪；看 launcher.log 与 tap.log");
        return false;
    }

    _log.Write(LogLevel.Info, nameof(InjectionOrchestrator), $"pid={target.Pid} TAP 就绪");
    return true;
}
```

**为什么必须有 ①**：App 重启（或用户点两次"重新注入"）时，ShellHost 里可能已经有一个跑着的 TAP。
再注入一次 Launcher 会让 `InitializeXamlDiagnosticsEx` 尝试**下一个空闲端点**（`VisualDiagConnection2`）并加载**第二个 TAP 实例** —— 两个 TAP 都会去改 `Footer` ⇒ **两个按钮**。
所以：**App 侧检查（①）+ TAP 侧单例互斥体（§3.9）= 双保险**。

⚠️ **未验证**：同一个进程里对**不同端点名**调用两次 `InitializeXamlDiagnosticsEx` 是否被允许。TAP 侧的单例守卫就是为了让这件事即使发生也无害（第二个实例自己什么都不做）。

**事件的线程处理**：`ShellHostWatcher` 的事件在后台线程触发，编排器要立刻返回。实现上用 `Channel<T>` 或 `Task.Run` 把工作排到线程池，**绝不在事件处理器里同步做 IO**。

---

### 3.11 `VmExt.Service.HealthMonitor`

**职责**：周期性校验"我们说 Ready，是不是真的 Ready"，并在异常时自愈。

```csharp
public sealed class HealthMonitor : IDisposable
{
    public HealthMonitor(InjectionOrchestrator orch, ILogSink log, TimeSpan interval);
    public void Start();
    public void Dispose();
}
```

**每次 tick 的检查项（按顺序，短路）**

| # | 检查 | 不通过的结论 | 动作 |
|---|---|---|---|
| 1 | `orch.State == Ready`？ | 非 Ready 说明别处在处理 | 跳过本次 |
| 2 | `ShellHostLocator.FindInCurrentSession(pid)` 还在？ | ShellHost 没了（Watcher 可能还没反应过来） | 通知编排器回 `WaitingForShellHost` |
| 3 | `TapProbe.IsTapAlive(pid)`？ | ⚠️ ShellHost 活着但 TAP 没了：**理论上不该发生**（TAP 无法卸载） | **记 Warn 并强制重注入**；如果连续 3 次都这样，升级为 `Faulted` 并提示用户 |
| 4 | 原生日志文件在最近 `interval*2` 内有更新？ | 只是提示级信息（TAP 可能只是没收到任何 XAML 事件） | 不动作，仅 Debug 日志 |

**实现要点**
- ⚠️ 第 3 项是**观测型**检查，其结论"TAP 无法卸载"来自 ⛔ §10.1 的设计推断，**没有实测过 TAP 消失**。所以它如果真的触发，是**重大信号**，必须把现场信息（pid、日志尾部、mutex 名）完整记下来，方便事后分析。
- 检查本身要极轻：三个 syscall 量级。默认 30s 一次，可以忽略不计。
- 用 `PeriodicTimer`（.NET 6+）而不是 `Task.Delay` 循环，避免累积漂移。

---

### 3.12 `VmExt.Service.EntryActionServer`

**职责**：接收 TAP 的点击通知（`action=pipe` 模式），派发给业务处理。

```csharp
public sealed class EntryActionServer : IDisposable
{
    public EntryActionServer(string pipeName, ILogSink log);
    /// <summary>点击回调。entryId 来自 vmext-tap.ini 的 entry{n}.id。</summary>
    public event Action<string /*entryId*/>? EntryClicked;

    public void Start();
    public void Dispose();
}
```

**实现要点**

1. 服务端：`NamedPipeServerStream(pipeName, PipeDirection.In, maxNumberOfServerInstances: 1, PipeTransmissionMode.Byte, PipeOptions.Asynchronous)`，循环 `WaitForConnectionAsync` → `StreamReader.ReadLine` → 派发 → 重建（`await using` 每次新建实例）。
2. **单实例是有意的**：TAP 一次只写一行且很快关闭，串行化反而更简单，也避免了并发读写的边界问题。若实测发现 TAP 因等待服务端而卡顿（服务端正忙），改为 `maxNumberOfServerInstances: 4`。
3. **必须容错**：TAP 可能在**没有服务端**的情况下尝试连接（App 没启动时 ShellHost 里不该有 TAP，但异常路径存在）。客户端 `WaitNamedPipeW` 超时/失败即放弃 —— 那是 TAP 侧的事（§4.2）。
4. 报文解析对**未知 `entryId` 要忽略并记 Warn**，不要抛异常。报文来自"另一个进程里的我们自己的代码"，但仍应视为外部输入。
5. 管道名由 `Constants.PipeName(sessionId)` 生成；**不要**用全局命名空间（会被其它会话看到）。
6. 端口/权限：默认管道 ACL 允许同用户访问 —— 因为 TAP 与 App 是同一用户、同一会话，**不需要改 ACL**。

**派发层**（`VmExt.App` 里实现）：`EntryClicked` → 主线程 → 打开对应功能（v1 可以是"打开某个窗口/执行一条命令/显示一个托盘气泡"）。

---

### 3.13 `VmExt.App`（托盘宿主）

```csharp
internal static class Program
{
    [STAThread]
    private static int Main(string[] args);
}

internal sealed class TrayHost : ApplicationContext
{
    public TrayHost(AppSettings settings, InjectionOrchestrator orch);
    // 菜单项：
    //   [状态]  就绪 / 等待 ShellHost / 已注入 / 故障（不可点，仅展示）
    //   [开关]  启用按钮注入            ← 改 appsettings.json 的 Enabled + 重写 vmext-tap.ini
    //   [动作]  重新注入（强制）        ← ForceReinjectAsync
    //   [动作]  打开配置文件夹          ← Process.Start(explorer, configDir)
    //   [动作]  打开日志文件夹
    //   [动作]  诊断信息（弹出可复制的文本，见下）
    //   [动作]  退出
}
```

**单实例**：`Program.Main` 用 `new Mutex(true, @"Local\VmExt.App.Singleton", out bool createdNew)`；`!createdNew` → 提示"已在运行"并退出（或激活已有实例的配置窗）。

**"诊断信息"文本的内容**（一键复制，是让用户/自己快速定位的关键）：

```
VolumeMixerExtender 诊断信息
  版本            : 1.0.0
  安装根目录      : C:\Users\eric\AppData\Local\VolumeMixerExtender
  会话 ID         : 2
  诊断运行时      : C:\Program Files (x86)\Windows Kits\10\bin\x64\XamlDiagnostics\xamldiagnostics.dll  (来源: 常见路径通配)
  ShellHost       : pid=17080 start=2026-10-03T01:41:21Z  TAP=就绪
  Launcher DLL    : ...\native\VmExt.Launcher.dll  (存在)
  TAP DLL         : ...\native\VmExt.Tap.dll       (存在)
  initData        : ver=1;cfg=...;log=...;pipe=...;mutex=...
  最近错误        : （无）
  日志目录        : ...\logs
```

**状态展示要区分的三个概念**（这是产品可用性的关键，也是 PoC 阶段最容易混淆的地方）：

| 状态 | 判定依据 | 用户看到的话 |
|---|---|---|
| **TAP 已就绪** | `TapProbe.IsTapAlive(pid)` | "已注入，打开音量面板即可看到按钮" |
| **按钮已注入** | ⚠️ 目前**没有**直接信号！TAP 只在日志里写一行 | 诚实说法："已注入"（不要说"按钮已添加"）。见下方说明 |
| **注入被禁用** | `appSettings.Enabled == false` | "已禁用（下次打开面板生效）" |

> **⚠️ 待补的可用性缺口**：TAP 完成一次按钮注入后，**没有回传任何信号给 App**。要真正做到"状态可信"，需要让 TAP 在注入成功后走 §2.4 的管道写一行 `INJECTED <entryId>`。
> 这属于 v1.1 的增强，v1 里状态只到"TAP 就绪"。**文档写清楚，不要假装状态是准的。**

---

### 3.14 可测试性划分

| 类别 | 覆盖对象 | 测试方式 | 是否需要 ShellHost |
|---|---|---|---|
| **纯单元测试** | `ConfigStore.BuildInitData` / `WriteTapIni` / `WriteLauncherIni`、`Constants` 的名字生成、`InjectionResult` 语义、initData 的转义校验 | xUnit | 否 |
| **文件系统集成** | `ConfigStore.Load/Save`、`FileLogSink` 轮转与并发写、`DiagnosticsRuntimeLocator` 的搜索顺序（用临时目录构造假 SDK 布局） | xUnit + 临时目录 | 否 |
| **进程集成** | `RemoteInjector` 注入一个"什么都不做"的测试 DLL 到一个自建进程；错误路径（不存在的 DLL、已退出的 pid） | xUnit + 自建靶进程 | 否 |
| **真实端到端** | 全链路：注入 → 开面板 → UIA 断言按钮几何 → 触发点击断言动作 | `docs/poc/scripts/cycle.ps1` 的模式（脚本驱动） | **是** |
| **不可自动化** | 面板在 band=4，`EnumWindows` 看不见；UIA 只能靠 `GetForegroundWindow` 拿到面板句柄，而一旦有别的窗口抢焦点就失手 | 人工 + 脚本辅助 | 是 |

**因此**：需要在 CI 里跑的只有前三类；第四类必须跑在有交互桌面的机器上（不能是 Session 0 / 无头环境）。

---

## 4. 原生侧（C++）功能模块

**总原则**：原生代码只做托管代码**做不到**的两件事，并且**随时可以被"完全放弃"**。任何异常、任何不认识的状态，一律选择"什么都不做 + 记日志"，绝不抛、绝不崩。

### 4.1 `VmExt.Launcher.dll`（in-proc 第 1 段）

**职责**：在 ShellHost 进程内调用 `InitializeXamlDiagnosticsEx`，把 TAP 拉起来。**只做这一件事**。
**不负责**：不改 XAML、不匹配元素、不持有诊断接口。

**导出**：无（只有 `DllMain`）。
**依赖**：`ole32.lib`（`CoInitializeEx`）。**不需要** cppwinrt 头、不需要 `WindowsApp.lib`。

#### 4.1.1 配置：`VmExt.Launcher.ini`（与 DLL 同目录，注入前由 C# 写好）

```ini
[launcher]
; TAP DLL 路径。相对路径按本 DLL 所在目录解析
tap=VmExt.Tap.dll
; SDK 里的诊断运行时
xamldiag=C:\Program Files (x86)\Windows Kits\10\bin\x64\XamlDiagnostics\xamldiagnostics.dll
; ★ 产品新增：初始化数据（§2.2 的 k=v; 串），传给 TAP
initdata=ver=1;cfg=...;log=...;pipe=...;mutex=...
; ★ 产品新增：日志路径（缺省为本 DLL 同目录 launcher.log）
log=...\logs\launcher.log
; 等待 Windows.UI.Xaml.dll 的上限（毫秒）
xamlWaitMs=30000
; 端点名探测起始索引（正常恒为 1）
endpointStart=1
```

#### 4.1.2 方法逐条

| 方法 | 签名 | 线程 | 职责 | 失败处理 |
|---|---|---|---|---|
| `DllMain` | `BOOL WINAPI DllMain(HMODULE, DWORD, LPVOID)` | loader 线程 | 只做：`DisableThreadLibraryCalls` → `CreateThread(Worker)` → `CloseHandle` | 建线程失败：返回 TRUE 让 DLL 正常加载（**不要**返回 FALSE，那会变成注入失败） |
| `Worker` | `DWORD WINAPI Worker(LPVOID self)` | 自建线程 | 全部实际工作，见下方步骤 | 任何一步失败 → 记日志 → `return 0`（**不要** `ExitProcess`） |
| `DllDir` | `std::wstring DllDir()` | 任意 | 由 `g_self` 取本 DLL 目录 | — |
| `LogF` | `void LogF(const char*, ...)` | 任意 | 格式化 + 锁 + `WriteFile` | 打不开日志句柄 → 全部静默 |
| `ReadIniString` | `std::wstring ReadIniString(const wchar_t* key, const wchar_t* def)` | 任意 | `GetPrivateProfileStringW` 读 `VmExt.Launcher.ini`；相对路径拼 DLL 目录 | 缺键用默认值 |
| `FileExists` | `bool FileExists(const std::wstring&)` | 任意 | `GetFileAttributesW != INVALID` | — |
| `InitXamlDiagnosticsExFn` | 函数指针 typedef | — | `HRESULT __stdcall(LPCWSTR endPointName, DWORD pid, LPCWSTR wszDllXamlDiagnostics, LPCWSTR wszTAPDllName, CLSID tapClsid, LPCWSTR wszInitializationData)` | — |
| `ProvideConfigToTap` | `bool ProvideConfigToTap(const std::wstring& tapPath, const std::wstring& config)` | 任意（Worker 内） | ★ 直投：`LoadLibraryW` + `GetProcAddress("VmExtTapProvideInitData")` + 调用；**刻意不** `FreeLibrary` | 加载失败或找不到导出 → 记日志 + `return false`（**不致命**，initData 还能兜底 ≤259 的配置） |

> ⛔ **函数指针的 6 个参数与顺序必须与 [附录 A](#附录-a-xamlomh-关键签名) 完全一致**。这是 ABI 级契约，写错不会编译报错，只会在运行时拿到一个诡异的 HRESULT 或崩在目标进程里。

#### 4.1.3 `Worker` 的完整步骤（按顺序，每步都有为什么）

```text
 1. Sleep(200)
     为什么：注入线程已经在别人的进程里跑。给目标进程的 loader 一点余量，
              避免与 CreateRemoteThread 的初始化抢 loader lock。PoC 实测需要这一步。

 2. 打开日志：CreateFileW(<logPath>, FILE_APPEND_DATA,
                FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_ALWAYS)
     为什么：FILE_SHARE_READ|WRITE 让 App 能在 TAP 持有句柄时仍然读日志；
              不要用 CRT 的 FILE*（§4.3 坑 2）。

 3. CoInitializeEx(nullptr, COINIT_MULTITHREADED)
     为什么：XAML 诊断会建立 COM 通道，MTA 是安全选择。
              返回值即使是 S_FALSE / RPC_E_CHANGED_MODE 也继续（不是致命）。

 4. 等 XAML 核心就绪：循环 GetModuleHandleW(L"Windows.UI.Xaml.dll")，100ms 一次，
    上限 xamlWaitMs（默认 30000ms）。拿不到 → 记日志 + return。
     为什么：目标可能刚启动，XAML 还没加载。等到它加载了才有 InitializeXamlDiagnosticsEx。

 5. GetProcAddress(xaml, "InitializeXamlDiagnosticsEx")
     拿不到 → 记日志（含 DLL 版本号）+ return。可能原因：WinUI3 而非 System XAML。

 6. 读 ini 并校验两个路径都存在：
       tap       ← 相对路径按 DLL 目录解析
       xamldiag  ← 默认 SDK 路径
     缺一个 → 记日志（写清缺哪个、实际展开成什么路径）+ return。
     为什么：这两个文件是在**目标进程**里被加载的，路径必须是目标能访问的绝对路径。

 7. ★ 组装配置串（产品新增）：从 ini 读 initdata 原样（§2.2.2 格式）。
    先做长度自检：> Constants.InitDataHardLimit(259) → **仍然照传**，但记一条警告：
      "配置 N 字符 > 259，initData 面包屑会被框架静默丢弃（配置由直投通道送达）"
     为什么还要照传：超限丢的只是面包屑，功能不受影响；但这条日志是将来排查的唯一线索 ——
                    不写它，事后看到 TAP 侧 initData 为空就会误判成"配置没送出去"。

 8. ★★ 配置直投（产品新增，**必须在第 9 步之前**）：
       HMODULE hTap = LoadLibraryW(<tap 绝对路径>);
       auto p = (void (WINAPI*)(const wchar_t*))GetProcAddress(hTap, "VmExtTapProvideInitData");
       if (p) p(config.c_str());  else 记日志 "找不到导出"
       ★ 刻意**不** FreeLibrary（§2.2.1）
     为什么排在这里：此刻 TAP 模块已被加载、全局变量可写；随后 XAML core 加载同一模块
                    （同一个 HMODULE）时配置已就位，DllGetClassObject 才拿得到。
     失败处理：导出找不到 → 记日志但**继续**（initData 通道还能兜底 ≤259 的配置）。

 9. ★ 读 initdata（见第 7 步），调用时作为第 6 个参数传入（空则传 nullptr）。

10. 端点名探测循环 i = endpointStart(1) .. endpointStart+10000：
       endpoint = L"VisualDiagConnection" + i
       hr = pInit(endpoint, GetCurrentProcessId(), xamldiag, tap, CLSID, initdata)
       SUCCEEDED(hr) → 记 SUCCESS（含端点名与索引）+ return
       hr == E_INVALIDARG → 记日志 + break
          为什么：E_INVALIDARG 通常不是"端点被占用"，而是参数本身不被接受
                  （路径不对、CLSID 不对、initdata 不合规）。继续加索引无意义。
       否则 → 继续下一个索引（前 3 次与每 1000 次记日志，避免刷屏）
```

**关于端点名探测**（✅ 实测）：第一次尝试 `VisualDiagConnection1` 就成功。
保留循环的理由是**幂等性**：如果 ShellHost 里已经有一个诊断会话（比如 App 重启后重复注入，或别的工具先占了），`1` 会失败。循环让新会话拿到 `2`、`3`… —— 但**注意**这会导致多个诊断会话并存，此时靠 §4.2 的 TAP 单例守卫保证只有一个生效。

#### 4.1.4 日志样本（排障时按这个判断）

```
[01:41:33.502 tid=6500] ================================================================
[01:41:33.502 tid=6500] vcxlaunch attached: pid=17080 tid=6500 CoInitializeEx=0x00000000
[01:41:33.502 tid=6500] Windows.UI.Xaml.dll = 00007FFE421D0000
[01:41:33.502 tid=6500] InitializeXamlDiagnosticsEx = 00007FFE42907C30
[01:41:33.502 tid=6500] xamldiagnostics.dll = C:\...\xamldiagnostics.dll  (exists=1)
[01:41:33.502 tid=6500] TAP dll             = C:\...\vcxtap.dll  (exists=1)
[01:41:33.502 tid=6500] ---- walking endpoint names VisualDiagConnection1..N ----
[01:41:33.502 tid=6500] SUCCESS: endpoint=VisualDiagConnection1  hr=0x00000000  (index 1)
```

| 日志停在哪一行 | 结论 |
|---|---|
| 完全没有 `vcxlaunch attached` | 注入没成功，或 `DllMain` 里 `CreateThread` 失败 → 看 App 日志的 `InjectionResult` |
| 停在 `Windows.UI.Xaml.dll` 之前 | 等了 30s 还没等到 XAML → 目标不是 System XAML 宿主 |
| 停在 `InitializeXamlDiagnosticsEx` | 导出不存在 → 可能 WinUI3 |
| 停在 `(exists=0)` | 路径不对 → 看 App 写的 `VmExt.Launcher.ini` |
| 停在 `SUCCESS` 之前并出现多个 `hr=` | 端点被占 → 同时检查 `tap.log` 是否已有另一个实例 |
| `SUCCESS` 之后 `tap.log` 没动静 | TAP DLL 加载失败（依赖缺失、导出名错）→ 用 `dumpbin /exports` 核对，或看系统事件日志的 loader 错误 |

---

### 4.2 `VmExt.Tap.dll`（in-proc 第 2 段，COM in-proc server）

**职责**：接住 XAML 的视觉树事件，在 `Footer` 出现时注入按钮，并把点击动作送出去。
**不负责**：不做进程监视、不做注入、不决定"要不要启用"以外的业务。

**导出**（⛔ 必须用链接器 `/EXPORT:`，不是 `__declspec(dllexport)`，见 §4.3 坑 3）：

```text
DllGetClassObject(REFCLSID, REFIID, LPVOID*)  → 只为 CLSID_VmExtTap 服务
DllCanUnloadNow()                              → 仅当无存活 Tap 对象时返回 S_OK
```

**依赖**：cppwinrt 头（仅头文件）+ `WindowsApp.lib` + `ole32.lib`。

#### 4.2.1 COM 对象模型

```text
DllGetClassObject(CLSID, IID_IClassFactory)
  └─ TapFactory : IClassFactory
       QueryInterface : IUnknown | IClassFactory
       CreateInstance(outer, riid, ppv)
            outer != nullptr → CLASS_E_NOAGGREGATION      // 不支持聚合
            new Tap() → QueryInterface(riid, ppv) → Release()
       LockServer → S_OK                                  // 什么都不做

Tap : IVisualTreeServiceCallback, IObjectWithSite
  QueryInterface :
      IUnknown                        → this (投给 IVisualTreeServiceCallback*)
      IVisualTreeServiceCallback      → this
      IObjectWithSite                 → this
      其它（含 IVisualTreeServiceCallback2）→ E_NOINTERFACE   ★ 见下
  AddRef / Release : Interlocked，归零即 delete
  IObjectWithSite::SetSite / GetSite
  IVisualTreeServiceCallback::OnVisualTreeChange
```

> ⭐ **`QueryInterface` 只声明 `IVisualTreeServiceCallback` + `IObjectWithSite` 是够的**（✅ PoC 全程跑通）。
> `IVisualTreeServiceCallback2`（`OnElementStateChanged`）是**可选**回传接口。XAML core 会 QI 它，拿到 `E_NOINTERFACE` 就退回 v1 接口，**不会**因此拒绝连接。
> **不要**为了"更完整"去实现 `IVisualTreeServiceCallback2`：多实现一个接口就多一份 vtable 布局出错的机会，而它对我们没有价值。

#### 4.2.2 方法逐条

| 方法 | 职责 | 关键约束 |
|---|---|---|
| `DllMain(DLL_PROCESS_ATTACH)` | `g_self = self` → `DisableThreadLibraryCalls` → `LogInit()` | ⛔ **不得**在这里创建互斥体、不得做 COM 初始化、不得 `LoadLibrary`（loader lock）。只做日志与保存 HMODULE |
| `LogInit()` | `CreateFileW(..., FILE_APPEND_DATA, FILE_SHARE_READ\|FILE_SHARE_WRITE, OPEN_ALWAYS)` + `InitializeCriticalSection` + 写一行分隔线 | 失败则后续所有 `LogF` 静默 |
| `LogF(fmt, ...)` | 时间戳 + tid + 格式化 + 持锁 `WriteFile` | ⛔ 必须持锁；缓冲区固定 1600 字节并 `_TRUNCATE` |
| `Tap::SetSite(IUnknown* site)` | 见下方流程 | 必须在 **advise 之前**完成单例检查 |
| `Tap::GetSite(riid, ppvSite)` | 转发给 `m_diag->QueryInterface` | — |
| `Tap::OnVisualTreeChange(rel, el, type)` | 见 §4.2.4 | ⛔ 在 UI 线程；⛔ 不得抛异常；⛔ 不得长时间阻塞 |
| `BuildTestLinkButton(model)` | 造按钮 + 复制样式 + 绑 Click | 见 §5.3 |
| `InjectIntoRow(model, btn)` | 包 Grid 落位 | 见 §5.2 |
| `InjectIntoFooter(footer)` | 去重 + 找模型按钮 + 调用上面两个 | 见 §4.2.4 |
| `TryInjectLater(footer, attempt)` | 把工作排到 UI 线程并等模型按钮出现 | `CoreDispatcher` + 最多 20 次 Low 优先级重试 |
| `ApplyRightAlign(footer, btn)` | **仅兜底路径使用**的右边距计算 | ⛔ 内部读 `ActualWidth`，必须先判 `>0`（§5.5） |
| `PerformClickAction(entry)` | 按配置执行动作 | ⛔ 不得阻塞 UI 线程 |
| `RunWinver()` | PoC 遗留的动作（`CreateProcessW(L"winver.exe")`） | 产品中改为 `PerformClickAction` |

#### 4.2.3 `SetSite` 的精确流程

```text
 1. 清掉旧的 m_vts / m_diag（Release）
 2. site == nullptr → return S_OK（XAML 在拆除诊断会话）
 3. QI IXamlDiagnostics  → m_diag
    QI IVisualTreeService → m_vts
    任一失败 → 记日志 + return S_OK（★ 返回失败会让 XAML 认为 TAP 坏了）
 4. ★ 配置来源判定（**直投为主，initData 面包屑**，§2.2.1）：
      a) 先取直投配置：加锁读本 DLL 的全局变量 g_providedData + g_hasProvided
         为什么先取它：它是主通道，且**没有长度限制**
      b) 再读 initData：IXamlDiagnostics::GetInitializationData(&bstr)
         ⚠️ 用 SysStringLen(bstr) 取长度，**不要**用 wcslen —— BSTR 允许内嵌 \0
         ⚠️ 用完 SysFreeString（实测每次是新分配，释放安全）
      c) 比对并记日志（这三行是排查配置问题的第一现场）：
           直投通道: 有/无  len=N  fnv1a64=XXXX
           initData : len=N  fnv1a64=XXXX
           + 判定：
             两者一致                        → "两种通道内容一致（未触及 259 上限）"
             直投有、initData 空、直投 > 259 → "initData 为空属**预期**：超过 259 上限被静默丢弃"
             直投有、initData 空、直投 ≤ 259 → "!! 非已知的长度上限原因"（★ 反向哨兵）
             两者都非空但不一致               → "!! 两通道内容不一致 —— 以直投为准"
             直投无                          → "!! 直投通道未收到数据"（降级路径）
      d) 决定生效配置：直投有则用直投；直投无但 initData 非空则用 initData（降级，仍记 !! 日志）
      e) ★ 从生效配置里取 cfg= → 存进 g_cfgPath，之后**所有** ini 读取都走它
         取不到 cfg → 回退到 DLL 同目录的 tap.ini，并记日志
         为什么必须有 e)：这才叫"把通道接上了"。否则通道只传了一堆没人用的字节。
 5. ★ [必须] 单例自检：
      hMutex = CreateMutexW(nullptr, FALSE, mutexName)
      GetLastError() == ERROR_ALREADY_EXISTS
         → 记日志 "duplicate TAP, going dormant" → SetSite 设 m_dormant = true → return S_OK
           （此后 OnVisualTreeChange 直接 return，不 advise、不注入）
      ★ 为什么必须在"advise 之前"：advise 之后就会开始收事件，
        重复实例如果在事件里才发现自己是多余的，可能已经注入了一次按钮
 6. 打开日志（若配置给了新路径，切换过去）
 7. m_vts->AdviseVisualTreeChange(this)
     成功 → 记日志；之后 OnVisualTreeChange 会开始被调用（**包括已存在树的重放**）
     失败 → 记 hr + return S_OK
```

> ✅ **第 4 步已实测完毕**（T1，长度扫描 9 点）。结论：
> `GetInitializationData` **会**原样返回 initData（含中文，≤259 字符逐字符一致），
> 但 **≥260 字符时静默返回空串且 `hr` 仍是 `S_OK`** ⇒ 配置主干改走**直投通道**（第 4a 步），
> initData 降级为面包屑。完整数据见 `verified-after-injection/06-initdata-channel-limit.md`。

> ⚠️ **第 4c 步的"反向哨兵"不是装饰**：按实测，直投 ≤259 时 initData **不该**为空。
> 真出现这种组合，说明 OS 引入了**别的**静默失败条件（Windows 更新就可能发生）。
> 没有这一行，那种情况会表现为"配置神秘丢失"且毫无线索。

#### 4.2.4 `OnVisualTreeChange` 的精确流程

```text
 1. 计数 seq = InterlockedIncrement(&g_events)
 2. 有界日志：seq <= g_logLimit(40000) 才打 "ADD/REM parent=.. idx=.. handle=.. type=[] name=[] children=N"
     为什么要有界：面板每次打开会推送几百条事件，无界日志会吃掉磁盘
 3. m_dormant → return S_OK
 4. 快速过滤（在拿到真对象之前，尽量便宜）：
      mutationType == Add
      && Name != nullptr && Name[0] != 0
      && wcscmp(Name, L"Footer") == 0
      （★ 只在 enabled=1 时才做后续工作；enabled 从 ini 每次重读）
 5. IXamlDiagnostics::GetIInspectableFromHandle(element.Handle, &insp)
      失败 → 记 hr + return S_OK
 6. winrt::Windows::Foundation::IInspectable obj{insp, take_ownership_from_abi}
    obj.try_as<WUX::FrameworkElement>() → 失败 → 记日志 + return S_OK
 7. TryInjectLater(fe, 0)
      整段包在 try/catch(...) 里，捕获后只记日志
 8. return S_OK   ★ 无论内部发生什么，都返回 S_OK
```

**第 4 步之后为什么要"延后"（`TryInjectLater`）**：
✅ 实测：`Footer` 的 **Add 事件先到，它的子元素（含模型按钮）随后才出现**。所以在 Add 回调里立即找模型按钮会拿到 `nullptr`。
处理方式：拿 `footer.Dispatcher()`，用 `RunAsync(CoreDispatcherPriority::Low, ...)` 重新尝试；最多 20 次；`HasThreadAccess()` 为真时直接同步执行（此时已经在 UI 线程，避免多绕一圈）。

| 细节 | 说明 |
|---|---|
| `RunAsync` 的 lambda **必须捕获 `Footer` 的 `FrameworkElement` 值**（不是裸指针） | 捕获引用/裸指针会在面板关闭后变成野指针 |
| 重试上限 20 次 | 每次都是 Low 优先级，实际观察是**第 1~2 次就成功**。上限只是为了不死循环 |
| 20 次都失败 | 记一条 Warn（说明页面的元素结构可能变了 → 对应 §10.2 的监控项），然后放弃 |

#### 4.2.5 点击动作 `PerformClickAction`

```text
从"创建按钮时"读到的 entry 配置复制到闭包里（不要每次点击重读 ini，点击延迟敏感）：
  action == exec :
      ★ 命令行由 Contract::BuildCommandLine(exe, args) 拼 —— exe 路径**加引号**，
        参数原样附上。⛔ 绝不把配置里的整条命令行直接交给 CreateProcessW：
        lpApplicationName=NULL + 未加引号的含空格路径会被**逐段前缀试探**，
        前缀处存在同名 exe 就启动那个（已实测复现，见 §4.3 坑 21）。
      std::wstring cmdline = BuildCommandLine(e.exe, e.args);
      std::vector<wchar_t> cmd(cmdline.begin(), cmdline.end()); cmd.push_back(L'\0');
      //                                       ↑ lpCommandLine **必须可写**
      STARTUPINFOW si{ sizeof(si) }; PROCESS_INFORMATION pi{};
      CreateProcessW(e.exe.c_str(),     // ★ 显式 lpApplicationName，杜绝前缀试探
                     cmd.data(),
                     nullptr, nullptr, FALSE, 0, nullptr,
                     nullptr,          // 工作目录：留 nullptr（由被启动的程序自己处理，见 §2.3 📌）
                     &si, &pi)
      成功 → 立即 CloseHandle(pi.hThread/pi.hProcess)
             ⛔ **不要** WaitForSingleObject —— 会卡住 ShellHost 的线程（点一下卡整个任务栏）
      失败 → 记 GetLastError()（不记的话用户只看到"点了没反应"，无从排查）
  action == pipe :
      ★ 必须放到独立线程里做（或见下方"非阻塞写法"），绝不能阻塞 UI 线程
      线程内： h = CreateFileW(pipeName, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr)
              若 INVALID_HANDLE_VALUE → 尝试 WaitNamedPipeW(pipeName, 200) 再重试一次
              成功 → 写 "CLICK <entryId> <unixMillisUtc>\n" → CloseHandle
              任一失败 → 记日志 → 结束（不重试、不弹窗）
```

| 陷阱 | 说明 |
|---|---|
| `CreateProcessW` 的 `lpCommandLine` **参数必须指向可写内存** | 传 `LPCWSTR` 字面量在某些情况下会被改写导致访问违规。用局部 `wchar_t buf[]` 或 `std::vector<wchar_t>` |
| 只 `CreateProcessW` 不 `CloseHandle` | 每次点击泄漏两个句柄 |
| 在 UI 线程上 `CreateFileW` 管道 | 服务端没起来时会卡住整个面板。**必须**放独立线程或用 `WaitNamedPipeW` 的短超时 + 一次性尝试 |
| 复用闭包里的 `wchar_t[]` | 闭包可能被多次调用（多次点击）→ 每次用独立缓冲 |

> **✅ 已实测的部分**：`CreateProcessW(L"winver.exe")` 从 `Button.Click` 里调用成功拉起进程（UIA `InvokePattern` 触发，窗口标题「关于"Windows"」）。
> ✅ **已实测完毕**（T3，2026-10-03，见 `verified-after-injection/08-pipe-action-chain.md`）：
> 报文逐字节正确；**UI 线程未被拖住**（`Invoke` 往返 7–14 ms）；管道 IO 在独立线程（tid 可证）；
> 服务端缺失时瞬时降级。⚠️ 服务端三个坑见 §2.4（其中 `ERROR_NO_DATA(232)` 是实测**新发现**）。

#### 4.2.6 内存与生命周期规则

| 规则 | 说明 |
|---|---|
| `VisualElement::Type` / `Name` 是 **BSTR** | ⛔ **不要 `SysFreeString`**。它们的所有权在诊断框架，PoC 全程只读。⚠️ 所有权未正式验证 —— 保守起见只读、绝不释放 |
| `GetIInspectableFromHandle` 拿到的 `IInspectable*` 是**带引用的** | 用 `winrt::take_ownership_from_abi` 接管，**不要**再手动 `Release` |
| `m_diag` / `m_vts` 在 `Tap` 析构时 `Release` | 在 `SetSite` 里换新值前也要先 Release 旧的 |
| `Tap::g_objects` 用 `InterlockedIncrement/Decrement` | `DllCanUnloadNow` 靠它判断 |
| ⛔ **TAP 实际上卸载不掉** | 我们在 `SetSite` 里把 `this` 交给 `m_vts->AdviseVisualTreeChange(this)`，并且**从不** `UnadviseVisualTreeChange`（没必要：下一块面板打开时还要接着用同一个对象）。只要框架持有这个回调引用，`Tap::g_objects` 就持续 ≥ 1 ⇒ `DllCanUnloadNow` 永远返回 `S_FALSE` ⇒ 卸载不了。**这是刻意的选择，不是缺陷**，见 §10.1 #2 |
| 注入的按钮**不会**持有 `Tap` 的引用 | `btn.Click` 的 lambda 只按值捕获 entry 配置（几个 `std::wstring`/枚举），不捕获 `this`。所以"按钮存在"不是 TAP 无法卸载的原因 —— 真正的原因只是上面那条 |

---

### 4.3 原生代码的硬性约束清单（全部是踩过的坑）

这张表是**编码前必读**。每一条都对应一次真实的失败。

| # | 约束 | 症状 | 正确做法 |
|---|---|---|---|
| 1 | `DllMain` 里不能做重活 | 死锁 / 注入超时 | 只 `DisableThreadLibraryCalls` + `CreateThread` + 返回 TRUE |
| 2 | ⛔ 不要用 CRT 的 `FILE*` + `ccs=UTF-8` 写日志 | **静默只写出一个 BOM，日志全丢**（本机实测） | `CreateFileW` + `WriteFile`，`FILE_SHARE_READ\|FILE_SHARE_WRITE`，`OPEN_ALWAYS` |
| 3 | ⛔ `DllGetClassObject`/`DllCanUnloadNow` 必须用 `STDAPI` | `error C2375: 重定义；不同的链接` | `combaseapi.h` 里它们是 `WINOLEAPI`（`EXTERN_C HRESULT STDAPICALLTYPE`），必须匹配；导出靠链接器 `/EXPORT:DllGetClassObject /EXPORT:DllCanUnloadNow`。**不要**用 `__declspec(dllexport)` + `extern "C" HRESULT __stdcall` |
| 4 | 需要装 `Button.Click` 的事件 | 编译错误找不到 `Click` | `#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>`（`Click` 在 `ButtonBase` 上） |
| 5 | 需要 `Panel::Children()` / `IVector` | 编译错误 | `#include <winrt/Windows.Foundation.Collections.h>` |
| 6 | `windows.h` 的 `GetCurrentTime` 宏与 cppwinrt 成员名冲突 | `warning C4002` | `#include <windows.h>` 之后 `#ifdef GetCurrentTime / #undef GetCurrentTime / #endif` |
| 7 | 本机 cppwinrt 版本**没有** `winrt::IInspectable` | `IInspectable 不是 winrt 的成员` | 用全名 `winrt::Windows::Foundation::IInspectable` |
| 8 | `UIElementCollection` **没有** `Remove(UIElement)` | `C2039: "Remove" 不是 ... 的成员` | 它是 `IVector<UIElement>` + `require<IUIElementCollection>` → 用 `IndexOf(el, out uint32 idx)` + `RemoveAt(idx)` |
| 9 | `Grid::SetColumn` 的参数是 `FrameworkElement` | `C2664: 无法将 UIElement 转换为 FrameworkElement` | 先把容器 `try_as<WUX::FrameworkElement>()` 再传 |
| 10 | `TransformToVisual` 在 `UIElement` 上，不在 `DependencyObject` 上 | `C2039` 一串连带错误 | 参数类型用 `WUX::FrameworkElement const&`，不要用 `DependencyObject const&` |
| 11 | 已加载的 DLL 文件被写锁定 | `LNK1104: 无法打开文件 ...dll` | 迭代时杀 ShellHost；正常部署时**先停进程再替换文件**（安装脚本必须处理，见 §7.5） |
| 12 | 所有 COM 边界必须吞异常 | 异常穿过 COM 边界 → shell 崩 | 每个导出方法、每个回调方法整体 `try { ... } catch (...) { LogF(...); }`，返回 `S_OK` |
| 13 | `GetPrivateProfileStringW` 对非 ASCII 的解读依赖文件编码 | 中文按钮文字变成乱码（或设置被静默忽略） | C# 侧写 INI 用 **UTF-16LE + BOM**（`new UnicodeEncoding(false, true)` / `Encoding.Unicode`）。✅ **已实测（T2）**：UTF-8 无 BOM ⇒ **静默乱码**；UTF-8 有 BOM ⇒ **连键都找不到**；UTF-16LE 无 BOM ⇒ 乱码；只有 UTF-16LE+BOM 正确。详见 `verified-after-injection/09-ini-encoding.md` |
| 14 | CRT 用 `/MT` 静态链接 | 目标机缺 VC++ Redistributable 时 DLL 加载失败（`LoadLibraryW` 返回 NULL → 注入报 `RemoteLoadReturnedNull`） | `cl /MT`（PoC 已是如此） |
| 15 | cppwinrt 需要 `WindowsApp.lib` | 链接错误找不到 `RoActivateInstance` 等 | `/link WindowsApp.lib ole32.lib`（TAP）；Launcher 只需 `ole32.lib` |
| 16 | 记日志要带 tid | 无法判断回调在哪个线程 | `LogF` 里固定输出 `GetCurrentThreadId()`；这是判断"是否在 UI 线程"的唯一手段 |
| 17 | ⛔ **无 BOM 的 UTF-8 源码 + 中文注释** | `warning C4819`，进而 **`error C2001: 常量中有换行符`** + `C1075`，报错行看着完全正常，**极难定位** | MSVC 无 BOM 时按**系统 ACP**（本机 936）解读源码；某个中文字的 UTF-8 尾字节是 `0x5C`（`\`），恰好把字符串字面量的收尾引号"吃掉" ⇒ 字面量跨行。**解决：编译参数加 `/utf-8`**（PoC `build.cmd` 已加；§7.3） |
| 18 | ⛔ **`InitializeXamlDiagnosticsEx` 的 initData 超过 259 字符会静默失效** | 没有任何错误：`hr=S_OK`，且 `GetInitializationData` 也返回 `S_OK`，只是 BSTR 长度为 0 ⇒ **配置神秘丢失** | 别把配置压在 initData 上。配置走**直投通道**（§2.2.1），initData 只当面包屑；超限时**主动打警告日志**（附长度），否则事后无从判断 |
| 19 | ⛔ 用 `wcslen` 量 BSTR 长度 | 内容里含 `\0` 时把"不同"误判成"相同" | BSTR **允许内嵌 `\0`** ⇒ 用 `SysStringLen(bstr)` |
| 20 | ⚠️ 比较两个字符串决定"配置对不对"时用自算哈希 | 哈希算法/种子与外部实现不一致 ⇒ 日志里的哈希**无法被第三方复核** | 用**标准 FNV-1a 64**：偏移基 `0xCBF29CE484222325`（注意网上大量资料误写成 `1469598103934665603`，少一位），按 UTF-16 码元逐位异或；空串哈希须等于偏移基（最好的自检） |
| 21 | ⚠️ **`CreateProcessW(lpApplicationName=NULL, 未加引号的含空格路径)`** | ⚠️ **可能启动错的程序**：该 API 会对命令行**逐段前缀试探**，前缀处存在同名 exe 就启动它（已复现：`C:\...\Temp\a b c\x.exe` 不加引号时启动了 `C:\...\Temp\a.exe`）。**平时能跑，前缀处一旦有同名文件就静默跑错程序** | **两个都做**：① `lpApplicationName` 显式传 exe 路径；② 命令行里路径**加引号**（由产品拼，别指望用户填对）。最好把配置拆成 `entry1.exe` + `entry1.args` 两段<br>⚠️ 这是**正确性**问题，不是安全问题（同用户上下文，不跨权限边界） |
| 22 | ⚠️ `CreateProcessW` 的 `lpCommandLine` 传字符串字面量 | 行为未定义（API 会**就地修改**该缓冲） | 复制到 `wchar_t[]` 临时缓冲再传 |
| 23 | ⚠️ 命令里写 `.lnk` 快捷方式 | `GetLastError=193`（`ERROR_BAD_EXE_FORMAT`）—— 点击"什么都没发生" | 快捷方式的解析是 **shell** 的职责。要那种效果必须 `explorer.exe "<lnk>"` |
| 24 | ⛔ 点击处理里 `WaitForSingleObject(子进程)` | **阻塞 ShellHost 的线程** ⇒ 点一下卡住整个任务栏 | 拿到 `PROCESS_INFORMATION` 就 `CloseHandle` 走人，不要等 |

---

## 5. 关键算法与不变量

这一节把"为什么要这么写"讲透。**只照抄代码不理解这几条，改动时极容易破坏行为。**

### 5.1 `Footer` 定位算法

**输入**：`IXamlDiagnostics` 给的 `Footer` 元素 `FrameworkElement`
**输出**：ItemsPanel（`StackPanel`）、item container（`ContentPresenter`）、模型按钮（`Button`）

**✅ 实测的祖先链**（`Up[]` 就是按这个顺序打的日志）：

```text
Button            ← 模型按钮「更多音量设置」，我们要拿它当样式与位置基准
  → ContentPresenter          up[0]
    → ContentControl          up[1]   ← 这一"行"的逻辑内容
      → ContentPresenter      up[2]   ← ★ ItemsControl 为它生成的 item container
        → StackPanel[Vertical] up[3]  ← ★★ ItemsPanel（纵向！这就是必须包 Grid 的根本原因）
          → ItemsPresenter     up[4]
            → ItemsControl     up[5]  ← Name = "Footer"（我们匹配的那个）
              → Border         up[6]
                → ContentPresenter up[7][PageContent]
                  → Grid[FullScreenPageRoot] up[8]
                    → ...（继续向上是 PageWindow / FullScreenPage / L2Frame / ControlCenterView）
```

**算法（`InjectIntoRow` 的前半段）**：

```cpp
WUX::DependencyObject child = model, cur = model, itemsPanel = nullptr, container = nullptr;
for (int i = 0; i < 12 && cur; ++i) {
    auto p = VisualTreeHelper::GetParent(cur);
    if (!p) break;
    if (p.try_as<WUXC::ItemsPresenter>()) {   // 命中：说明 cur 就是 ItemsPanel
        itemsPanel = cur;                     //   → StackPanel
        container  = child;                   //   → 上一次循环里的那个（item container）
        break;
    }
    child = cur;                              // 记住"上一个"，它才是 panel 的直接子节点
    cur   = p;
}
```

| 判定 | 为什么这么写 |
|---|---|
| 用 `ItemsPresenter` 作为"锚"而不是直接数层数 | 层数会随模板变化；`ItemsPresenter` 是 `ItemsControl` 模板里**语义稳定**的节点（`ItemsControl → ItemsPresenter → ItemsPanel` 是 XAML 约定） |
| 用 `child` 而不是 `cur` 作为 container | 命中时 `cur` 已经是 ItemsPanel；**panel 的直接子节点才是 item container**。这是最容易写错的一处 |
| 上限 12 层 | 实测只需 5 层。留余量但不无限走，避免在某些奇怪模板下走到整个树的根 |
| 参数类型是 `FrameworkElement` 而不是 `DependencyObject` | 后面要用 `TransformToVisual`（§4.3 坑 10） |

**找不到怎么办**：`InjectIntoRow` 返回 `false` → 走兜底（往 `ItemsControl.Items()` append 一项）。
兜底的**已知后果**（✅ 实测）：因为 ItemsPanel 是纵向的，按钮会落在**第二行**，footer 高度 48 → 78。
**这是可接受的降级**：功能可用、位置不理想，好过什么都不做。但必须在日志里明确写出"走了兜底路径"。

### 5.2 同行右对齐算法（本项目的核心难点）

**问题**：底栏是 `ItemsControl`，它的 `ItemsPanel` 是**纵向** `StackPanel`。
⇒ **往里 append 一项必然新起一行**（✅ 实测 footer 48→78，按钮掉到第二行）。

**被否决的三个方案**（记录原因，避免反复）：

| 方案 | 为什么否决 |
|---|---|
| 把 `ItemsPanel` 的 `Orientation` 改成 `Horizontal`，再 append 我们的按钮 | ① 横向 `StackPanel` 用**无限宽度**测量子元素，`HorizontalAlignment` 完全失效 → 还是无法右对齐；② 原行的 container 会从"撑满 358"缩到"内容宽度"，`更多音量设置` 的 hover 高亮跟着缩水（视觉回归） |
| 往 `ItemsControl.Items()` append 一个含 `Grid` 的项 | 还是新起一行 —— 问题在 panel 的排布方向，不在项的内容 |
| 用 `TransformToVisual` 测量模型按钮的内边距，再据此设 `Margin.Left` | ✅ **实测失败**：注入发生在布局之前，`ActualWidth()` 是 0，算出 354px 的荒谬边距；且之后 `SizeChanged` 不再触发，错位被永久固化（UIA 矩形变成 `∞`）。详见 §5.5 |

**采用方案：把原行"包"进一个两列 `Grid`**

```text
改前：  ItemsPanel(StackPanel, Vertical)
            └─ ContentPresenter  ← item container（原行，宽度 = 358）
                 └─ ... → Button[更多音量设置]

改后：  ItemsPanel(StackPanel, Vertical)
            └─ Grid  ← 新加的两列容器
                 ├─ Col0 (*)    : ContentPresenter  ← 原行（宽度仍 = 358 - 按钮宽 - 边距）
                 │                   └─ ... → Button[更多音量设置]   位置不变
                 └─ Col1 (Auto) : Button[我们的入口]                 右对齐
```

**精确操作序列**（顺序不能换）：

```cpp
// ① 建 Grid，两列
WUXC::Grid grid;
auto starCol = ColumnDefinition(); starCol.Width(GridLength{ 1.0, GridUnitType::Star });  // 列 0
auto autoCol = ColumnDefinition(); autoCol.Width(GridLength{ 1.0, GridUnitType::Auto });  // 列 1
grid.ColumnDefinitions().Append(starCol);
grid.ColumnDefinitions().Append(autoCol);

// ② 先把原行从 ItemsPanel 取出（★ 必须先 IndexOf 再 RemoveAt，见 §4.3 坑 8）
uint32_t holderIndex = 0;
if (!panel.Children().IndexOf(holder, holderIndex)) return false;   // 不在 panel 里 → 放弃走兜底
panel.Children().RemoveAt(holderIndex);

// ③ 原行放进列 0；我们的按钮放进列 1
WUXC::Grid::SetColumn(holder, 0);
grid.Children().Append(holder);
WUXC::Grid::SetColumn(btn, 1);
btn.HorizontalAlignment(WUX::HorizontalAlignment::Right);
btn.VerticalAlignment(WUX::VerticalAlignment::Center);
grid.Children().Append(btn);

// ④ Grid 放回 ItemsPanel
panel.Children().Append(grid);
```

| 设计点 | 为什么 |
|---|---|
| ⭐ **列 0 必须是 `Star` 而不是 `Auto`** | `Auto` 列测量时给无限宽度 → 原行缩到内容宽度 → `更多音量设置` 的 hover 高亮缩水（视觉回归）。`Star` 让原行保留剩余全部宽度，**位置与外观与注入前完全一致**（✅ 实测：注入后它仍是 `(2193,1420) 94x40`） |
| 为什么把原行"取出再放回"而不是"往 panel append 第二个孩子" | panel 是纵向的，直接 append 就是第二行 |
| 右侧边距 `4` | ✅ 实测：底栏左边距也是 4（底栏 x=2189，模型按钮 x=2193）→ 取 4 得到对称。结果：我们的按钮右边缘 2543 = 2547-4 |
| 垂直对齐 `Center` | ✅ 实测：模型按钮 `(2193,1420) 94x40` 中心 y=1440；我们的按钮 `(2472,1419) 71x40` 中心 y=1439（差 1px 是取整） |
| 幂等 | ⛔ **不能用"记住 Footer 指针"**。早期写法是 `g_lastInjectedFooter == get_abi(footer)`，理由是"面板每次打开重建元素、句柄会变，所以不会误判" —— ❌ **这个理由已被实测证伪**：元素确实会重建，但**分配器可能把同一地址复用给新的 Footer**，于是旧守卫把新树误判成处理过的，**按钮被静默跳过**（实测日志里两次 `Footer appeared` 拿到同一个 handle）。✅ 正确做法：**按内容判定** —— 在 Footer 子树里找有没有 `AutomationProperties.AutomationId == "VmExtEntry"` 的按钮，有就跳过。这是自证的，且能自愈"按钮被别的东西移除"的情况（§5.4 L3） |

**✅ 实测的最终几何（连续两轮完整 cycle 复现一致）**：

| 元素 | 屏幕矩形 | 说明 |
|---|---|---|
| `Footer` | `(2189,1417) 358x48` | 高度仍 48 → **没有第二行** |
| `更多音量设置` | `(2193,1420) 94x40` | 与注入前完全一致 |
| 我们的按钮 | `(2472,1419) 71x40` | 同行、右边缘 2543、高 40 与模型一致 |

### 5.3 样式继承算法

**目标**：让注入的按钮与「更多音量设置」在**视觉上无差别**（尤其 hover/press 高亮框大小）。

**复制清单（`BuildTestLinkButton`）**：

| 属性 | 来源 | 为什么必须复制 |
|---|---|---|
| `Style` | `model.Style()`（非 null 才设） | 底栏按钮的模板/圆角/间距大多在 style 里 |
| `MinWidth` / `MinHeight` | model | 防止不同内容的按钮被压得更小 |
| `Padding` | model | 影响点击区与文字间距 |
| `CornerRadius` | model | 悬停高亮的圆角 |
| `FontSize` / `FontWeight` | model | 字形一致 |
| `HorizontalContentAlignment` / `VerticalContentAlignment` | model | 文字在按钮内的对齐 |
| `Foreground` | model | 文字颜色（含浅色/深色主题差异） |
| ⭐ **`Height`** | **见下方规则** | **决定 hover 高亮框的高度** |

**不复制、而是显式设定**的属性（复制了反而错）：

| 属性 | 我们设成 | 原因 |
|---|---|---|
| `Width` | **不设** | 我们的文字长度与模型不同，设死宽度会截断或留白 |
| `HorizontalAlignment` | `Right` | 这是整个需求的目标 |
| `VerticalAlignment` | `Center` | 与模型在行内垂直居中一致 |
| `Margin` | `{8, 0, 4, 0}` | 左 8 是"与模型保持间距"的最小值；右 4 见 §5.2 |
| `Background` | **不设** | 交给 `Style` / 主题资源，硬设会破坏 hover/press 变色 |

**⭐ `Height` 的规则（这是本算法里最容易出错的一处）**

```cpp
// 读显式属性，绝不"测量"
double h = model.Height();          // 若未设置，XAML 的 double DP 是 NaN
if (!(h > 0)) h = model.MinHeight();
if (!(h > 0)) h = 40.0;             // 兜底常量：✅ 本机实测模型按钮渲染高度就是 40
btn.Height(h);
btn.MinHeight(h);
```

✅ **实测依据**：模型按钮的 `Height` 是**显式 `40.0`**、`MinHeight` 是 `0.0`。所以这一行能在**注入当刻**就把高度定死，**完全不依赖布局时机**：

```
model metrics: Height=40.0 MinHeight=0.0 ActualH=40.0 Style=yes
-> TestLink height forced to 40.0
```

| 反面做法 | 为什么不行 |
|---|---|
| 读 `model.ActualHeight()` | 注入发生在布局之前 → 拿到 0 → 高度错（而且 0 会让按钮不可见） |
| 用 `SizeChanged` 延后设定 | 面板树可能已经布局完成，事件不再触发 → 永不生效 |
| 干脆不设高度，靠 `Style` | ✅ 实测：不设时渲染成 **71x30**，与模型的 40 不一致 → **hover 高亮框明显偏小** |

> **优先级**：显式属性（`Height`/`MinHeight`）**永远优先于测量值**（`ActualWidth`/`ActualHeight`）。这是本项目的通用原则，见 §5.5。

### 5.4 幂等与去重（三层）

**为什么重要**：`Footer` 的 Add 事件在一次面板打开里可能出现多次；App 重启、用户连点"重新注入"也都会再注入一次 Launcher。任何一层缺失都会出现**两个按钮**。

| 层 | 位置 | 机制 | 覆盖的场景 |
|---|---|---|---|
| **L1** | C# `InjectionOrchestrator.TryInjectAsync` | 注入前 `TapProbe.IsTapAlive(pid)`，已就绪就跳过 | App 重启 / 重复点"重新注入" |
| **L2** | 原生 TAP `SetSite` | 命名互斥体 `Local\VmExt.Tap.Singleton.<pid>`；`ERROR_ALREADY_EXISTS` → 休眠 | L1 失效（多端点会话、多个 Launcher 竞态） |
| **L3** | 原生 TAP `InjectIntoFooter` | **按内容判定**：在 Footer 子树里找 `AutomationProperties.AutomationId == "VmExtEntry"`，找不到才注入 | 同一个 `Footer` 的 Add 事件被重复投递；以及"树被重建" |

> ⛔ **L3 为什么不能用"记住 Footer 指针"**（原设计，已实测证伪）
>
> 原写法是 `static void* g_lastInjectedFooter; if (g_lastInjectedFooter == winrt::get_abi(footer)) return;`，
> 文档原话是"面板每次打开重建元素、句柄会变，所以不会误判"。**前半句对，后半句错**：
>
> * 元素确实会重建（这一点早前已实测）；
> * 但**分配器会把同一个地址复用给新的 `Footer`** —— ✅ 实测抓到过：两次面板打开，
>   `Footer appeared` 拿到**同一个 handle**，于是守卫把新树误判成"已处理"，
>   **按钮被静默跳过**（日志里连一行都没有，因为当时是裸 `return`）。
>
> 这是"用**身份**（指针地址）表达**状态**"的经典错误：地址不是身份，它只是"当时恰好没被占用"。
> 而且它的失效是**静默**的 —— 表现成"有时面板打开没有按钮"，归因极其困难。
>
> ✅ **改法**：让判据**自证** —— 直接问"这个 Footer 里有没有我的按钮"。
> 带上 `AutomationId="VmExtEntry"` 标记后，这个检查既准确又可读，还顺带自愈
> "按钮被 XAML 重新模板化移除"的情况（指针守卫在这种情况下反而会永久阻断再注入）。
>
> 教训可推广：**幂等判定优先用"结果是否存在"，而不是"我记不记得做过"。**
> L2 的互斥体之所以可靠，正是因为它由**内核对象**做仲裁（有明确的所有权），而不是靠我们自己记地址。

**⚠️ L1 的一个边界**：`TapProbe` 只认"TAP 已就绪"。如果 TAP 已经加载但**尚未完成 SetSite**（互斥体还没创建的窗口期，毫秒级），L1 会判定"没有 TAP"而再注入一次 —— 此时 L2 兜住（第一个创建互斥体的赢）。

**为什么用"谁先创建互斥体谁赢"而不是"谁后谁赢"**：后到的实例难以判断先到的实例状态；先到者已经开始工作，让后到者休眠是唯一安全的选择。

### 5.5 布局时机不变量（全部由踩坑换来）

**不变量 I1**：
> **任何依赖 `ActualWidth` / `ActualHeight` 的取值，必须先判 `> 0`。** 为真才使用；否则走"显式属性"或"延后重试"路径。

**违反 I1 的实测后果（完整复盘）**：

```text
想法：用 TransformToVisual 测量模型按钮相对 ItemsPanel 的位置，据此算右边距 —— 更"优雅"
实际：注入发生在布局之前 → model.ActualWidth() == 0
      → inset = 358 - (4 + 0) = 354  →  btn.Margin { 8, 0, 354, 0 }
      → 按钮被推出底栏
      → 且 panel.SizeChanged 不再触发（树早就布局完了，尺寸不变）
      → 错位被永久固化：UIA 读到的矩形变成 ∞
      → 验证脚本 footer-map.ps1 在 [int] 转换时抛异常，把真实症状掩盖成"脚本挂了"
```

| 教训 | 落地规则 |
|---|---|
| 布局前测量会拿到 0 | 先判 `> 0`（I1） |
| 定了错的东西不一定有第二次机会 | 能一次定死就用显式属性（`Height`） |
| 验证脚本本身也会成为盲区 | 矩形转换必须容忍 NaN/∞（✅ 已修 `footer-map.ps1`） |

**不变量 I2**：
> **需要在 UI 线程上做、但当前不在 UI 线程的工作，必须 `CoreDispatcher::RunAsync` 排队，不得 `Sleep` 等待。**

实现范式（`TryInjectLater` 就是这么写的）：

```cpp
WUC::CoreDispatcher d = nullptr;
try { d = footer.Dispatcher(); } catch (...) {}
if (!d) { InjectIntoFooter(footer); return; }        // 拿不到 dispatcher：直接试一次

if (d.HasThreadAccess()) {
    attemptFn();                                     // 已经在 UI 线程：别再绕一圈
} else {
    d.RunAsync(WUC::CoreDispatcherPriority::Normal, attemptFn);
}
```

**不变量 I3**：
> **注入/改动 XAML 之后，不要假设布局已经更新。** 需要"改动后的几何"时，用自己的验证工具（UIA）从**另一个进程**读，不要在 TAP 里读完立刻自证。

理由：TAP 读的是 XAML 的已计算布局，`Children().Append` 之后要等一次 layout pass。跨进程用 UIA 读反而更可靠，也正是我们做验收的方式（§8.3）。

---

## 6. 状态机与错误处理

### 6.1 状态与用户可见表现的映射

`OrchestratorState`（定义见 §3.10）必须**直接映射**到托盘菜单显示的文字。**不要把内部状态名直接给用户看。**

| 状态 | 托盘显示 | 用户可做的动作 | 是否算故障 |
|---|---|---|---|
| `Stopped` | 「未运行」 | 启动 | 否 |
| `LocatingRuntime` | 「正在检查运行环境…」 | — | 否 |
| `WaitingForShellHost` | 「等待系统外壳就绪」 | 退出 | 否（正常：ShellHost 还没起来） |
| `Injecting` | 「正在注入…」 | — | 否 |
| `WaitingForTap` | 「正在激活…」 | — | 否 |
| `Ready` | 「已注入 —— 打开音量面板即可看到按钮」 | 禁用 / 重新注入 / 退出 | 否 |
| `Faulted` | 「故障：<LastError>」 | 打开诊断信息 / 打开日志 / 退出 | **是** |

**`Ready` 的措辞要克制**：目前没有"按钮已成功注入"的回传信号（§3.13），所以只能说"已注入"，**不能**说"按钮已添加"。见 §10.4 的改进项。

### 6.2 失败降级矩阵

**核心原则：每一层失败都必须是一个"用户能理解、系统仍可用"的状态，而不是一个异常。**

| 失败点 | 检测方式 | 降级行为 | 用户可见 |
|---|---|---|---|
| 找不到 `xamldiagnostics.dll` | `DiagnosticsRuntimeLocator.Locate()` 返回 null | 进 `Faulted`，**不注入** | 「故障：找不到 XAML 诊断运行时」+ 搜索详情 |
| 安装路径含 `;` / `=` | `ConfigStore.BuildInitData` 的 `EnsureSafe` 抛错（启动自检时） | 进 `Faulted` | 「故障：安装路径含保留字符」+ 建议换路径 |
| `OpenProcess` 失败（权限） | `InjectionResult.Error == OpenProcessFailed` | 重试 3 次后进 `Faulted` | 「故障：无法打开系统外壳进程」 |
| 目标进程不存在 | `Error == TargetGone` | **不算故障**，回 `WaitingForShellHost` | 无感 |
| `LoadLibraryW` 返回 NULL | `Error == RemoteLoadReturnedNull` | 重试（退避），并检查 DLL 依赖 | 长时间不成功则 `Faulted` |
| 注入成功但 TAP 未就绪 | `WaitUntilAlive` 超时 | 重试；连续 3 次失败 → `Faulted` | 「故障：注入未生效」+ 日志提示 |
| TAP 在但有重复实例 | 互斥体已存在 | TAP 自己休眠（L2，§5.4） | 无感 |
| `Footer` 一直没出现 | TAP 日志无命中 | TAP 什么都不做 | 无感（可能用户就没打开面板） |
| 页面元素结构变了（找不到模型按钮 / ItemsPanel） | `TryInjectLater` 20 次失败 / `InjectIntoRow` 返回 false | 记 Warn；走兜底（第二行）或放弃 | 按钮位置不佳或没有按钮 |
| ini 读不到 / `enabled` 缺失 | TAP 内 `GetPrivateProfileStringW` 返回默认 | 视为 `enabled=0`（**保守：什么都不做**） | 无按钮 |
| 管道写入失败 | `CreateFileW`/`WriteFile` 失败 | 记日志，**不重试**、不弹窗 | 点击无反应（日志里有原因） |
| TAP 里发生 C++ 异常 | `catch (...)` | 记日志 + 返回 `S_OK` | 无感 |
| ShellHost 崩溃/重启 | Watcher 的 `ShellHostExited` | 回 `WaitingForShellHost` → 自动重注入 | 短暂无感 |

**"保守默认"的两处**（值得单独强调，都是刻意选择）：

1. **ini 缺失或解析失败 → 视为禁用**。宁可没有按钮，也不要因为配置读失败而在系统面板上留下奇怪的东西。
2. **协议版本不认识 → 只记日志不注入**。`ver` 不匹配说明两侧二进制版本错配，此时任何"尽力而为"都可能造成不可预期后果。

### 6.3 重试与退避策略

| 场景 | 退避 | 上限 | 说明 |
|---|---|---|---|
| 注入失败（可重试类） | 1s → 2s → 4s → 8s → 16s → 30s（封顶） | 同一 ShellHost 生命周期内**无限重试** | ShellHost 存活期间反复失败通常意味着环境问题不会自愈，但无限重试是安全的（每次失败都是毫秒级，且不触碰 shell 代码） |
| `TargetGone` | 不重试 | — | 等新的 ShellHost |
| 注入成功但 TAP 未就绪 | 每 500ms 检查 `IsTapAlive` | 10s | 超时后**重新注入**（而不是重等），因为"注入成功"可能只是 `DllMain` 上了但 Worker 卡住了 |
| 健康检查发现 TAP 消失 | 立即强制重注入 | 连续 3 次 → `Faulted` | 见 §3.11 |
| 用户点「重新注入」 | 立即 | 每次点击一次 | 有 L1 幂等，重复点无害 |

⛔ **禁止的做法**：不要对 `Injecting` 状态加"全局互斥、失败就长时间冷却"。ShellHost 可能在几秒内被重建多次（用户折腾 explorer），冷却会让产品在这些时刻静默失效。

### 6.4 "绝不崩 shell"的规则（Code Review Checklist）

**这是本项目的最高优先级约束。ShellHost 崩了会影响任务栏/开始菜单/快速设置，而这是用户的日常环境。**

提交原生代码前，逐条对照：

| # | 检查项 | 为什么 |
|---|---|---|
| 1 | 每个 COM 导出方法（`DllGetClassObject`/`DllCanUnloadNow`）整体被 `try/catch(...)` 包住？ | 异常穿过 COM 边界 = 未定义行为 = 可能崩 |
| 2 | `OnVisualTreeChange` 整体被 `try/catch(...)` 包住，且**总是返回 `S_OK`**？ | 返回失败会让 XAML 认为 TAP 异常 |
| 3 | `SetSite` 在任一 QI 失败时也返回 `S_OK`？ | 同上 |
| 4 | `DllMain` 里除了 `DisableThreadLibraryCalls` + `CreateThread` 什么都没做？ | loader lock 死锁 |
| 5 | 有**任何** `Sleep` 出现在 UI 线程路径上（`OnVisualTreeChange` → `TryInjectLater` → `InjectIntoFooter` → 点击处理器）？ | UI 线程阻塞 = 面板卡死 |
| 6 | 所有 C++/WinRT 调用都被 try/catch 包住？ | cppwinrt 的失败以异常形式抛出（如 `hresult_error`） |
| 7 | lambda 捕获的是**值**（`FrameworkElement`）而不是引用/裸指针？ | 面板关闭后捕获的引用悬空 |
| 8 | 有对 `VisualElement::Type`/`Name` 做 `SysFreeString`？ | 所有权在框架，**不要释放** |
| 9 | `CreateProcessW` 的命令行缓冲是**可写**的？ | 字面量可能被改写 → 访问违规 |
| 10 | `CreateProcessW` 的两个句柄都关闭了？ | 句柄泄漏 |
| 11 | 点击动作里的管道 IO 不在 UI 线程？ | 服务端没起来会卡住面板 |
| 12 | 所有 `BSTR` / `IInspectable*` 的引用计数配对正确？ | 泄漏或提前释放 → 崩 |
| 13 | 对未知 `Name` / 未知 mutation 类型 / `nullptr` 参数都有早退？ | 防御性编程 |
| 14 | 日志缓冲区有界（固定大小 + `_TRUNCATE`）？ | 日志本身不能成为崩溃源 |
| 15 | 拿不到 `ItemsPresenter` / `IndexOf` 失败时返回 `false` 走兜底，而不是继续操作？ | 避免对错误对象操作 |

**验收底线**：注入后反复开关面板 20 次、反复点击按钮 20 次、注入状态下重启 explorer 3 次 —— 全程 ShellHost 不崩、任务栏不闪、无异常对话框。

---

## 7. 构建、打包、部署

### 7.1 安装后的目录布局

**全部装在用户目录，全程不需要管理员权限**（这是刻意的设计约束）。

```text
%LOCALAPPDATA%\VolumeMixerExtender\
├── VmExt.App.exe                     ← 托盘宿主（入口）
├── VmExt.Core.dll
├── VmExt.Interop.dll
├── VmExt.Service.dll
├── VmExt.App.runtimeconfig.json
├── native\
│   ├── VmExt.Launcher.dll            ← 注入用
│   ├── VmExt.Tap.dll                 ← TAP
│   ├── VmExt.Launcher.ini            ← 注入前由 App 写
│   └── VmExt.Launcher.ini.sample
├── config\
│   ├── appsettings.json
│   └── vmext-tap.ini                 ← TAP 每次注入前重读
├── logs\
│   ├── app-YYYYMMDD.log
│   ├── launcher.log
│   └── tap.log
└── tools\
    ├── install.ps1
    └── uninstall.ps1
```

⛔ **路径里不能有 `;` 或 `=`**（§2.2 约束）。`%LOCALAPPDATA%` 通常安全，但用户名可能含特殊字符 —— 启动自检必须验证。

**`xamldiagnostics.dll` 不在这个目录里**，它是 SDK 的一部分，通过绝对路径引用（§3.8）。

### 7.2 C# 构建

```powershell
# 开发构建
dotnet build VolumeMixerExtender.sln -c Release

# 发布（推荐 self-contained：免去用户装 .NET Desktop Runtime 的前置要求）
dotnet publish src\VmExt.App\VmExt.App.csproj `
    -c Release -r win-x64 --self-contained true `
    -p:PublishSingleFile=false `
    -p:PublishTrimmed=false `
    -o out\app
```

| 参数 | 为什么 |
|---|---|
| `--self-contained true` | 用户机器上不必有 .NET 8 Desktop Runtime。体积换可靠性，对个人工具是划算的 |
| `PublishSingleFile=false` | 单文件打包会把原生 DLL 也解包到临时目录，而我们要用**固定绝对路径**引用它们（L1/L2 契约依赖路径稳定）。**必须关掉** |
| `PublishTrimmed=false` | WinForms + 反射（JSON 序列化）对裁剪不友好，且体积不是问题 |

⚠️ 注意：`VmExt.App.exe` 的**所在目录**必须与 `native\`、`config\` 同级 —— 安装脚本负责摆好，程序里用 `AppContext.BaseDirectory` 定位，**不要**依赖 `Environment.CurrentDirectory`（从快捷方式启动时它可能是别的地方）。

### 7.3 原生构建（`vcxproj`）

**为什么不用 PoC 的 `build.cmd`**：命令行脚本无法被 IDE 索引、无法产生符号供调试、也无法集成到解决方案的一次构建里。
**保留 `build.cmd` 的价值**：在没有 VS 的机器上应急构建，以及作为"最小构建参数"的参考。

**两个 vcxproj 的公共属性**：

| 属性 | 值 | 说明 |
|---|---|---|
| 配置类型 | 动态库 (.dll) | |
| C++ 标准 | `/std:c++17` | cppwinrt 需要 |
| 运行库 | **多线程 (/MT)** | 免 VC++ Redistributable（§4.3 坑 14） |
| 字符集 | Unicode | |
| 平台 | x64 only | 与 ShellHost 位宽一致 |
| **源码字符集** | ⛔ **`/utf-8`** | 源码含中文注释时**必须**：无 BOM 的 UTF-8 会被按系统 ACP 解读，某中文字尾字节 `0x5C` 会吃掉字符串收尾引号 → `C2001 常量中有换行符`（§4.3 坑 17）。PoC 已实测踩过 |
| 警告级别 | `/W3` 起 | |
| `LinkIncremental` | **false** | 避免增量链接产物与"已加载 DLL"的锁冲突 |
| 输出目录 | `..\..\out\native\` | 与 C# 的 publish 输出合并 |

**`VmExt.Tap` 额外的必须项**：

| 项 | 值 | 为什么 |
|---|---|---|
| 附加包含目录 | `$(WindowsSdkDir)Include\$(WindowsSDKVersion)cppwinrt` | cppwinrt 头（`winrt\*.h`）不在默认包含路径里 |
| 附加依赖项 | `WindowsApp.lib;ole32.lib` | cppwinrt 的 XAML 激活需要 `WindowsApp.lib` |
| 导出 | `/EXPORT:DllGetClassObject /EXPORT:DllCanUnloadNow` | ⛔ 不能用 `__declspec(dllexport)`（§4.3 坑 3） |
| 预处理器 | `_WIN32_WINNT=0x0A00;UNICODE;_UNICODE` | Win10+ API |
| 生成调试信息 | 是（`/Zi` + `ProgramDatabase`） | 出问题时要能挂调试器到 ShellHost |

**`VmExt.Launcher` 额外的必须项**：只需 `ole32.lib`；不需要 cppwinrt、不需要 `WindowsApp.lib`。

**命令行等价构建（应急用）**：

```cmd
:: 必须在同一个 cmd 进程里 call + 编译
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
set CPPWINRT=%WindowsSdkDir%Include\%WindowsSDKVersion%cppwinrt

cl /nologo /std:c++17 /EHsc /O2 /MT /LD /W3 /DUNICODE /D_UNICODE ^
   launcher.cpp /link /out:VmExt.Launcher.dll /DLL ole32.lib

cl /nologo /std:c++17 /EHsc /O2 /MT /LD /W3 /DUNICODE /D_UNICODE /I"%CPPWINRT%" ^
   tap.cpp /link /out:VmExt.Tap.dll /DLL WindowsApp.lib ole32.lib ^
   /EXPORT:DllGetClassObject /EXPORT:DllCanUnloadNow
```

**构建后自检（放进构建脚本，防止"编出来了但导出名不对"）**：

```powershell
# 必须正好出现这两个导出
dumpbin /exports out\native\VmExt.Tap.dll | Select-String 'DllGetClassObject|DllCanUnloadNow'
# 必须不出现对 VCRUNTIME*.dll 的依赖（确认 /MT 生效）
dumpbin /dependents out\native\VmExt.Tap.dll
```

### 7.4 打包

| 项 | 做法 |
|---|---|
| 格式 | 一个 zip（个人工具，不需要 MSI/WiX 的复杂度） |
| 内容 | `out\app\**`（含 `native\`）+ `config\` 模板 + `docs\` |
| 签名 | ⚠️ 不签名。**必须**在文档与 README 里写清"未签名，Defender 可能报"（§7.5 第 4 步） |
| 版本号 | 单一来源：`Directory.Build.props` 里的 `<Version>`，同时写进 `appsettings.json` 的注释与诊断信息输出 |

### 7.5 安装 / 卸载

**`tools\install.ps1` 的步骤**（幂等，可重复运行）：

```text
1. 解压到 %LOCALAPPDATA%\VolumeMixerExtender\（覆盖）
2. ★ 覆盖前必须先停进程：
     - 停止正在运行的 VmExt.App.exe（否则 App.exe 文件被锁）
     - ★ 提示用户：如果 native\*.dll 被锁（ShellHost 持有），要么重启 explorer，
       要么"新文件名"策略（见下方说明）
3. 校验自检项（§7.6 全部）；任一失败就明确报错并给出修复建议
4. 注册开机自启：HKCU\Software\Microsoft\Windows\CurrentVersion\Run
     "VolumeMixerExtender" = "%LOCALAPPDATA%\VolumeMixerExtender\VmExt.App.exe"
5. 提示（不自动做）：Defender 排除目录
     Add-MpPreference -ExclusionPath "%LOCALAPPDATA%\VolumeMixerExtender"
     （需要管理员；脚本只输出这行命令让用户自己决定）
6. 启动 VmExt.App.exe
```

⛔ **第 2 步的"DLL 被锁"是安装体验上最烦人的问题**。原生 DLL 被 ShellHost 加载后无法覆盖（`LNK1104` 的直接原因）。
两个可选对策，**推荐第一个**：

| 对策 | 说明 |
|---|---|
| **重启 ShellHost 后再替换**（推荐） | 安装脚本可以 `Stop-Process ShellHost`（sihost 会重新拉起）→ App 的 Watcher 会自动重新注入新的 DLL。用户感知只是"快速设置闪了一下" |
| 版本化文件名 | 把 DLL 写成 `VmExt.Tap.1.0.0.0.dll` 并让 ini 指向新文件。**缺点**：旧版本文件永远留在磁盘上（因为无法删除），目录会越来越脏 |

**`tools\uninstall.ps1` 的步骤**：

```text
1. 停 VmExt.App.exe，删除 HKCU\...\Run 的项
2. ★ 提示：native\*.dll 仍被 ShellHost 持有，无法立即删除
     - 让用户重启 explorer（或注销），再手工删除目录
     - 或脚本提供 -RestartShell 开关：停 ShellHost → 删文件
3. 保留日志目录（除非 -PurgeLogs），方便事后排查
4. 打印"剩余未删除的文件清单"（诚实，不要假装卸载干净了）
```

### 7.6 启动自检清单

`VmExt.App` 启动时必须逐项做完这些检查，**任何一项失败都要给出"能照着修的"错误信息**，而不是笼统的"初始化失败"：

| # | 检查 | 失败信息模板 |
|---|---|---|
| 1 | 安装路径不含 `;` `=` | 「安装路径含保留字符 `;` 或 `=`，请把程序移到只含普通字符的目录」 |
| 2 | `native\VmExt.Launcher.dll` 存在 | 「缺少 `<绝对路径>`，请重新安装」 |
| 3 | `native\VmExt.Tap.dll` 存在 | 同上 |
| 4 | 两个 DLL 的位宽是 x64 | 「原生组件非 x64，与系统外壳不匹配」 |
| 5 | `xamldiagnostics.dll` 能定位 | 「找不到 XAML 诊断运行时。搜索过：<逐条列出>。修复：安装 Windows SDK，或在配置里显式指定路径」 |
| 6 | 本进程 session != 0 | 「当前运行在不支持会话（Session 0），无法注入」 |
| 7 | 单实例互斥体 | 「已在运行」→ 激活已有实例的窗口 |
| 8 | 能写日志目录 | 「日志目录不可写：<路径>」→ 降级为无日志继续（不是致命） |
| 9 | `appsettings.json` 可解析 | 「配置格式错误：<JSON 错误位置>」→ 致命 |

---

## 8. 验证与验收

### 8.1 分级验证（每级都有可判定的通过标准）

**必须按顺序做**。任何一级不通过，不要往下做 —— 后面的失败会被前面的问题掩盖。

| 级 | 目的 | 步骤 | 通过标准 | 不通过说明什么 |
|---|---|---|---|---|
| **V0** | 注入不加任何东西也安全 | 注入 `VmExt.Launcher.dll`，但 `enabled=0` | ShellHost 不崩；反复开关面板 20 次无异常；`launcher.log`/`tap.log` 出现 | 注入方式或 DLL 依赖有问题 |
| **V1** | TAP 能连上诊断会话 | `enabled=0`，看 `tap.log` | 出现 `vcxtap loaded` → `Tap::SetSite` → `QI IXamlDiagnostics hr=0x0` → `QI IVisualTreeService hr=0x0` → `AdviseVisualTreeChange hr=0x0`，随后有大量 `ADD` | 端点名/CLSID/TAP 加载有问题 |
| **V2** | ⭐ **配置下发契约成立**（直投主 + initData 面包屑） | `enabled=0`，配置串带一个可识别的 `cfg=` 值 | ① `tap.log` 打印 `直投通道: 有 len=N fnv1a64=H1`，且 `H1` 与 Launcher 日志里的哈希**逐位相同**；② 打印出解析到的 `cfg`，与 App 写的一致；③ ≤259 时 `initData : len=N fnv1a64=H1` 也一致 | ① 直投没到 → 导出名/签名/顺序（§2.2.1）② `cfg` 没解析出来 → 配置串格式 ③ initData 为空但长度 ≤259 → **看 TAP 的反向哨兵日志**（§4.2.3 第 4c 步） |
| **V2.1** | initData 上限行为符合预期 | 配置串刻意做成 >259 字符 | Launcher 警告日志出现"面包屑会被静默丢弃"；TAP 打印"initData 为空属**预期**"；**按钮照常注入**、几何不变 | 若按钮没出现，说明配置**没有**走直投通道（超限时被一起丢了） |
| **V3** | 配置与编码 | `enabled=1`，`text` 设为中文（如「音量合成器」） | 按钮文字正确显示，不是乱码 | INI 编码问题 → 用 **UTF-16LE + BOM** 写（§4.3 坑 13）。✅ 编码这一层已在 T2 实测钉死，所以 V3 若仍乱码，就是显示层问题而不是编码 |
| **V4** | 点击动作（`exec` / `pipe`） | ① `action=exec` 指向一个可见程序 ② `action=pipe`，App 侧服务端在跑 ③ 服务端**不在**时也要点一次 | ① 程序被拉起 ② App 收到 `CLICK <id> <ts>` ③ **两次点击时面板都不卡顿**（UIA `Invoke` 往返 < 120ms） | pipe 链路或 UI 线程阻塞（§4.2.5）。✅ 三项均已在 PoC 验证（`08-pipe-action-chain.md`） || **V5** | 生效时机 | 干净 shell → 注入 → 打开面板 | 第一次打开面板就有按钮 | 若"第一次没有、第二次才有"，说明 `TryInjectLater` 的重试次数不够或事件时序理解有误 |
| **V6** | 幂等 | 连续调 3 次「重新注入」，然后开面板 | 面板上**只有一个**按钮；`tap.log` 里能看到重复实例被识别为 `dormant` | L1/L2 去重失效（§5.4） |
| **V7** | 生命周期 | 注入就绪后重启 explorer（模拟 shell 重启） | App 日志出现 `ShellHostExited` → `ShellHostStarted` → 注入 → `TAP 就绪`；再次开面板按钮仍在 | Watcher 或编排器有问题（§3.6/§3.10） |
| **V8** | 压力 | 反复注入/重启 shell 10 轮；每次开面板点按钮 20 次 | 无崩溃、无句柄泄漏（用 Process Explorer 看 App 与 ShellHost 的句柄数不持续增长） | 句柄/引用计数泄漏 |

**⚠️ 当前状态**：V0/V1/V2/V2.1/V5 已由 PoC 等价验证 ✅（V2 的证据见 `verified-after-injection/06-initdata-channel-limit.md`，含三次独立复现）；V3/V4/V6/V7/V8 **全部未验证**，是实现阶段的主要风险。

### 8.2 验收测试用例表

| ID | 名称 | 前置 | 步骤 | 期望结果 | 可自动化 |
|---|---|---|---|---|---|
| AT-01 | 按钮出现在同一行右侧 | V5 通过 | 打开音量面板；用 UIA 读 `Footer` 与全部 `Button` 的屏幕矩形 | `更多音量设置` 与我们的按钮 **y 区间重叠**；我们的按钮右边缘距 `Footer` 右边缘 **≤ 8px**；`Footer` 高度 **48**（不是 78） | ✅ `recon/footer-map.ps1` |
| AT-02 | 按钮文字正确 | V3 通过 | 同上，读按钮 `Name` | 等于 `vmext-tap.ini` 里的 `entry1.text` | ✅ 同上 |
| AT-03 | 点击执行动作 | — | UIA `InvokePattern` 调用按钮 | `action=exec` → 目标进程出现；`action=pipe` → App 日志出现 `EntryClicked` | ✅ `recon/click-testlink.ps1` |
| AT-04 | 与模型按钮视觉一致 | AT-01 通过 | 比两个按钮的高度 | 高度相等（都是 40） | ✅ 同上 |
| AT-05 | 关闭面板无残留 | — | 关闭再打开面板 5 次 | 每次都只有一个按钮（不是每开一次多一个） | ✅ 脚本循环 |
| AT-06 | `enabled=0` 时不注入 | — | 设 `enabled=0`，重开面板 | 没有按钮；`tap.log` 有「disabled，skipping」 | ✅ |
| AT-07 | shell 重启后自愈 | — | 注入就绪 → 重启 explorer → 等 15s → 开面板 | 按钮仍在 | ⚠️ 半自动（需要重启 shell） |
| AT-08 | 面板已打开时注入 | — | 先开面板 → 注入 → 关面板 → 再开面板 | **再一次打开时**出现按钮（本方案不承诺"立刻出现"） | ✅ |
| AT-09 | 稳态 CPU 占用 | V7 通过 | 注入就绪后静置 5 分钟，看 App 与 ShellHost 的 CPU | App **< 0.1%**；ShellHost 相对基线无可见增长 | ⚠️ 需要人工观察 |
| AT-10 | 面板不卡顿 | — | 打开面板后快速滑动音量列表 | 无卡顿（对比未注入时无明显差异） | ❌ 人工 |
| AT-11 | 卸载后无残留 | — | 运行 `uninstall.ps1` → 重启 shell | 面板无按钮；`HKCU\...\Run` 无项 | ⚠️ 半自动 |
| AT-12 | 找不到诊断运行时时的表现 | — | 把 `xamldiagnostics.dll` 路径改成不存在的 | 托盘显示明确故障；**不注入**；诊断信息里列出搜索过的路径 | ✅ |

### 8.3 自动化脚本清单（复用 `docs/poc/scripts/` 的现成工具）

这些脚本是 PoC 阶段写的，**直接可用**，产品的验收脚本应该复用它们的模式：

| 脚本 | 作用 | 用于 |
|---|---|---|
| `docs/poc/scripts/cycle.ps1` | 一轮完整迭代：停 shell → 构建 → 重启 explorer → 注入 → 开面板 → 打日志 + 几何 | 开发期迭代 |
| `docs/poc/scripts/footer-map.ps1` | 用 UIA 打印 `Footer` 与全部 `Button` 的屏幕矩形 | AT-01 / AT-04 / AT-05 |
| `docs/poc/scripts/click-testlink.ps1` | UIA `InvokePattern` 触发按钮并断言目标进程出现 | AT-03 |
| `docs/poc/scripts/shot-footer.ps1` | 截取底栏区域到 PNG（2 倍放大） | 视觉确认 |
| `docs/poc/scripts/parse-tree.ps1` | 从 `tap.log` 重建元素层次并打祖先链 | 定位算法排障 |
| `docs/poc/scripts/qs-panel-probe.ps1` | 打开/定位面板并 dump XAML 元素树 | 所有需要面板的场景 |

⛔ **这些脚本有一个共同的脆弱点**：`ControlCenterWindow` 是 **band=4** 窗口，`EnumWindows`/`FindWindow`/UIA `RootElement` **都看不到它** ✅。
拿 HWND 只能靠 `GetForegroundWindow()` 或 `GetGUIThreadInfo()`。
⇒ **一旦有别的窗口抢走前台，脚本就会报"面板未打开"**（✅ 踩过：注入动作或用户操作都会导致）。
**自动化的正确做法**：先拿到并**缓存面板 HWND**（它是常驻的），后续用 `IsWindowVisible(hwnd)` 判断，而不要每次都依赖前台窗口。

---

## 9. 排障手册

### 9.1 症状 → 原因 → 动作

| 症状 | 最可能的原因 | 动作 |
|---|---|---|
| 托盘显示「等待系统外壳就绪」很久 | ShellHost 没起来（用户很久没开快速设置/开始菜单） | 正常。让用户按 `Win+Ctrl+V` 触发一次 |
| 托盘「已注入」但面板上没有按钮 | ① `enabled=0` ② 时 `Footer` 名字变了 ③ `tap.log` 里 `TryInjectLater` 20 次都失败 | 看 `tap.log` 有没有 `Footer appeared`，再看有没有 `no ItemsPresenter ancestor` / `row wrap failed` |
| 按钮出现在**第二行**，footer 变高 | 走了兜底路径（`InjectIntoRow` 返回 false） | 看 `tap.log` 的 `up[]` 祖先链是否还是 5 层结构；对比 §5.1 的实测链 |
| 按钮跑到面板外面 / UIA 读到 `∞` | 用了布局前的测量值（违反 I1） | 查代码里是否有未判 `>0` 的 `ActualWidth/ActualHeight`（§5.5 的完整复盘） |
| 出现**两个**按钮 | 去重失效（§5.4） | 看 `tap.log` 是否有两个 `vcxtap loaded`；查 `InjectionOrchestrator` 的 L1 检查是否被绕过 |
| 点击没反应 | ① `action=pipe` 但 App 没在跑/管道名不匹配 ② `action=exec` 的命令行不对 ③ 写入管道失败 | 看 `tap.log` 的点击记录与失败 hr；核对 `Constants.PipeName(sessionId)` 与 initData 里的一致 |
| 点击后面板卡住 | 管道 IO 在 UI 线程上（违反 §4.2.5） | 把 IO 移到独立线程或加短超时 |
| 注入报 `OpenProcessFailed` | 目标 IL 变高 / 目标成了 PPL / 目标是别的会话 | 检查 `ShellHostLocator` 的 session 过滤；确认 ShellHost 仍非 PPL（§2.6） |
| 注入报 `RemoteLoadReturnedNull` | DLL 依赖缺失（如 CRT 动态链接）或位宽不符 | `dumpbin /dependents` 确认无 `VCRUNTIME*.dll`；确认 x64 |
| `LNK1104 无法打开 VmExt.Tap.dll` | 构建目标文件正被 ShellHost 加载 | 停 ShellHost（或用 §7.5 的对策） |
| `launcher.log` 里 `hr=0x80070057`（E_INVALIDARG） | 某个参数不被接受：路径不存在、CLSID 不符、initData 不合规 | 逐项核对 `VmExt.Launcher.ini` 里两个路径是否存在；核对 CLSID 字面量 |
| `xamldiagnostics.dll 存在` 但 TAP 没起来 | SDK 的 `xamldiagnostics.dll` 版本与系统 XAML 不兼容 | 换用与被注入 XAML 版本匹配的 SDK（§10.2） |
| `tap.log` 里有 `duplicate TAP, going dormant` | 重复注入（这是 L2 正常工作） | 无需处理；若还出现两个按钮，说明 L3 失效 |
| 面板在注入后自己关了 | ⚠️ 曾观察到的现象，**原因未定**：可能是 `InitializeXamlDiagnosticsEx` 同步遍历整棵 XAML 树卡住 ShellHost UI 线程 → 面板失焦自动关闭；也可能是用户操作 | ❌ **不做支持**：音量浮层不是常驻窗口，本方案在**下次打开**面板时生效即可（§10.4 T4） |

### 9.2 诊断命令集

```powershell
# ---- 1. 目标进程在不在、在哪个会话、TAP 有没有起来 ----
Get-Process ShellHost | Select-Object Id, SessionId, StartTime
$pid2 = (Get-Process ShellHost).Id
Get-Process ShellHost | ForEach-Object { $_.Parent }      # 期望 sihost

# ---- 2. TAP 心跳（命名互斥体）----
#     存在即"TAP 已就绪"
[System.Threading.Mutex]::TryOpenExisting("Local\VmExt.Tap.Singleton.$pid2", [ref]$null)

# ---- 3. 面板与底栏几何（需要面板在前台）----
& .\docs\poc\scripts\footer-map.ps1

# ---- 4. 注入结果与 TAP 行为 ----
Get-Content .\docs\poc\vcxlaunch.log -Tail 30
Get-Content .\docs\poc\vcxtap.log   -Tail 60

# ---- 5. 原生 DLL 的导出与依赖自检 ----
dumpbin /exports   out\native\VmExt.Tap.dll
dumpbin /dependents out\native\VmExt.Tap.dll      # 不应出现 VCRUNTIME*.dll

# ---- 6. 诊断运行时探测 ----
Get-ChildItem "$env:ProgramFiles(x86)\Windows Kits\10\bin\*\x64\XamlDiagnostics\xamldiagnostics.dll" |
    Sort-Object FullName -Descending | Select-Object -First 5 FullName

# ---- 7. INI 是否被正确写出（含编码检查）----
Get-Content .\config\vmext-tap.ini -Encoding UTF8
Format-Hex .\config\vmext-tap.ini | Select-Object -First 2   # 看是否有 BOM
```

### 9.3 日志阅读指南

**判断"注入是否成功"的正确顺序**：

```text
① app-YYYYMMDD.log
     "ShellHostStarted pid=..."        → Watcher 发现了目标
     "TryInject pid=..."               → 开始注入
     "TAP 就绪"                         → ★ 到这一步才叫注入成功

② launcher.log （同一目录）
     "SUCCESS: endpoint=... hr=0x00000000"  → in-proc 入口调用成功
     ← 若这里没有，问题在 Launcher 内部（§4.1.4 的表格）

③ tap.log
     "vcxtap loaded: pid=... stage=..."  → TAP 被 XAML core 加载
     "QI IXamlDiagnostics hr=0x0"        → 拿到诊断接口
     "AdviseVisualTreeChange hr=0x0"     → 开始收事件
     "*** Footer appeared: ..."          → ★ 页面的底栏出现了
     "up[0] ... up[9] ..."               → 祖先链（对照 §5.1）
     "wrapped the row container ..."     → ★ 注入成功
     "align: ..."                        → 兜底路径的对齐计算
     "*** TestLink injected ***"         → 完成
```

**三个"★"是排障的全部关键**：`TAP 就绪` → `Footer appeared` → `wrapped the row container`。
中间断在哪一个，就直接定位到对应章节。

---

## 10. 已知限制、风险与版本兼容

### 10.1 硬限制（⛔ 不可消除，全部是体系结构决定的）

| # | 限制 | 后果 | 应对 |
|---|---|---|---|
| 1 | `InitializeXamlDiagnosticsEx` 是**每进程一次**的诊断初始化，**没有 teardown API** | TAP DLL 一旦加载就跟着 ShellHost 活到进程退出，无法卸载 | "禁用"通过 `enabled=0` 实现（让 TAP 什么都不做）。**不要**试图去找卸载 API，浪费时间 |
| 2 | TAP 从不调用 `UnadviseVisualTreeChange` | 诊断框架持续持有我们的回调引用 ⇒ `DllCanUnloadNow` 永远返回 `S_FALSE` ⇒ TAP 无法卸载 | 同上。**这是刻意的**：既然诊断会话本身反不掉（#1），再花力气反掉 TAP 也没有意义 |
| 3 | 已加载的 DLL 文件被写锁定 | 无法原地升级/卸载（LNK1104） | 停 ShellHost 后替换（§7.5） |
| 4 | 面板是 **band=4** 窗口 | `EnumWindows`/`FindWindow`/UIA `RootElement` 都看不到 | 只用 `GetForegroundWindow()`/`GetGUIThreadInfo()`；自动化里缓存 HWND（§8.3） |
| 5 | XAML 元素实例**每次打开面板都重建** | 无法长期持有元素引用；注入必须每次重做 | 事件驱动（`Footer` 的 Add）+ 每次重注入。**这也意味着"按钮消失"是免费的**，不需要清理逻辑 |
| 6 | ShellHost 是原生非 .NET 进程 | in-proc 两段必须是原生代码（§1.2） | 架构已按此设计 |
| 7 | `WaitForMultipleObjects` 上限 64 个句柄 | 极端情况下 Watcher 数组会满 | 达到 60 时清理并重建数组（§3.6） |

### 10.2 软依赖与版本敏感点（**必须监控**）

这些不是硬限制，但**任何一次 Windows 更新都可能让它们失效**。产品必须有"失效时安全退化"的能力，并且要有办法快速发现失效。

| # | 依赖 | 当前值（实测） | 失效后果 | 监控/发现方式 |
|---|---|---|---|---|
| S1 | ⚠️ 底栏元素的 `Name` 是 `Footer` | 来自 `ControlCenter.dll` 的编译期 XAML | TAP 匹配不到 → 不注入（**安全退化**） | 健康检查无法直接发现。**发现方式**：用户报"按钮没了" + `tap.log` 里没有 `Footer appeared`。可加一个"主动探测"：让 TAP 在 advise 后若 5 秒内没见到任何 `Name==Footer`，记一条 Warn |
| S2 | ⚠️ 元素结构（`Button → ContentPresenter → ContentControl → ContentPresenter → StackPanel[V] → ItemsPresenter → ItemsControl`） | 见 §5.1 | 走兜底（第二行）或放弃（安全退化） | `tap.log` 里 `up[]` 与 `no ItemsPresenter ancestor` / `row wrap failed` |
| S3 | ⚠️ 模型按钮的 `Height` 是显式 `40.0` | 实测 | 高度回退到常量 40，仍是安全的 | 日志里 `model metrics: Height=...`。**这条要定期人工核对** |
| S4 | ⚠️ 底栏几何 `(2189,1417) 358x48`、左边距 4 | 实测（依赖分辨率/DPI/语言/缩放） | 右侧边距不再对称（视觉小瑕疵，不影响功能） | 注意：**不要**把绝对坐标写进代码。代码里只用相对关系（左 inset 镜像） |
| S5 | ⚠️ 目标进程名 `ShellHost.exe` | 实测 | 找不到目标 → 一直 `WaitingForShellHost` | 启动自检可以验证"是否存在 ShellHost"，不存在时给出明确提示 |
| S6 | ⚠️ ShellHost 与 App 同为 Medium IL、非 PPL | 实测 | `OpenProcess` 失败 → `Faulted` | 每次注入前检查（失败即报错，已经是这样） |
| S7 | ⚠️ SDK 的 `xamldiagnostics.dll` 与系统 `Windows.UI.Xaml.dll` 版本兼容 | SDK 10.0.14393.33 + Xaml 10.0.26100.8972 ✅ | TAP 起不来 → 一直 `WaitingForTap` | 记录两者版本到诊断信息里；跨大版本更新系统后**必须重跑 V1** |
| S8 | ⚠️ 同一进程内对不同端点名重复 `InitializeXamlDiagnosticsEx` 的行为 | **未验证** | 可能加载第二个 TAP（被 L2 兜住） | V6 覆盖 |

**维护建议**：

1. 把 S1–S3、S5 的值写进**一个可执行的探测脚本**（不是文档里的字符串），每次 Windows 大更新后跑一次。
2. `tap.log` 的 `up[]` 祖先链输出**不要**删掉 —— 它是结构变化时唯一的现场证据。
3. 出现 S1/S2 失效时，产品行为是"安全地没有按钮"而不是崩溃。这是刻意设计的，要保留。

### 10.3 安全与合规说明

| 项 | 说明 |
|---|---|
| 注入手法 | `CreateRemoteThread` + `LoadLibraryW` —— **木马经典手法**。Defender/EDR 很可能拦或报毒。这是**自用工具**的已知代价 |
| 缓解 | 安装目录加 Defender 排除（脚本只提示命令，不自动执行）。**不要**把这套东西分发给第三方 |
| XAML 诊断 API 的安全背景 | ⚠️ **CVE-2023-36003** 记录了"XAML 诊断注入是提权原语"。本项目的使用前提是**同完整性级别**（Medium IL → Medium IL），✅ 实测成立，不跨越 IL 边界 |
| 不提权 | 全装 `%LOCALAPPDATA%`，不用管理员。**不要**为了"方便"而改成装到 `Program Files` + 服务 —— 那会把一个用户级工具变成需要提权的系统级工具，风险面完全不同 |
| 不越界 | 不 hook shell 代码、不改 shell 的注册表项、不修改 `ControlCenter.dll`。只通过**文档化的诊断契约**操作 XAML 对象树 |
| 数据 | 不收集、不上传任何东西。全部日志在本机 |
| 用户预期 | 文档与 README 必须写清："会向 `ShellHost.exe` 注入代码，Defender 可能报警，建议加排除" |

### 10.4 待解决 / 待补

| # | 项 | 优先级 | 说明 |
|---|---|---|---|
| ~~T1~~ | ~~`GetInitializationData` 是否返回我们的串~~ | — | ✅ **已验证完毕**（2026-10-03）。结论：**返回**，含中文逐字符一致，但**上限正好 259 字符**，≥260 静默返回空串且 `hr` 仍是 `S_OK`。⇒ 配置改走**直投通道**，initData 降级为面包屑。完整长度扫描表见 `verified-after-injection/06-initdata-channel-limit.md` |
| ~~T2~~ | ~~**INI 的编码**对 `GetPrivateProfileStringW` 读中文的影响~~ | — | ✅ **已验证完毕**（2026-10-03）。结论：**只有 UTF-16LE + BOM 可用**。UTF-8 无 BOM ⇒ **静默乱码**；UTF-8 有 BOM ⇒ **键都找不到**（设置被静默忽略）；UTF-16LE 无 BOM ⇒ 乱码；系统 ACP/GBK ⇒ 本机可用但换区域设置即坏。写侧另有坑：`WritePrivateProfileStringW` 对**全新文件**会写成 ANSI。见 `verified-after-injection/09-ini-encoding.md` |
| ~~T3~~ | ~~`action=pipe` 全链路~~ | — | ✅ **已验证完毕**（2026-10-03）。报文逐字节正确；**UI 线程实测未被拖住**（`Invoke` 往返 7–14 ms），管道 IO 在独立线程（tid 可证）；服务端缺失时瞬时降级且有日志；配置热重载顺带验证。★ 副产品：`ConnectNamedPipe` 的合法失败**有两种**（110 / **232**），后者写错会**静默丢点击**。见 `verified-after-injection/08-pipe-action-chain.md` |
| ~~T4~~ | ~~"面板已打开时注入"的真实行为~~ | — | ❌ **不做支持（by design）**。原因：**音量浮层不是常驻窗口** —— 它只在按快捷键时出现、失焦即消失。产品模型是"注入一次 → TAP 常驻 → 之后每次打开面板自动注入"（§9），**根本不需要"往一个已经开着且正在被看的浮层里插东西"**。所以这个行为不值得查明，也不进验收。（观察到过面板消失但原因未定，与产品无关） |
| T5 | **TAP → App 的"注入完成"回传**（`INJECTED <entryId>`） | 中 | 目前的托盘状态只能到"TAP 就绪"，说不了"按钮已添加"（§3.13） |
| T6 | 多显示器 / 不同缩放下的几何 | 中 | §5.2 的右对齐用的是相对关系 + 常量 4，理论上缩放无关，但未验证 |
| T7 | 深色/浅色主题、高对比度主题下的视觉一致性 | 低 | `Foreground`/`Style` 已复制，理论上跟随主题 |
| T8 | 一个 ShellHost 里两个诊断会话并存的实际后果 | 低 | V6 覆盖 |

---

## 11. 附录

### 附录 A：`xamlOM.h` 关键签名

来源：`C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um\xamlOM.h`（✅ 已核对）

```cpp
// 入口：必须由目标进程自己调用（该导出位于目标的 Windows.UI.Xaml.dll）
_Check_return_ HRESULT InitializeXamlDiagnosticsEx(
    _In_ LPCWSTR endPointName,          // 如 L"VisualDiagConnection1"
    _In_ DWORD   pid,                   // GetCurrentProcessId()
    _In_ LPCWSTR wszDllXamlDiagnostics, // SDK 的 xamldiagnostics.dll 绝对路径
    _In_ LPCWSTR wszTAPDllName,         // 我们的 TAP DLL 绝对路径
    _In_ CLSID   tapClsid,              // CLSID_VmExtTap
    _In_ LPCWSTR wszInitializationData);// ★ 我们的 k=v; 串

typedef MIDL_uhyper InstanceHandle;                       // u64

typedef enum VisualMutationType { Add = 0, Remove = 1 } VisualMutationType;

typedef struct VisualElement {
    InstanceHandle Handle;
    SourceInfo     SrcInfo;
    BSTR           Type;
    BSTR           Name;
    unsigned int   NumChildren;
} VisualElement;

typedef struct ParentChildRelation {
    InstanceHandle Parent;
    InstanceHandle Child;
    unsigned int   ChildIndex;
} ParentChildRelation;
```

**本方案用到的三个接口**：

| 接口 | IID | 用到的方法 |
|---|---|---|
| `IVisualTreeServiceCallback` | `{AA7A8931-80E4-4FEC-8F3B-553F87B4966E}` | `OnVisualTreeChange(ParentChildRelation, VisualElement, VisualMutationType)` |
| `IVisualTreeService` | `{A593B11A-D17F-48BB-8F66-83910731C8A5}` | `AdviseVisualTreeChange(IVisualTreeServiceCallback*)`、`UnadviseVisualTreeChange`、`AddChild`/`RemoveChild`/`CreateInstance`/`SetProperty`（**本方案未使用**，见下） |
| `IXamlDiagnostics` | `{18C9E2B6-3F43-4116-9F2B-FF935D7770D2}` | ⭐ `GetIInspectableFromHandle(InstanceHandle, IInspectable**)`、⭐ `GetInitializationData(BSTR*)` |

**其余可选接口**（**本方案刻意不用**）：

| 接口 | IID | 为什么不用 |
|---|---|---|
| `IVisualTreeServiceCallback2` | `{BAD9EB88-AE77-4397-B948-5FA2DB0A19EA}` | `OnElementStateChanged` 对我们的需求无价值，多实现一个接口就多一份 vtable 出错风险 |
| `IVisualTreeService2` / `IVisualTreeService3` | `{130F5136-...}` / `{0E79C6E0-85A0-4BE8-B41A-655CF1FD19BD}` | 提供 `CreateInstance`/`AddChild` 等"用诊断 API 造元素"的能力。我们选择用 C++/WinRT 直接构造 XAML 对象（**类型安全、编译期检查、代码短**）。用诊断 API 要手写 `BSTR typeName` 与字符串化的属性值（如 `"Windows.UI.Xaml.Controls.Button"`、`"4,0,4,0"`），脆弱且没有编译期保护 |
| `IObjectWithSite` | OS 标准 | **必须实现**：用来接收 `IXamlDiagnostics*` |

### 附录 B：PoC → 产品代码映射表

PoC 在 `docs/poc/`，**已跑通**，产品的原生两段基本是它的"改名 + 加配置 + 去实验代码"。

| PoC 文件 | 行数 | 产品对应 | 需要做的改动 |
|---|---|---|---|
| `injector.cpp`（4096 B） | ~150 | `VmExt.Interop.RemoteInjector`（C#） | **重写为 C#**。PoC 用了"本地 kernel32 地址当远程地址"的简化（§3.4），换成 RVA 算法；加错误分类 `InjectError` |
| `vcxlaunch.cpp` | ~250 | `native\VmExt.Launcher\launcher.cpp` | ① 读配置串 + **直投给 TAP**（顺序：直投 → `InitializeXamlDiagnosticsEx`，§4.1.3 第 7–9 步）；② 长度 >259 时打警告；③ ini 名改为 `VmExt.Launcher.ini`；④ 日志路径可配；⑤ 删掉 `endpointStart` 之外的实验分支 |
| `vcxtap.cpp` | ~700 | `native\VmExt.Tap\tap.cpp` | ① 导出 `VmExtTapProvideInitData` + 配置来源判定（§4.2.3 第 4 步）；② 单例互斥体；③ 配置从 `vmext-tap.ini` 读（现在是 `vcxtap.ini` 的 `stage`）；④ `RunWinver()` → `PerformClickAction(entry)` + 管道模式；⑤ 按钮文字/边距/高度来自配置；⑥ `stage` 分级日志机制简化掉（只保留 `enabled`）；⑦ vtable 相关代码**保持不动**（已跑通，重写没有收益） |
| `vcxmix.cpp` | ~600 | **无**（方案 A 已证伪） | **删除**。保留在 `docs/poc/src/` 里作为记录即可，不要带进产品仓库 |
| `build.cmd` | — | `native\*.vcxproj` | 见 §7.3 |
| `cycle.ps1` / `scripts\*.ps1` | — | 保留在开发工具目录 | 见 §8.3 |

**移植时必须一起搬过去的"踩坑修正"**（这些是 PoC 花时间换来的，重新踩一遍毫无意义）：
1. 日志用 `CreateFileW` 而不是 CRT 流（§4.3 坑 2）
2. `STDAPI` + `/EXPORT:`（§4.3 坑 3）
3. `#undef GetCurrentTime`（§4.3 坑 6）
4. `IndexOf`+`RemoveAt` 而不是 `Remove`（§4.3 坑 8）
5. `Grid::SetColumn` 要 `FrameworkElement`（§4.3 坑 9）
6. 列 0 用 `Star` 不用 `Auto`（§5.2）
7. `Height` 读显式属性、不测量（§5.3）
8. 所有 `ActualXxx` 先判 `>0`（§5.5 / I1）

### 附录 C：环境事实速查（实测，用于交叉核对）

| 项 | 值 |
|---|---|
| Windows build | 26300.9550 |
| 面板窗口类 / 标题 | `ControlCenterWindow` / 「快速设置」 |
| 面板 band | **4** |
| 面板宿主 | `ShellHost.exe`，父进程 `sihost.exe` |
| 面板子窗口（XAML 宿主） | `Windows.UI.Input.InputSite.WindowClass`（UIA `FrameworkId=XAML`） |
| XAML 实现 | **System XAML** = `Windows.UI.Xaml.dll` 10.0.26100.8972（**不是** WinUI3） |
| `Windows.UI.Xaml.dll` 相关导出 | `InitializeXamlDiagnosticsEx`、`GetDependencyObjectAddress`、`OverrideXamlMetadataProvider` |
| 诊断运行时 | `xamldiagnostics.dll`（SDK 10.0.14393.33），位于 `C:\Program Files (x86)\Windows Kits\10\bin\x64\XamlDiagnostics\` |
| `ControlCenter.dll` | 4 MB WinRT 组件，只导出 `ControlCenterMain` / `DllCanUnloadNow` / `DllGetActivationFactory` |
| `ControlCenter.*` 是否注册为 ActivatableClassId | **否**（所以方案 A 才需要直接调 `DllGetActivationFactory`） |
| ShellHost 完整性级别 | Medium IL，非 PPL，`OpenProcess(ALL_ACCESS)` 成功 |
| 底栏几何（注入前） | `(2189,1417) 358x48` |
| 模型按钮几何 | `(2193,1420) 94x40`，`Height=40.0` 显式，`MinHeight=0.0`，`Style` 非空 |
| 注入后我们的按钮几何 | `(2472,1419) 71x40`（右边缘 2543 = 2547-4，高 40） |
| 工具链 | VS18 Community 14.50.35717（另有 VS2022 BuildTools 14.44）；Windows SDK 10.0.26100.0；`cl`/`cmake`/`msbuild` **都不在 PATH**；无 WDK |
| dotnet SDK | 8.0.425 / 10.0.103 |

### 附录 D：参考

| 来源 | 内容 |
|---|---|
| `Win11-QuickSettings-XAML-Injection-Notes.md` §9-§11 | 全部探测过程与结论、注入落地的完整记录 |
| `docs/design.md` §2 §6 | 方案选型（含方案 A 的完整证伪证据链）、最终实现说明 |
| `docs/reference/research-agent-report.md` | 外部调研：Windhawk / ExplorerPatcher / XAML 诊断 API（带源码引用） |
| `xamlOM.h` | 诊断接口的权威定义（本附录 A 即摘自此文件） |
| `microsoft/microsoft-ui-xaml` Samples/WinUISnoop、`asklar/lvt`、`TranslucentTB/ExplorerTAP`、`m417z/UWPSpy` | 现成 TAP 实现的可参考样本 |

### 附录 E：修改记录

| 版本 | 日期 | 变更 |
|---|---|---|
| 1.0 | 2026-10-03 | 首版。基于已跑通的 PoC（Route B：XAML 诊断 TAP）定稿架构、跨进程契约、C# 与原生模块的逐方法说明、验证计划与排障手册 |



