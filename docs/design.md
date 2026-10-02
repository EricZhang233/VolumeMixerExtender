# 注入设计：在「声音输出」页 Footer 右侧加一个居右入口

> 目标（Eric 定稿）：在快速设置音量面板**底栏「更多音量设置」按钮右侧的空位**，加一个**居右对齐**的入口（落 XAML，不是悬浮层）。
> 目标机：Windows 11 build 26300.9550，ShellHost.exe，System XAML。
> 所有实测数据见 `logs/raw-outputs.md`。
>
> **状态更新（2026-10-03）**：本文原来主推的**方案 A 已被证伪**（见 §2 与 §3），
> **方案 B（XAML 诊断 TAP）已跑通并落地**：按钮"TestLink"出现在底栏同一行右侧，点击启动 `winver.exe`。
> 实现与最终实测几何见 **§6**。

---

## 1. 事实基线（全部已实测，不是推测）

| # | 事实 | 出处 |
|---|---|---|
| F1 | 面板窗口类 `ControlCenterWindow`，**band=4**；`EnumWindows`/`FindWindow`/UIA `RootElement` 都看不到它 | §01 §02 |
| F2 | 面板窗口 **HWND 常驻**（开关都是 `0x000100FE`），只有首建发 `OBJECT_CREATE` | §05 |
| F3 | **XAML 元素实例每次打开都重建**（runtime id `.4.76` → `.4.114`，5/5 全变） | 见 `logs/element-persistence.txt` |
| F4 | 真 XAML 树挂在子窗口 `Windows.UI.Input.InputSite.WindowClass`（`FrameworkId=XAML`）下 | §03 |
| F5 | 面板是 **System XAML**（`Windows.UI.Xaml.dll` 10.0.26100.8972 已加载；`Microsoft.Internal.FrameworkUdk.dll` / `Microsoft.UI.Content.*` **均未加载**） | §08 |
| F6 | ShellHost 与我们的进程**同为 Medium IL**，**非 PPL**，`OpenProcess(ALL_ACCESS)` **成功** → 经典 DLL 注入可行 | `logs/injection-feasibility.txt` |
| F7 | `Windows.UI.Xaml.dll` 导出 `InitializeXamlDiagnosticsEx`、`GetDependencyObjectAddress`、`OverrideXamlMetadataProvider`；WinUI3 侧无诊断入口 | §07 |
| F8 | `XamlDiagnostics.dll`（诊断 tap DLL）**在机器上找到了**：`C:\Program Files (x86)\Windows Kits\10\bin\x64\XamlDiagnostics\xamldiagnostics.dll`（arm64/x86 也有）。见 F13 | 实测（此前一次递归搜索漏报，已更正） |
| F13 | **本机有原生工具链**：`cl.exe` = `C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.50.35717\bin\Hostx64\x64\cl.exe`（另有 VS2022 BuildTools 14.44）；CMake 在 `C:\Program Files\CMake\bin\cmake.exe`；dotnet SDK 8.0.425 / 10.0.103。注意：**都不在 PATH 上**，也没有 vswhere/msbuild.exe | 实测 |
| F9 | 面板本体实现是 `ControlCenter.dll`（4 MB，WinRT 组件，只导出 3 个：`ControlCenterMain` / `DllCanUnloadNow` / `DllGetActivationFactory`） | §08 |
| F10 | `ControlCenter.dll` 内含类名（字符串）：`ControlCenter.ControlCenterPage`、`ControlCenter.ControlCenterView`、`ControlCenter.ControlCenterViewModel`、`ControlCenter.IVolumeMixerList`、`ControlCenter.AsyncSlider` … | 见下 §2 |
| F11 | **`ControlCenter.*` 没有注册到 `HKLM\...\WindowsRuntime\ActivatableClassId`** → `RoGetActivationFactory` 拿不到；只能直接调它自己的 `DllGetActivationFactory` | 实测 |
| F12 | Footer 几何：`x=2189, 358x48`；「更多音量设置」按钮 `x=2193, 94x40`；**右侧空位 `x=2287..2547`，约 256x40** | §06 |

`ControlCenter.dll` 里的类名（节选，共 60+）：

```text
ControlCenter.ControlCenterPage        ControlCenter.IControlCenterPage
ControlCenter.ControlCenterView        ControlCenter.IControlCenterView
ControlCenter.ControlCenterViewModel   ControlCenter.IVolumeMixerList
ControlCenter.AsyncSlider              ControlCenter.IAsyncSlider
ControlCenter.AccessibleItemContainer  ControlCenter.IQuickActionToggleButton
ControlCenter.DelegateCommand          ControlCenter.MediaTransportControls
```

**关键推论**：面板是「一个 WinRT 组件 DLL 提供的 XAML Page」，而且**每次打开都重新实例化**（F3）——
⇒ 只要拦住**页面的实例化**，就能在"页面刚建好"时拿到它的 `IInspectable*`，然后直接操作 XAML 树。

---

## 2. 方案选型

| 方案 | 可行性 | 依赖 | 脆弱性 | 结论 |
|---|---|---|---|---|
| **A. 激活工厂 vtable hook**（`ControlCenter.dll!DllGetActivationFactory` → 拦 `ActivateInstance`） | ❌ **已证伪**：hook 本身能装上（自测命中），但 shell **根本不用 `IActivationFactory::ActivateInstance` 创建这些编译期 XAML 对象**——全新 ShellHost + 面板首开后命中 **0 次**。详见 §6.1 | 无 | — | **不采用** |
| B. XAML 诊断 API（`InitializeXamlDiagnosticsEx` + `IVisualTreeService`） | ✅ **已跑通**：API 支持 `CreateInstance`/`AddChild`/`SetProperty`，`IXamlDiagnostics::GetIInspectableFromHandle` 给出真 `IInspectable`；`AdviseVisualTreeChange` 能推元素事件（正好在 `Footer` 出现时拦） | **本机已有 `xamldiagnostics.dll`（F8）+ `cl.exe`（F13）**；需自写 TAP DLL（实现 `IObjectWithSite` + `DllGetClassObject`，**无需注册表**） | 低：走文档化的 COM 契约，**不依赖类名/RVA/特征码** | **✅ 采用，已落地（§6）** |
| B′. A 与 B 组合 | — | 同上 | — | A 证伪后**无意义**，已放弃 |
| C. hook 组件实现方法（Windhawk/EP 套路：`TaskListButton::UpdateVisualStates` 之类） | ⚠️ 需按符号名/RVA 定位 | PDB 符号或特征码 | **高**（build 26300 很前沿，符号一变就崩） | 不采用 |
| D. 悬浮窗（`WS_EX_NOACTIVATE`） | ✅ | 无 | 低 | 不采用（失焦掉面板、非原生观感，§4.2 已否） |

---

## 3. 方案 A 的详细设计（**已证伪，仅作记录**，见 §6.1）

### 3.1 总体结构

```text
injector.exe（我们的进程，Medium IL）
  └─ OpenProcess(ALL_ACCESS) → VirtualAllocEx → WriteProcessMemory(DLL 路径)
     → CreateRemoteThread(LoadLibraryW)                       [F6 已验证可行]
        └─ 注入进 ShellHost.exe：vcxmix-hook.dll
             ├─ 1) 定位 ControlCenter.dll（已加载，F9）
             ├─ 2) GetProcAddress("DllGetActivationFactory")
             ├─ 3) 对每个 ControlCenter.* 类名调 DllGetActivationFactory
             │       → 得到单例 IActivationFactory*
             │       → 打补丁：vtable[6] = 我们的 ActivateInstance detour
             ├─ 4) 同时 inline hook DllGetActivationFactory 导出，
             │       把"之后才被激活"的工厂也一并包装
             └─ 5) detour 里：ActivateInstance 返回的 IInspectable* 就是页面
                     → try_as<FrameworkElement>() → add_Loaded(...)
                     → Loaded 回调里 FindName(L"Footer")
                     → 造一个 Button → Children().Append(button)
```

### 3.2 为什么是 vtable[6]

```text
IUnknown      : QueryInterface(0) AddRef(1) Release(2)
IInspectable  : GetIids(3) GetRuntimeClassName(4) GetTrustLevel(5)
IActivationFactory : ActivateInstance(6)      <-- 就是这一格
```

实现方式（两条都行，建议后者）：
- 改工厂对象的 vtable 指针 → 指向我们复制的表（污染面小，只影响这一个对象）；**注意**：工厂是**单例**，改它等于全局生效，正是我们要的。
- 或 `VirtualProtect(PAGE_READWRITE)` 直接改 `.rdata` 里那张 vtable 的第 6 格（更简单，但影响该 vtable 的所有实例）。

### 3.3 关键 API 签名

```c
// ControlCenter.dll 的导出
HRESULT DllGetActivationFactory(HSTRING activatableClassId, IActivationFactory** factory);

// IActivationFactory
HRESULT ActivateInstance(IInspectable** instance);

// 造控件（XAML 侧）
RoGetActivationFactory(HSTRING(L"Windows.UI.Xaml.Controls.Button"), IID_IActivationFactory, &f);
f->ActivateInstance(&btn);
btn->put_Content(/* ... */);
btn->put_HorizontalAlignment(/* 2 = Right */);

// 找元素
FrameworkElement::FindName(HSTRING(L"Footer"), IInspectable**);   // AutomationId "Footer" 大概率就是 x:Name
// 或 VisualTreeHelper::GetChildrenCount/GetChild 递归 + AutomationProperties::GetAutomationId
```

### 3.4 插入时机与线程

- `ActivateInstance` 会在**创建页面的那个线程**上被调用；XAML 页面只能在 UI 线程建 → **detour 本身就是 UI 线程**，可直接调 XAML API（C 里那个 RPC_E_WRONG_THREAD 的坑不存在）。
  ⚠️ 仍需在 PoC 里**实测确认**（V1 打日志记录 tid，和 `ControlCenterWindow` 的 tid=5136 比对）。
- **不要在 `ActivateInstance` 返回后立刻插元素**：那一刻 `OnApplyTemplate` 可能还没跑，`FindName("Footer")` 可能返回 null（Windhawk 的 mod 也踩过这个坑，注释原文：*"Calling ApplyAllSettings immediately from the constructor fires before the XamlRoot is stable … crash the process on startup"*）。
- **正确做法**：`add_Loaded(...)`，等页面的 `Loaded` 事件，再插。插之前判空 + try/catch，失败就静默放弃（不抛进 shell）。

### 3.5 居右对齐的落法（需运行时确认 Footer 类型）

Footer 现在只有 1 个子元素（「更多音量设置」按钮，`94x40` 靠左），说明 Footer 很可能是 **Grid**（左对齐子项 + 剩余空间）或 **水平 StackPanel**。两种情况的处理不同：

| Footer 实际类型 | 落法 |
|---|---|
| `Grid`（最常见） | 把我们的 Button 追加进去，`HorizontalAlignment = Right`；若 Grid 有 `ColumnDefinitions` 且最后一个列是自适应/星号，则用 `Grid::SetColumn(btn, last)` + `HorizontalAlignment=Right` |
| 水平 `StackPanel` | 追加会紧贴上一个按钮（不是贴右）→ 需要包一层：`Grid{ ColumnDefinitions: Auto, * }`，或改用 `RelativePanel`/设置 Footer 的 `HorizontalAlignment=Stretch` + 插入一个 `Width=*` 的 spacer，或直接在 Footer 里插一个 `Grid` 包住两者 |
| `RelativePanel` | `RelativePanel::SetAlignRightWithPanel(btn, true)` |

⇒ **PoC 第 3 步必须先 dump Footer 的 `winrt::get_class_name()` 和它的子元素类型**，再决定落法。这是设计里唯一还没定死的点。

### 3.6 失败的兜底

- 所有 detour body 用 `try/catch(...)` 包死：**任何异常都不许逃进 shell**。
- 版本/类名对不上（`DllGetActivationFactory` 返回失败 / `FindName` 一直 null）→ 打日志、卸钩子、保持沉默。
- 崩了 ShellHost 的后果：任务栏/开始/快速设置会重启（`ShellHost.exe` 由系统托管重启），但**若崩在 UI 线程可能连带 explorer 体验异常** → 必须先在**只加载不做事**的 V0 阶段验证稳定性。
- Defender：`CreateRemoteThread + LoadLibraryW` 是木马经典手法，很可能被拦 → 需要为输出目录/进程加排除（自己的机器、自己用，可接受）。

### 3.7 分阶段验证计划（每步都有可判定的通过标准）

| 阶段 | 内容 | 通过标准 |
|---|---|---|
| **V0** | 只注入，DLL 里只写日志（不 hook） | ShellHost 不崩；反复开关面板 10 次无异常；日志文件出现 |
| **V1** | hook `DllGetActivationFactory`，只**记录**被激活的类名 + tid | 开关面板一次，日志里出现页面类名；tid == 面板 tid（5136） |
| **V2** | 对 `ControlCenter.*` 全部类名逐个拿工厂 + 打 vtable[6]，记录 `ActivateInstance` 拿到的 `IInspectable*` 的 `GetRuntimeClassName` | 能打印出页面对象的真实运行时类名 → **确定是哪个类** |
| **V3** | 在该页面上 `FindName("Footer")`，dump Footer 的类名/子元素类型/子元素数 | 拿到 Footer 的类型 → 定下 §3.5 的落法 |
| **V4** | 追加一个纯色 Button（不绑逻辑），按 §3.5 居右 | 目视：面板底栏右侧出现按钮，位置在 `x≈2287..2547` 区间内 |
| **V5** | 绑 `Click`（`add_Click` + 自写 `winrt::implements` 委托）→ 打开「音量合成器」页或自己的程序 | 点击有反应；面板行为不受影响 |

---

## 4. 还没解决 / 待确认

> §4 是**方案 A 时期**的待办清单，逐项现状见括号（2026-10-03 更新）。

1. **哪个 `ControlCenter.*` 类对应「声音输出」页** —— （**已无关**：A 已证伪，B 不需要类名）
2. **Footer 的 XAML 类型**（决定 §3.5）—— （**已解决**：`Footer` 是 `ItemsControl`，`ItemsPanel` 是**纵向** `StackPanel`；落法见 §6.3，§3.5 那份三选一表格里的"水平 StackPanel"一行不是本案）
3. **`ActivateInstance` 是否真的在 UI 线程** —— （**已无关**：拦截根本没触发）
4. **方案 A vs B 的取舍** —— （**已定**：A 证伪，B 落地。B 的进程级单例 / 无 teardown 限制仍然成立，但已接受）
5. **Defender 是否会拦注入** —— （**实测不会**：`CreateRemoteThread + LoadLibraryW` 反复注入 ShellHost 全程无拦截）
6. **构建注意** —— （**已解决**：`build.cmd` 在同一个 `cmd.exe` 里 `call vcvars64.bat` 后调用 `cl`）

---

## 5. 参考（来自调研 agent 报告，见 `logs/research-agent-report.md`）

- Windhawk mod `taskbar-content-presenter-injector.wh.cpp`：**在 WinRT 实现方法里往 `panel.Children().Append(presenter)`** —— 与我们 V4 的做法同类；它还给出 `GetFrameworkElementFromNative`（从 native `this` 反查 WinRT 对象，偏移 `+3` 指针）与 `SYMBOL_HOOK` 表。
- ExplorerPatcher `ShellExperienceHostPatches.cpp`：hook `NetworkUX::App::LoadResourceDictionaries` + 改 `Application.Resources` 的 `QuickActionControlStyle.Setters()`。**注意**：那是**旧**的 ShellExperienceHost/NetworkUX 路径；我们这台 build 26300 的快速设置在 `ControlCenter.dll`（F9），ShellHost 里并没有 `NetworkUX.dll` —— 所以**不能照抄**，但"在进程内改 XAML 对象"的范式一致。
- XAML 诊断 API 的权威定义：`xamlOM.h`（`IVisualTreeService::{CreateInstance, AddChild, RemoveChild, ClearChildren, SetProperty}`、`IXamlDiagnostics::GetIInspectableFromHandle`）。
- 现成 TAP 实现可参考：`microsoft/microsoft-ui-xaml:Samples/WinUISnoop`、`asklar/lvt`、`TranslucentTB/ExplorerTAP`、`m417z/UWPSpy`、`windhawk-mods:mods/cjk-spacer.wh.cpp`。
- 安全提醒：**CVE-2023-36003** —— XAML 诊断注入是已知的提权原语；跨完整性级别可能被 OS 拦。我们同 IL（F6），风险不在这一档。

---

## 6. 最终落地：方案 B 跑通（2026-10-03）

### 6.1 先记结论：方案 A 为什么不行

不是"hook 装不上"，而是"装上了也没人走这条路"：

1. `ControlCenter.dll!DllGetActivationFactory` 能解析出 **101/102**（共 123 个候选）`ControlCenter.*` 类名；
2. 拿到的工厂**不是单例**（每次调用返回新对象），但**共享同一份 `.rdata` vtable** → patch `vtable[6]` 是全局生效的；
3. `IActivationFactory` vtable 布局：`[0..2]` IUnknown、`[3..5]` IInspectable（`GetIids`/`GetRuntimeClassName`/`GetTrustLevel`）、`[6]` `ActivateInstance`；
4. **自测能命中**（`[detour] ActivateInstance ENTERED` 打出来了）→ patch 本身是活的；
5. 决定性实验：全新 ShellHost，**启动后 29 ms** 就注入（早于第一次面板创建），然后开面板 → **命中 0 次**。

⇒ shell 创建这些编译期 XAML 对象时**不走 `IActivationFactory::ActivateInstance`**。这条路到此为止。

### 6.2 方案 B 的实际结构（4 个产物，都在 `poc/`）

```text
injector.exe     --dll <dll> [--process ShellHost.exe | --pid N]
                 经典 CreateRemoteThread + LoadLibraryW
   └─ 注入 vcxlaunch.dll
        └─ vcxlaunch.dll   等在目标进程里 WaitForModule(Windows.UI.Xaml.dll)，
        │                  然后**在进程内**调
        │                  InitializeXamlDiagnosticsEx(L"VisualDiagConnection1", pid,
        │                      <SDK>\xamldiagnostics.dll, <poc>\vcxtap.dll, CLSID_VcxTap, nullptr)
        │                  端点名第一发就成功（N=1）
        └─ XAML core 用 DllGetClassObject(CLSID_VcxTap) 把 vcxtap.dll 载入 ShellHost
             └─ vcxtap.dll  COM in-proc server，实现 IObjectWithSite + IVisualTreeServiceCallback
                SetSite 拿到 IXamlDiagnostics* / IVisualTreeService*
                AdviseVisualTreeChange → 元素 Add/Remove 事件（**会重放已存在的树**）
                命中 Name=="Footer" 时：
                    GetIInspectableFromHandle(handle) → 真 FrameworkElement
                    → 注入 Button（Content "TestLink"，Click → CreateProcessW("winver.exe")）
```

要点：
- **TAP DLL 不需要注册表注册**，XAML core 自己 `LoadLibrary` + `DllGetClassObject`；
- `vcxtap.dll` 的 `DllGetClassObject` / `DllCanUnloadNow` 必须是 `STDAPI`（与 `combaseapi.h` 一致）并走 `/EXPORT:` 导出，用 `__declspec(dllexport)` + `extern "C" __stdcall` 会 C2375；
- 关键 IID：`IVisualTreeServiceCallback {AA7A8931-80E4-4FEC-8F3B-553F87B4966E}`、`IVisualTreeService {A593B11A-D17F-48BB-8F66-83910731C8A5}`、`IXamlDiagnostics {18C9E2B6-3F43-4116-9F2B-FF935D7770D2}`。

### 6.3 唯一的硬骨头：底栏 ItemsPanel 是**纵向** StackPanel

实测出的关键事实（`vcxtap.log` 里打出的 `up[]` 就是这条链）：

```text
Button[更多音量设置]
  → ContentPresenter
    → ContentControl            ← 这一"行"的逻辑内容
      → ContentPresenter        ← ItemsControl 为它生成的 item container
        → StackPanel[Vertical]  ← Footer 的 ItemsPanel ★ 纵向
          → ItemsPresenter
            → ItemsControl name="Footer"
              → Grid → ContentPresenter[PageContent] → Grid[FullScreenPageRoot]
                → ContentPresenter → ContentControl[PageWindow]
                  → ControlCenter.FullScreenPage[FullScreenPageControl]
                    → ContentPresenter → L2Frame → … → ControlCenterView
```

所以**往 Footer append 一项必然新起一行**（实测 footer 从 48 高变成 78，按钮掉到第二行）。
修法（`InjectIntoRow`）：找到"item container"（即 ItemsPresenter 的直接子 `StackPanel` 的、位于模型按钮祖先链上的那个孩子），
把它从 ItemsPanel 里取出，塞进一个两列 `Grid`（列 0 `*` 放原行、列 1 `Auto` 放 TestLink），再把 Grid 放回 ItemsPanel。

- 列 0 用 `*` 而不是 `Auto`：这样原行的宽度和原来完全一致，`更多音量设置` 位置不动（hover 高亮不会缩水）；
- 属性复制：`Style` / `MinWidth` / `MinHeight` / `Padding` / `CornerRadius` / `FontSize` / `FontWeight` / 内容对齐 / `Foreground`；
- **高度**：模型按钮的 `Height` 是显式 **40.0**（`MinHeight=0.0`），所以直接读 `Height()` 就能在注入当刻定死 40，**不依赖布局时机**（这点很关键，见下）；
- **右边距**：4 px，和左边距对称。

> 踩过的坑：曾试过用 `TransformToVisual` 去"测量模型按钮的内边距"再算右边距 ——
> 但注入发生在布局之前，`model.ActualWidth()` 还是 0，算出 354 px 的荒谬边距，
> 而之后**没有**再触发 `SizeChanged` 去纠正（面板树已布局完，尺寸不再变化），结果 UIA 矩形直接变成 `∞`。
> 教训：**任何依赖 `ActualWidth/ActualHeight` 的取值都必须先判 `> 0`，否则就是这次这种静默错位。**

### 6.4 最终实测几何（UIA，稳定复现两轮）

| 元素 | 屏幕矩形 | 说明 |
|---|---|---|
| `Footer` | `(2189,1417) 358x48` | 高度仍是 48 → **没有多出第二行** |
| `更多音量设置` | `(2193,1420) 94x40` | 位置与注入前完全一致 |
| `TestLink` | `(2472,1419) 71x40` | 同一行、右边缘 2543 = 距底栏右边 **4 px**、高 **40** 与模型一致 |

点击验证：UIA `InvokePattern` 调用后 `winver.exe` 启动（标题「关于"Windows"」），已确认。验证脚本 `recon/click-testlink.ps1`。

顺带修正一个误判：`FooterGrid` / `LeftFooter` / `RightFooter` **不是**这条底栏。它们挂在 `L1Grid` 下，属于 **L1（主快速设置页）的底栏**，而且一直是空的。声音输出页的底栏是页面级的 `Footer`（`PageWindow → FullScreenPage → L2Frame → PageContent → Footer`）。所以匹配条件只认 `Footer`。

### 6.5 一轮测试的代价 / 为什么 cycle 要重启 explorer

`poc/cycle.ps1` 一条命令跑完：停 shell → 构建 → 重启 explorer → 注入 → 开面板 → 打日志 + 打几何。重启的原因有两条，**都不是产品需求**：

1. **硬约束**：`vcxlaunch.dll` / `vcxtap.dll` 已被 `LoadLibraryW` 载入 `ShellHost.exe`，**进程内已加载的 DLL 文件是写锁定的**，`link` 会 LNK1104 落不了盘。改了 C++ 就必须让持有者退出。（换文件名可以绕过，但那样只会攒出一堆 `vcxmix2..7.dll` 之类的垃圾。）杀 `explorer.exe` 是为了拿到**确定全新的 `ShellHost.exe`**——它由 shell 体系托管。
2. **测试卫生**：`InitializeXamlDiagnosticsEx` 是**每进程一次**的诊断初始化，TAP 常驻且会**重放已存在的树**；同一进程反复测等于在"已被上一轮改造过的树"上继续跑，结论会失真。

⇒ **产品上不需要重启 explorer，也不需要在 logon 阶段抢时间。** 实测（`poc/test-late-inject.ps1`）：

- 全新 shell 起来后（ShellHost 已跑 ~30 s、面板已开过又关掉）才注入 → `InitializeXamlDiagnosticsEx` **仍然成功**（`SUCCESS: endpoint=VisualDiagConnection1 hr=0x00000000`），TAP 载入、advise 成功；
- 之后**不再做任何注入**，只是关掉再打开面板 → `TestLink` 出现（`(2472,1419) 71x40`）。
  ⇒ TAP 常驻，之后**每次打开面板 `Footer` 一出现就自动注入**（F3：XAML 元素每次打开都重建，进程/HWND 常驻）。

**但有一个真正的生命周期约束**：TAP 活在 **ShellHost** 里，而 ShellHost 的父进程是 **`sihost.exe`**（实测，不是 explorer）。
ShellHost 会随 shell 重启（explorer 重启 / sihost 重启 / ShellHost 自身崩溃）而换成**新进程**，TAP 随之消失、按钮也就没了。
⇒ 产品需要的是「**等 ShellHost.exe 出现 → 注入 → 监视它退出 → 再来一遍**」这个循环，而不是"重启 explorer"。
这个监视可以做到 0 CPU：§9.8 已实测 band 窗口照发 WinEvent、空闲 600 ms 0 事件；或直接用 `RegisterWaitForSingleObject` / WMI `Win32_ProcessStartTrace` 盯进程创建。

**留一个诚实的保留**：这套流程**反不掉**的只有"诊断会话"本身——`InitializeXamlDiagnosticsEx` 是每进程单例、没有 teardown API，
TAP DLL 一旦载入就跟着 ShellHost 活到进程退出。但它**不影响任何可见行为**：把注入开关关掉（或让 TAP 什么都不做）就等于不存在，
残留的只是"ShellHost 里所有 XAML 变更都会回调我们"那点开销（回调里只做一次字符串比较）。
另外"面板正开着时注入会不会即时生效"没测成：那次实验里面板在注入前后就消失了，且**无法确定**是我的注入动作导致
（`InitializeXamlDiagnosticsEx` 会同步遍历整棵 XAML 树，可能短暂卡住 ShellHost UI 线程 → 面板失焦自动关闭）还是当时有人在操作机器。
由于面板本来每次都要重新打开，这一条不影响产品设计。

### 6.6 已知限制 / 待办

- TAP 的进程级单例特性：诊断初始化后**没有干净的 teardown**，卸载/重载 TAP 这条路没走通 → 每轮迭代靠重启 ShellHost。
- 匹配条件是 `Name=="Footer"` 这个**字符串**。它在 `ControlCenter.dll` 的编译期 XAML 里，属于"改了就不工作"的软依赖（但比 RVA/特征码稳得多）。
- 目前按钮的 `Click` 是硬编码 `winver.exe`，还没有真正的"扩展入口"语义。
- 交付形态（怎么在登录时自动注入、要不要做成服务/启动项、Defender 排除项）还没定。
