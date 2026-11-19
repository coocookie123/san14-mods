# SAN14 Mod Manager

`manager/` 是管理器独立源码目录，与根目录 `sdk/` 和 `mods/` 同级。它包含通用 Host、插件装载、安装工具、认证 IPC 和地址无关的 MCP Worker。

通常使用根目录 `build.ps1`，它会先生成 Worker 及完整性清单，再构建与之匹配的 Host。只改管理器时，可先运行根构建入口的 `-Target worker`，随后在同一 `-ArtifactRoot` 使用 `-Target manager -Tests`。

`worker/` 中的 Python Worker 对游戏版本布局无依赖。真实内存和原生命令由独立私有 GameAdapter 提供；本目录不包含该实现。通用 Host 和 Worker 可以单独构建及运行合成适配器测试，实际游戏需要匹配的 GameAdapter 二进制。
