# SAN14 Mod Manager

`manager/` 是管理器独立源码目录，与根目录 `sdk/` 和 `mods/` 同级。它包含通用 Host、插件装载、运行包打包、认证 IPC 和地址无关的 MCP Worker。

通常使用根目录 `build.ps1`，它会先生成 Worker 及完整性清单，再构建与之匹配的 Host。只改管理器时，可先运行根构建入口的 `-Target worker`，随后在同一 `-ArtifactRoot` 使用 `-Target manager -Tests`。

`all` 和 `manager` 构建会按本目录 `adapter.lock.json` 自动下载公开 Release 中的固定 GameAdapter，校验 SHA-256，并打包 `SAN14-runtime.zip`。不需要访问私有源码仓。下载／缓存／离线参数见根 README；`-SkipGameAdapter` 可仅构建通用核心。

`src/installer/` 包含 Windows Setup 及安装、修复和卸载测试。版本识别使用游戏 EXE 文件名、SHA-256 和构建号，运行时兼容性由 GameAdapter 判断。`./manager/build.ps1 -Installer -Tests` 可单独构建并测试安装器入口。根目录默认构建会将运行包和默认 Mod 封装成可运行的 Setup EXE。

`worker/` 中的 Python Worker 对游戏版本布局无依赖。真实内存和原生命令由独立私有 GameAdapter 提供；本目录不包含该实现。通用 Host 和 Worker 可以单独构建及运行合成适配器测试，实际游戏需要匹配的 GameAdapter 二进制。
