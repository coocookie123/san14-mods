# MCP

可选的本机 MCP 连接服务。启用后监听 127.0.0.1:8765；通过共享 Base 执行已验证的游戏查询和命令。

版本 `0.2.0`，内部 ID `mcp-mod`。本目录属于 `default` 分组。

从仓库根目录构建与打包：

```powershell
./build.ps1 -Target mods -Mod mcp-mod -Tests -ArtifactRoot build/mcp-mod
```

作者：[清都jsl](https://space.bilibili.com/3690994744691528)。
