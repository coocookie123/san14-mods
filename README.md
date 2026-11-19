# SAN14 Mods

三国志14 Mod 的统一源码仓库。管理器、SDK、MCP Worker 和各 Mod 都在这里修改、测试、提交，一次克隆即可取得完整公共源码，不使用子模块。

```text
manager/                 通用 Host、安装工具和地址无关的 MCP Worker
sdk/                     SDK 头文件、Schema、示例和插件构建工具
mods/
  default/               优化伴侣、存档数据统计、电脑智商优化、增强字体、增强设定与编辑、MCP
  optional/              一千个曹操、双城全明星对战
tools/                   统一打包工具
tests/                   默认／可选分组和打包检查
```

`default` 表示常规打包范围，各 Mod 的启用偏好仍由各自清单和用户设置决定。`optional` 不进入默认 Mod 包。组件独立版本记录在各自 `mod.json` 或 `project.json`，源码来源提交见 [sources.lock.json](sources.lock.json)。

实际游戏内存地址、版本布局、Hook 和原生命令适配实现位于独立的私有 GameAdapter 仓库。本仓不包含游戏文件、存档或该适配源码。运行游戏需要维护者提供的匹配 GameAdapter 二进制；编译公共代码不需要私有源码。

## 构建

需要 Windows x64、MSVC x64、Windows SDK 和 Python 3.12。`CompilerRoot` 指向 MSVC 的版本目录，也可使用配置好的 `SAN14_MSVC_ROOT`。

```powershell
git clone https://github.com/zhaoqxu/san14-mods.git
cd san14-mods
py -3.12 -m venv .venv
./.venv/Scripts/python.exe -m pip install -r requirements-build.txt
./build.ps1 -Tests -ArtifactRoot build/first
```

默认构建管理器、单 MCP Worker 和六个默认 Mod。产物为 `build/first/native/dinput8.dll`、完整 `build/first/SAN14Base/worker/`、`build/first/mods/default/*.s14mod` 和 `SAN14-mods-default.zip`。这份公共构建不会生成或附带 GameAdapter。

只改一个 Mod 时：

```powershell
./build.ps1 -Target mods -Mod mcp-mod -Tests -ArtifactRoot build/mcp-change
```

加入可选曹操 Mod：

```powershell
./build.ps1 -Target mods -Mod officer-temperament-mod -Tests -ArtifactRoot build/temperament
```

同时构建默认与全部可选 Mod：

```powershell
./build.ps1 -IncludeOptional -Tests -ScenarioFile '<你的原版 CE 剧本01文件>' -ArtifactRoot build/with-optional
```

双城制作器只读取用户提供的匹配原始剧本，生成的原生内容位于 `build/`，不进入源码仓。没有原始剧本仍可编辑、构建其它七个原生插件。

曹操 Mod 需要运行时提供 `world.officer_temperament`；SDK 使用兼容的可选表尾接口，旧适配器缺少该能力时明确拒绝加载此可选插件。它不影响默认 Mod 的构建和使用。

## 协作

直接从本仓开功能分支，在需要的目录修改后提交一个 PR，跨管理器、SDK 和 Mod 的配套修改可以一起审查。流程及修改位置见 [CONTRIBUTING.md](CONTRIBUTING.md)。源码统一不代表新的玩家版本已经发布；安装和游戏内验收分别记录。

安装前退出游戏，使用管理器安装流程部署同一构建的 Host、完整 Worker、匹配 GameAdapter 和所选 Mod。先在隔离测试目录验收，再替换实际游戏安装。
