<!--
 readme编写规范：
  1. 所有readme都需要保留本说明且不可做任何翻译修改。主readme(中文版)是本说明的唯一源头(Source of Truth)，禁止将非中文版本中被修改的说明同步回主readme。非中文版的此说明需始终保持与主readme单向同步更新。
  2. 非中文版本的readme必须保留主readme(中文版)的所有视觉元素、HTML 标签、图标路径和排版格式，非中文版本的readme是中文版的完整对应精准同步翻译版，翻译不可笼统，不可修改原意，不可增减内容。
  3. 针对目标语言环境进行功能描述的本土化。
  4. 非中文版本的readme需要合理修改指向资源的路径，确保资源能够正确显示。
  5. 非中文版本的readme都需要保留并置顶本说明
  6. 非中文版本的readme都需要置顶以下元素，可以替换为相应的翻译版本但必须保留原意：
	    <div align="right">
	    <a href="../readme.md">For the latest updates, please refer to the Chinese README.</a>
	    </div>
  7. 任何关于项目内容的更新，必须首先在主readme(中文版)中完成。在主版确认无误后，再根据本规范同步至其他语言版本。
  8. 非中文版本的readme顶部的语言切换器仅保留指向主readme(中文版)的链接，不互相跳转。非中文版本的入口仅在主readme中统一显示。
-->
<div align="right">
  <a href=".github/readme_res/readme_en.md">English</a>
</div>
<div align="center">

# VolumeMixerExtender

**把快速设置里的音量面板，变成一台完整的混音台**

  <a href="https://github.com/EricZhang233/VolumeMixerExtender/releases/latest"><img alt="version" src="https://img.shields.io/github/v/release/EricZhang233/VolumeMixerExtender?label=version&color=512BD4" /></a>
  <a href="https://github.com/EricZhang233/VolumeMixerExtender/releases/latest"><img alt="downloads" src="https://img.shields.io/github/downloads/EricZhang233/VolumeMixerExtender/total?label=downloads&color=0078D4" /></a>
  <a href="https://github.com/EricZhang233/VolumeMixerExtender/stargazers"><img alt="stars" src="https://img.shields.io/github/stars/EricZhang233/VolumeMixerExtender?label=stars&color=E3B341" /></a>

  [![C++ 20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus)](https://isocpp.org/)
  [![CMake](https://img.shields.io/badge/Build-CMake-064F8C?logo=cmake)](https://cmake.org/)
  [![Windows 11](https://img.shields.io/badge/Windows-11%2026H2%2B-0078D4?logo=microsoft)](https://www.microsoft.com/windows)
  [![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg?logo=gnu)](https://www.gnu.org/licenses/gpl-3.0.html)

<a href="#highlights">亮点速览</a> • <a href="#quickstart">快速开始</a> • <a href="#features">功能详解</a> • <a href="#cli">命令行</a> • <a href="#architecture">技术架构</a> • <a href="#faq">常见问题</a>

</div>

---

<a name="highlights"></a>

## 亮点速览

*   🎛️ **入口即页面**：快速设置里点「选择声音输出」，不再落到系统原生声音页，而是直接进入自绘页面；按 `Win+Ctrl+V` 打开声音页也一样。
*   🔊 **默认设备随手换**：输出与输入设备各成一组列表，点一下设备行就切换系统默认设备，当前默认带强调色高亮。
*   🎚️ **端点与逐应用音量**：设备总音量、逐应用音量与静音、实时音量条都在同一页，不必再往系统设置里翻。
*   🔀 **逐应用输出重定向**：让某个应用单独走某台输出设备——「游戏走耳机、播放器走音箱」；已重定向的应用会常显它的去向。
*   🧹 **一键清除重定向**：底栏左格把全部应用恢复为默认设备，同时清除保存的应用音量偏好，恢复到 100%；设备总音量不变。
*   🪟 **独立管理器**：底栏齿轮打开 WPF 管理器；自定义页与系统页随时切换，管理器关闭即退出。
*   💻 **命令行管理**：`vmex_cli.exe` 覆盖注入、状态、端点、默认设备、重定向与开机自启，`-help` 与 `skill` 内置完整文档。
*   🚀 **双击即用**：`vmex.exe` 无控制台常驻、自动接入、单实例运行，不开窗口、不驻托盘。

---

<a name="quickstart"></a>

## 快速开始

### 环境要求

| 项目 | 要求 |
| :--- | :--- |
| 操作系统 | Windows 11 26H2（Build 26300）或更高版本，x64 |
| 运行依赖 | 主进程与 CLI 无额外运行依赖；WPF 管理器使用 Windows 11 自带的 .NET Framework 4.8，不携带运行时 |
| 管理员权限 | 普通运行不需要；安装或卸载录制模式驱动时单独提权 |
| 安装 | 不需要，压缩包解压即用 |

### 三步上手

1. **下载**：从 [Releases](https://github.com/EricZhang233/VolumeMixerExtender/releases) 取最新压缩包，解压到任意目录（路径含中文或空格都可以）。
2. **运行**：双击 `vmex.exe`。它没有控制台窗口，会在后台常驻并自动接入系统外壳。希望它跟着登录自动就绪，就点自绘页底栏的齿轮，在 **管理器 → 开机启动** 打开开关；也可直接运行 `vmex_manager.exe`。
3. **使用**：按 `Win+A` 打开快速设置，点「选择声音输出」，看到的就是自绘页面。

> 💡 习惯命令行？`vmex_cli.exe -help` 看全部命令，`vmex_cli.exe skill` 取面向脚本与 AI Agent 的接入文档。

---

<a name="features"></a>

## 功能详解

### 接管「选择声音输出」入口

*   **只换内容、不改导航**：识别到声音页出现后替换它的内容区，原内容被完整保存，切回系统页即可原样还回去。
*   **四条路径都认**：点入口按钮、按 `Win+Ctrl+V` 都能进自绘页；辅助功能、投影等其它页面保持系统原样，不会误接管。
*   **每次打开都重新判断**：面板每次打开都会重建，页面照样接管，不需要重启任何东西，也不会有「只能用一次」的毛病。

### 默认设备与端点音量

*   **「音量」层就是默认设备开关**：列出全部输出设备，点选设备行即把它设为系统默认设备，当前默认以强调色高亮。
*   **一台设备一行**：名称、静音按钮、音量滑块与实时音量条同排，拖动即时生效。
*   **输入侧默认收起**：采集设备收成「输入设备（N）」一行，点一下展开，不挤占输出列表的位置。
*   **「显示驱动名」开关**：设备行、重定向下拉与「→ 端点名」三处同步决定是否带驱动名后缀，默认开启，改完即记住。

### 音量合成器

*   **逐应用控制**：应用图标、名称、音量滑块与静音按钮排成一行，调哪个应用不必再去翻系统设置。
*   **图标来自程序自身**：提取不到时回落为名称首字母色块，不留空白。
*   **会话会自愈**：每次打开面板都重新枚举；音频服务重启、切换 Windows 用户后即使会话全部失效，也会自动重新取回并重新画出，而不是永久空白。
*   **空列表有落点**：当前没有应用发声时显示「（当前没有正在发声的应用）」。

### 逐应用输出重定向

*   **点重定向图标后展开**：在下拉里选目标输出设备；首项「默认设备」表示取消重定向、恢复跟随系统默认。
*   **去向常显**：已重定向的应用在名称下方显示一行「→ 端点名」，一眼看清它此刻走的是哪台设备。
*   **系统声音不参与**：`系统声音` 行不提供重定向。
*   **一键清除**：底栏左格「清除重定向」把全部应用恢复为默认设备，同时清除当前用户保存的应用音量偏好，将当前应用会话音量恢复到 100%；系统主音量与设备总音量不变。其他工具设置的应用偏好也会清除。
*   **双声道电平**：音量滑轨内嵌上下两条峰值电平，响度计使用提亮的系统主题色，音量条使用压暗的同一主题色。

### 独立管理器

录制模式位于输出设备列表末行，通过原生开关启用。开启后默认输出切到 VB-CABLE 的 CABLE Input（V），开启前的默认设备成为监听 R；逐应用混音区直接控制 V，应用音量和静音同时影响录制与监听，R 的总音量只影响监听。V 始终不出现在主设备列表中，仅开启录制时可在应用重定向菜单中选择。关闭时恢复开启前记住的默认设备，并将所有单独重定向到 V 的应用恢复为跟随默认，保留指向其他设备的重定向与应用音量。录制状态不持久化；录制期间使用独立保护进程，在宿主崩溃或被结束时尝试恢复默认设备并清理 V 重定向。

录制模式使用随包 `Driver/VBCABLE/` 中的 VB-Audio 官方 VB-CABLE 标准版，只保留 Windows 11 x64 所需的原版文件。管理器验证原版安装器及微软签名后打开官方安装窗口，请在其中点击 Install Driver 或 Remove Driver，操作需要管理员权限，完成后重启 Windows。无需测试签名或导入自签证书。后台宿主启动及安装完成后将三个虚拟端点全部置于禁用待机状态；开启录制仅临时启用 `CABLE Input`，`CABLE In 16 Ch` 和 `CABLE Output` 保持禁用。关闭录制先恢复默认设备及应用路由、停止监听，再禁用 `CABLE Input`；失败保留待清理状态供重试，宿主崩溃时保护进程也会尝试恢复和禁用。录制软件从默认输出（`CABLE Input`）的桌面音频或回环采集系统声，麦克风使用实体端点。VB-CABLE 是共享设备，这些启停操作也会影响使用它的其他程序；本程序不修改驱动。目标 Windows 11 26H2 的实体机音频验收尚未完成。

录制模式开关按配置完整性显示：驱动已安装并正常启动、CABLE Input 存在且硬件归属已确认、实体监听输出可用即可显示。Windows 枚举包含已禁用端点，CABLE Input 的待机禁用不影响开关显示；实际虚拟音频流在启用端点后创建，失败自动回滚。未安装或配置未就绪时整行隐藏；面板打开时及打开期间异步复查。安装、修复与诊断仍可通过管理器和 CLI 进行。`driver hide` 先退出录制再将三个端点禁用，`driver show` 先退出录制再恢复本程序有记录的禁用操作，用于手动诊断。

端点操作只处理已确认硬件归属的标准 VB-CABLE，无法确认时跳过并记录原因。`driver show` 仅恢复本程序有记录的隐藏操作，保留用户原本禁用的端点；记录存于用户配置下的 `HiddenCableEndpoints`，跨程序重启保留。安装器取消、操作未生效或失败时恢复操作前的端点状态；本程序卸载清理配置前也会先恢复自身隐藏的端点，恢复失败则保留记录供重试。旧版本未记录归属的隐藏状态需要通过 Windows 声音设置手动恢复。

管理器、CLI 和录制请求统一只连接当前会话中同一程序目录下的宿主；移动或复制程序目录后，请先退出旧目录中的宿主再启动新目录中的程序。

管理器标题栏关闭按钮左侧的“关于”图标展示当前版本、开发者、项目主页与第三方引用信息，并提供 VB-CABLE 来源、许可及捐款/购买入口。VB-CABLE 是 Donationware，欢迎支持作者；专业使用与企业或组织分发须遵守 [VB-Audio 官方许可条款](https://vb-audio.com/Services/licensing.htm)，其许可独立于本项目的 GPL。

点面板底栏齿轮，或运行 `vmex_manager.exe` 打开 WPF 管理器。设置页已从音量面板移除。管理器打开时会自动启动后台程序，可点击「打开音量面板」直接进入声音页，关闭后不留托盘或后台管理器进程；主进程与登录自启保持原入口，直接使用 Core。重复打开会激活已有窗口。

| 项目 | 说明 |
| :--- | :--- |
| **开机启动** | 注册或移除登录自启。开关状态取自自启项本身，在「任务管理器 → 启动」里被系统禁用也会如实显示为关 |
| **显示驱动名** | 设备名是否附带驱动名后缀，默认开启；下次打开面板生效 |
| **运行状态与启动** | 查询后台进程与面板接管状态；未运行时可启动程序 |
| **退出程序** | 结束接管并退出后台进程，成功后管理器也关闭；保留自启与数据 |
| **注册到开始菜单** | 在系统开始菜单创建管理器快捷方式，已存在则覆盖 |
| **录制模式驱动** | 查询 VB-CABLE 状态并打开官方安装器或卸载器；操作前退出录制模式 |
| **关于** | 标题栏图标打开版本、开发者与第三方组件来源、许可及支持入口 |
| **打开日志目录** | 打开本程序日志目录，方便排查问题 |
| **卸载并清理** | 普通确认框确认后停止接管，清除自启、配置、日志、缓存、全部应用重定向和音量偏好，保留程序文件；失败保留窗口与详情供重试 |

### 后台常驻

*   双击 `vmex.exe` 即启动，没有控制台窗口、没有托盘图标、没有主界面。
*   单实例：已经在运行时，重复启动会被拒绝，而不是开出第二份。

---

<a name="cli"></a>

## 命令行：桌面端与终端共用同一份核心

`vmex_cli.exe` 与 `vmex.exe` 链接同一个 Core（`vmex_core.dll`），读写同一份状态——命令行里改完，回到面板重新打开看到的就是新值。

随程序内置两份文档，不必再去找别的说明：

```powershell
vmex_cli.exe -help     # 完整命令与参数说明
vmex_cli.exe skill     # 面向脚本与 AI Agent 的接入文档
```

当前版本可以在命令行完成：注入与状态查询、后台进程（`start` / `stop` / `host`）、管理器（`manager`）、打开面板（`panel`）、日志目录（`logs`）、项目主页（`project`）、驱动名显示（`show-driver-name on|off`）、端点枚举（`devices`）、默认设备切换（`default`）、逐应用重定向与清除（`redirect` / `clear-redirect`）、录制模式（`recording`）、驱动管理（`driver`，含隐藏或恢复多余端点）、开机自启（`autostart`）及卸载清理（`uninstall --yes`）。完整清单与参数以 `-help` 为准，它随程序一起更新。

---

<a name="architecture"></a>

## 技术架构

**VolumeMixerExtender** 的每一行业务逻辑都只存在一份：后台宿主、命令行与 WPF 管理器共用同一个 Core。管理器只负责界面，不承担常驻服务。

```mermaid
flowchart TB
    subgraph Shell["壳层 · 零业务逻辑"]
        HOST["vmex.exe<br/>无控制台后台宿主"]
        CLI["vmex_cli.exe<br/>参数解析 / 输出格式化"]
        GUI["vmex_manager.exe<br/>WPF 管理器"]
    end
    subgraph Core["Core · 唯一业务层"]
        SVC["注入与监视 · 音频设备与会话<br/>端点策略 · 载荷部署<br/>开机自启 · 用户设置 · 日志"]
    end
    subgraph Payload["注入载荷 · 只负责渲染"]
        TAP["vmex_tap.dll<br/>自绘页面"]
    end
    HOST --> SVC
    CLI --> SVC
    GUI --> SVC
    SVC -->|部署并接入| TAP
    TAP -->|页面动作 · 命名管道| SVC
    SVC --> OS["Windows 音频 API · 注册表 · 启动文件夹"]
```

*   **实现**：C++20 Core + C# WPF 管理器，由 CMake 统一构建；发布包含 `vmex.exe`、`vmex_cli.exe`、`vmex_manager.exe`、`vmex_core.dll`、`vmex_tap.dll`、`vmex_launcher.dll` 和 `xamldiagnostics.dll`，请完整解压到同一目录。主进程和 CLI 不依赖 WPF。
*   **单一 Core 原则**：后台宿主与命令行共用业务层，功能天然对齐，日志统一记录。
*   **页面即载荷**：界面由注入到系统外壳内的模块渲染，跟随系统浅色/深色主题与强调色，不额外开窗、不驻留托盘。
*   **动作走本机管道**：页面上的写操作经命名管道交给宿主执行，页面自身不长期持有系统句柄。
*   **如实呈现事实**：默认设备与重定向都直接读系统当前状态，不另存一份本地镜像，也就不会出现「界面说的和系统做的不一致」。
*   **安全边界**：正常运行与系统外壳同完整性级别，不修补系统组件；仅驱动安装、卸载操作单独申请管理员权限。

---

<a name="faq"></a>

## 常见问题

<details>
<summary><b>为什么任务管理器里会有一个 vmex.exe？</b></summary>

它是后台宿主，负责接入系统外壳，并把页面上的操作落到实处。它没有窗口、没有托盘图标，也不联网上传任何内容。不需要时，点自绘页齿轮，在 **管理器 → 退出程序** 结束它即可。
</details>

<details>
<summary><b>怎么彻底退出，而不是留在后台？</b></summary>

点自绘页底栏齿轮打开管理器，再点「退出程序」：接管结束、后台宿主退出，管理器也会关闭；如有载荷，系统声音面板会短暂重启并恢复原生行为。「开机启动」开关不受影响。只关闭管理器不会停止后台宿主。
</details>

<details>
<summary><b>需要安装吗？会写注册表吗？</b></summary>

普通功能解压即用。开启「开机启动」会在「启动」文件夹创建快捷方式；用户设置（例如「显示驱动名」）写入 `HKCU\Software\EricSoft\VolumeMixerExtender`；日志与图标缓存写入 `%TEMP%\eric\VolumeMixerExtender`。程序从解压目录加载注入组件，接入系统外壳。录制模式的驱动需要单独安装，安装和卸载会请求管理员权限。
</details>

<details>
<summary><b>我的数据存在哪里？</b></summary>

| 内容 | 位置 |
| :--- | :--- |
| 用户设置 | `HKCU\Software\EricSoft\VolumeMixerExtender` |
| 程序配置 | `%LOCALAPPDATA%\VolumeMixerExtender\vmex.ini` |
| 缓存与日志 | `%TEMP%\eric\VolumeMixerExtender`：日志在 `log\`（一次进程会话一个文件），应用图标缓存在 `icons\`，可随时删除 |

逐应用重定向与系统默认设备都由 Windows 自己保存（系统持久默认端点表），程序不另存一份镜像。
</details>

<details>
<summary><b>升级怎么升？</b></summary>

先让它退出（**管理器 → 退出程序**），再用新版本包里的文件覆盖原目录，然后重新双击 `vmex.exe`。注入模块活在系统外壳进程里，需要先结束接管才能替换。
</details>

<details>
<summary><b>安全软件报警或拦截怎么办？</b></summary>

接入用的是系统自带的 XAML 诊断接口（与系统自身的扩展方式同源），但「把模块送入另一个进程」这个动作本身容易被安全软件判为可疑。若被拦截或误报，请为程序目录添加信任后重试。
</details>

<details>
<summary><b>应用多了要滚动很久？</b></summary>

这是当前的取舍：端点音量与应用列表共用一个滚动区，列表长了就要往下滚。后续版本会再考虑压缩或分组。
</details>

<details>
<summary><b>支持 Windows 10 吗？</b></summary>

不支持。需要 **Windows 11 26H2（Build 26300）** 或更高版本；版本不足时程序会拒绝启动并说明原因。
</details>

---

## 贡献与支持

### 构建与自测

需要 Visual Studio C++ 工具链、Windows SDK、CMake、PowerShell 7 与 .NET 10 SDK。首次构建下载固定 SHA-256 的 VB-CABLE 官方 Pack45，缓存于 obj/driver-deps/vbcable，只把 Windows 11 x64 所需的原版文件解压到产物的 Driver/VBCABLE；不再构建或分发自研测试驱动。管理器编译为 .NET Framework 4.8，使用 Windows 11 自带的运行时，发布不携带 .NET 或 Windows App SDK。`vmex_cli.exe about` 显示与管理器相同的关于信息，`recording` 和 `driver` 的完整用法见 `-help` 与 `skill`。

```powershell
cmake --preset vs18-x64 -DBUILD_TESTING=ON
cmake --build --preset debug
ctest --test-dir obj/vs18-x64 -C Debug --output-on-failure
```

CTest 验证卸载中途失败的保护行为、音频重置部分失败的结果、命令与真实本地管道、配置和资源；另有独立 WPF 窗口冒烟测试，检查实际控件、明暗主题、控件模板与布局。自测不清除用户音频偏好。真实设备上的重定向、应用重启后的音量持久性和不同缩放下的电平可读性仍需实机验收。

发布自动检出对应标签，先自测，再构建并逐项校验发布包，上传成功后更新说明；任一步失败退回草稿。本地 `output` 只存放构建结果，不需要提交压缩包。预发布满 48 小时后，还需确认软件包存在、对应发布流程成功，且没有 `block-release` 问题或 `[no-release]` 标记才会转正。

欢迎任何形式的贡献——无论是提交 Bug、改进文档，还是提交代码（PR）。

*   发现异常或有功能建议？前往 [Issues](https://github.com/EricZhang233/VolumeMixerExtender/issues) 反馈，附上复现步骤与 `%TEMP%\eric\VolumeMixerExtender\log` 里的日志会更快被处理。
*   想参与开发？工程使用 CMake + Visual Studio 构建，后台宿主与控制台共用同一套 Core，改一处即可两端生效。

如果你觉得这个项目对你有帮助，请给它由衷的一颗 ⭐️ **Star**！

<a name="thanks"></a>

## 特别致谢

*   [**EarTrumpet**](https://github.com/File-New-Project/EarTrumpet)：逐应用音量与输出端点的概念与交互参考。本项目在这一方向上重写了会话枚举策略——每次使用都重新枚举、不跨时间缓存失效的枚举器，以避免「应用列表丢失后永久空白」那一类经典问题。
*   [**VB-CABLE / VB-Audio Software**](https://www.vb-cable.com/)：录制模式使用其官方标准版虚拟音频驱动，随包分发 Windows 11 x64 所需的原版文件；VB-CABLE 是 Donationware，欢迎捐款或购买许可，使用与分发须遵守其 [独立许可条款](https://vb-audio.com/Services/licensing.htm)。
*   [**Windows App SDK / XAML 诊断接口**](https://learn.microsoft.com/windows/apps/)：本项目的接管能力建立在系统自带的 XAML 诊断与可视化树接口之上。

---

本项目以 [GPL-3.0](./LICENSE) 授权。

<div align="center">
  Made with ❤️ by EricZhang233
</div>
