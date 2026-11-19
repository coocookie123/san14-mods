# MCP Worker 语义协议 v1

Windows Host 启动 Worker，通过认证命名管道传输四字节小端长度加 UTF-8 JSON。Host 校验进程身份并为运行包固定清单摘要。MCP HTTP 服务由插件显式启用，仅监听本机。

Worker 向适配器请求以下方法：

| 方法 | 请求 | 响应 |
| --- | --- | --- |
| `semantic.catalog` | `{"version":1}` | 版本及标准 MCP 工具定义数组 `tools` |
| `semantic.invoke` | 版本、工具 `name`、结构化 `arguments` 和会话 `authority` | 标准 MCP 工具结果 |
| `semantic.snapshot` | 版本、基础查询 `tool` 和会话 `authority` | 此查询所需的版本化语义记录与快照元数据 |
| `semantic.status` | `{"version":1}` | `runtime_busy` 表示适配器仍有工作在执行 |

参数用城市 ID、武将 ID 等游戏业务标识表达。适配器验证方法、会话授权和游戏状态。HTTP 停用先关闭新请求入口，再等执行中的变更完成。传输中断时，命令结果可能未确定，客户端须核对游戏状态后再决定是否重试。

`semantic.snapshot` 的投影由适配器白名单控制，返回城市、武将、部队、势力、地形和区域等业务记录。地图列表达游戏格子信息；不返回对象字节、地址、布局配置或原生调用参数。`metadata` 沿用冻结快照的一致性与代际记录，不宣称原子采集。一次查询只捕获一次，再在 Worker 内筛选与格式化。

SDK 的 `tool.call` 与 HTTP 使用同一 Worker。SDK 工具调用不要求开启 HTTP；两种入口分别保留 Host 所签发的所有者授权。只有 `semantic.*` 白名单可穿过 Worker 管道；DLL 内私有解码回调无法从此管道调用。
