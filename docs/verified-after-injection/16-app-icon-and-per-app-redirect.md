# 16 · 面板内：应用图标 + 逐应用重定向 + 录制模式改哨兵（2026-10-04）

| 项 | 内容 |
|---|---|
| 目的 | 面板里**应用行的图标是空白**；**逐应用重定向的 UI 从未落地**；Eric 定稿交互：点应用行向下展开，用下拉框选重定向端点 |
| 结论 | ★ 图标、重定向、清除反馈**全部落地并经 Eric 实机交互确认**（两轮）；本轮**挖出并修掉三个真 bug**（两个在 XAML 命中测试/懒创建上，一个在报文协议上，见 §3/§6） |
| 新增 | `Core/AppIcons.*`（进程 exe → HICON → WIC PNG 缓存）、`Core/RedirectStore.*`（`redirects.ini`，appKey → deviceId） |
| 修改 | `Components/inject.tap/Page.h`（`MakeAppIcon` / `MakeRedirectCombo` / `MakeAppRow` 全部重做）、`Components/inject.tap/Tap.cpp`（`TapPageSendPipe` 改为返回 `int`）、`Core/AudioInterop.h`（增 `FindVirtualEndpoint`）、`Core/AudioDeviceManager.*`（增 `FindVirtualDevice`）、`Core/TapCommand.*`（增参数个数校验）、`Core/TapPipeServer.cpp`、`Core/App.cpp`、`text.yaml` |
| 证据 | TAP 日志 `tap-20261004-023535.log` / `tap-20261004-025324.log`、宿主日志 `app-20261004-025324.log` / `app-20261004-030240.log`（`%TEMP%\eric\VolumeMixerExtender\log`） |
| 未覆盖 | "重定向真的把声音搬过去了"**要在实体机上验**（本机只有 RDP 虚拟端点，见 §7） |

---

## 1. 应用图标

| 项 | 做法 |
|---|---|
| 提取 | 会话的 `appKey` 是小写 exe 全路径 ⇒ `SHGetFileInfoW(SHGFI_ICON \| SHGFI_LARGEICON)` 拿 `HICON`，失败退 `ExtractIconExW` |
| 编码 | `IWICImagingFactory::CreateBitmapFromHICON` → PNG，写到 `<缓存根>\icons\<hash>.png`（同一进程只做一次） |
| 渲染 | TAP 用 `Image` + `file:///` URI；**取不到时回落"首字母色块"**（`InitialLetter`），不留空白 |
| 实测 | 首次会话（未命中缓存）日志： |

```text
2026-10-04 02:36:49.646 [DEBUG] [icons] [tid=9100 seq=33] 应用图标已提取
    C:\Program Files (x86)\Tencent\QQMusic\QQMusic.exe
    -> C:\Users\eric\AppData\Local\Temp\eric\VolumeMixerExtender\icons\FEA0F34CB67105FB.png
```

⇒ 缓存命中后不再出现该行（后续会话日志里确实没有）——**缓存生效**。

> 参考实现对照：EarTrumpet 的 `ImageEx`（`IShellItemImageFactory`）思路相同，我们选择"先落盘成 PNG 再由 XAML 读 URI"
> 是因为载荷里没有 `IShellItemImageFactory` 之外的 WIC 桥，且缓存能省掉每次开面板的重复提取。

## 2. 逐应用重定向：两版交互（第一版被 Eric 当场否掉）

| 版本 | 交互 | 结果 |
|---|---|---|
| v1（**已废**） | 应用行右侧放一个小 chevron 区域**单独**当"展开按钮" | ⛔ **实测点不动** —— chevron 落在不可点的 trailing 区，且用 `Background(nullptr)` 的画刷只有字形本身能命中（几个像素宽） |
| v2（**现行**） | **整条标题带**（图标 + 名称 + chevron）合并成**一个透明底 `Button`**，点哪儿都展开 → 展开面板里一个 `ComboBox`（首项「默认设备」= 清除，其余为渲染端点；`显示驱动名` 设置照旧生效） | ✅ 实测可点、可选 |

页面侧交互探针（`tap-20261004-025324.log`，节选，Eric 的两轮点击）：

```text
02:53:37.027 [DEBUG] [page] 应用行展开
02:53:37.924 [DEBUG] [page] 重定向下拉已展开 app=c:\program files (x86)\tencent\qqmusic\qqmusic.exe
02:53:38.682 [DEBUG] [page] 选择重定向 pid=11960 目标=远程音频
02:53:38.686 [DEBUG] [page] 重定向下拉已收起 app=c:\...\qqmusic.exe
02:53:39.331 [DEBUG] [page] 已清除全部重定向并重置列表
02:53:40.277 [DEBUG] [page] 应用行展开            ← 清除之后页面仍然可交互（关键）
02:53:41.087 [DEBUG] [page] 重定向下拉已展开 app=c:\...\qqmusic.exe
```

宿主侧对应的报文（`app-20261004-025324.log`，节选）：

```text
02:53:38.683 [INFO] [pipe] 收到报文 SETREDIRECT: SETREDIRECT render 11960 {3.0.0.00000001}.{6C26BA7D-...}
02:53:38.692 [WARN] [audio.policy] 设置逐应用重定向失败 pid=11960
02:53:38.692 [INFO] [pipe] 逐应用重定向 pid=11960 role=console 结果=3
02:53:38.696 [WARN] [pipe] 逐应用重定向未生效(本机 RDP 限制?), 未记录状态 pid=11960
02:53:39.300 [INFO] [pipe] 收到报文 CLEARREDIRECT: CLEARREDIRECT
02:53:39.301 [INFO] [pipe] 清空逐应用重定向 结果=0
```

⇒ **报文链路、UI 交互、状态记录策略三件事都在日志里对得上**；`结果=3` 与"未记录状态"是同一件事的两面（见 §5）。

## 3. 两个真 bug（都在 XAML 侧，都是"看起来能用、实际吃掉输入"）

| # | 症状 | 根因 | 修法 |
|---|---|---|---|
| 1 | 展开下拉框后，**整页除了下拉框其它一律点不动**；收起后**依然**点不动 | 应用行的展开面板在父容器还是 `Collapsed` 时就创建了 `ComboBox` ⇒ **弹出层（`Popup`）残留**，它铺在整个页面上吃掉所有输入 | ⛔ **懒创建**：顺序改成 先 `Visible` → 再创建 → 再挂树；收起前先 `IsDropDownOpen(false)` |
| 2 | v1 的 chevron"点都点不动" | `Button.Background(nullptr)` 后**只有内容字形**参与命中测试 | 整条标题带改用一个**透明底**（`Colors::Transparent()`）的画刷 |

⚠️ 排查这两条时**不能靠** `AddHandler(..., handledEventsToo: true)`：
C++/WinRT 的 `UIElement::AddHandler` 要的是 `IInspectable` 委托，直接传 C++ lambda **编译不过**；
最后是靠**给每个交互元素挂日志**判定"输入到底有没有到达"（也就是 §2 那几条 `[page]` 探针日志）。

## 4. 清除重定向：命令成功 ≠ 用户看得见

Eric 报"清除重定向没有用"，但日志显示命令**执行成功**（`CLEARREDIRECT` 结果 `0`；下拉里选「默认设备」也 `S_OK`）——
**真正的问题是界面毫无变化**。修法：

| 项 | 做法 |
|---|---|
| 可见反馈 | 底栏「清除重定向」→ 清页面本地选择 → **`MountPage` 重建正文** ⇒ 所有下拉框回到「默认设备」（日志 `已清除全部重定向并重置列表`） |
| 消竞态 | 下拉的**当前值改由页面本地 map 维护**（首次从 `redirects.ini` 播种，选择即更新、清除即清空）；⛔ 不再每次去读文件 —— 既省 IO，也消除"宿主删文件 / 页面重建"之间的竞态 |

## 5. 两条边界（都按"画事实"的原则定）

| 边界 | 决定 | 理由 |
|---|---|---|
| 系统声音行（`appKey = #system`，`pid = 0`）不给重定向 | UI 上**不出现 chevron / 下拉** | `pid=0` 的报文会被宿主判为非法；对齐 EarTrumpet 的 `IsMovable = !IsSystemSoundsSession` |
| 只有策略**真的生效**才写 `redirects.ini` | 三个 role 全失败 ⇒ 记 `逐应用重定向未生效(本机 RDP 限制?), 未记录状态` 且不落盘 | 否则会出现"UI 显示已重定向、实际没生效"的假象（本机 RDP 下**必然**全失败，所以这条天天在生效） |

## 6. 第三个真 bug：录制模式发的是**硬编码设备名**（本轮顺带挖出）

Eric 第二轮日志里冒出一条**不该存在**的报文：

```text
02:54:04.624 [INFO] [pipe] 收到报文 SETDEFAULT: SETDEFAULT render OC Virtual Speaker
02:54:04.625 [WARN] [audio.policy] 切换系统默认设备失败: OC (hr=0x80070057, role=0)
```

两个问题叠在一起：

1. **硬编码**：勾「录制模式」时页面直接发字面量 `OC Virtual Speaker`（V 的**显示名**，不是端点 ID）——违反"文本不硬编码"，
   而且 V 的 ID 本来就该由宿主解析（**页面侧枚举被"过滤 V"挡住，拿不到**）。
2. **被截断**：报文是**按空格切分**的，`OC Virtual Speaker` 被切成 `OC` + 多余两段；宿主取到 `OC` 去调策略
   ⇒ `E_INVALIDARG`。**多余段以前被静默忽略**，所以"命令发了、日志看着正常、实际全是垃圾"。

修法（两处一起改，缺一不可）：

| 位置 | 改动 |
|---|---|
| 页面 | 改发哨兵 **`@virtual`**（`Core/InjectionContract.h` 的 `kVirtualDeviceTarget`）；`@virtual` 由宿主解析 |
| 宿主 | `HandleSetDefault` 见到 `@virtual` → `IAudioDeviceManager::FindVirtualDevice`（`detail::FindVirtualEndpoint`，**不过滤 V**）取真实 ID；取不到就记 `未找到虚拟端点 V, 未切换默认设备 flow=…` 并放弃 |
| 解析器 | **收紧参数个数**：`SETDEFAULT` 必须 2 段、`SETREDIRECT` 2 或 3 段（2 段 = 清除该应用）、`CLEARREDIRECT` / `UNINSTALL` 0 段；不符 ⇒ 记 `报文参数不合法, 已忽略` 并丢弃 |

改动后重跑协议探针（宿主日志 `app-20261004-030240.log`，一次发 9 行）：

```text
03:02:57.415 [WARN] [pipe] 报文参数不合法, 已忽略: SETDEFAULT render OC Virtual Speaker   ← 修前：会拿 "OC" 去调策略
03:02:57.415 [INFO] [pipe] 收到报文 SETDEFAULT: SETDEFAULT render @virtual
03:02:57.429 [WARN] [pipe] 未找到虚拟端点 V, 未切换默认设备 flow=render                  ← 本机没有 V，如实回显
03:02:57.434 [INFO] [pipe] 切换默认设备 flow=render role=console 结果=3                  ← 真 ID 仍然照旧走策略
03:02:57.438 [WARN] [pipe] 报文参数不合法, 已忽略: SETDEFAULT bogus
03:02:57.444 [INFO] [pipe] 逐应用重定向 pid=1234 role=console 结果=3                     ← SETREDIRECT 2 段（清除）合法
03:02:57.444 [WARN] [pipe] 报文参数不合法, 已忽略: SETREDIRECT render 1234 {3.0.0.…} extra ← 4 段非法
03:02:57.444 [INFO] [pipe] 清空逐应用重定向 结果=0
03:02:57.445 [WARN] [pipe] 报文参数不合法, 已忽略: CLEARREDIRECT junk
03:02:57.445 [WARN] [pipe] 未知报文, 已忽略: WHATEVER hello
```

⇒ 五类分支（合法 / 参数不足 / **参数过多** / 未知动词 / 未实现）现在**都显式记日志**，不再有"半解析"。

## 7. 仍未验证

| 项 | 为什么 |
|---|---|
| **重定向真的把声音搬过去** | 本机在 RDP 里，只有 `远程音频` 一个渲染端点，`SetPersistedDefaultAudioEndpoint` → `E_INVALIDARG`（见 `15` 号 §4）⇒ 实体机复验 |
| **录制模式切到 V / 切回 R** | 本机**没有 V**（驱动层还没做）；现在至少"没有 V 时明确记日志、不乱发报文"已经成立 |
| 录制模式里"选 R"的语义 | ⚠️ **待定**：勾着时本行下拉框按设计是 **R 的选择器**，但现行实现两种模式都发 `SETDEFAULT` ⇒ 会把系统默认从 V 切走。需要先决定"选 R 是否顺带改默认输出"，并补一条把 R 传给监听程序的通道 |
| 逐像素外观对齐 | 只做了几何验收（`13` 号）+ Eric 目视确认，没有专门的截图比对 |
