# 贡献指南

公共改动统一在本仓开分支和 PR，无需同时向多个仓库提交。

| 目标 | 目录 |
| --- | --- |
| 管理器核心、插件装载、Worker 完整性和通用安装工具 | `manager/` |
| MCP HTTP、IPC、启停和基础查询整理 | `manager/worker/` |
| 公共 C ABI、Schema、插件构建和示例 | `sdk/` |
| 常规 Mod | `mods/default/<id>/` |
| 曹操及双城 | `mods/optional/<id>/` |

内存布局、版本地址、原生 Hook 或新原生命令的实现仍在维护者的私有 GameAdapter 中修改。公开代码只提交语义接口、实体 ID、复制后的数据和合成夹具。需要两侧配套时，在 PR 写明所需适配能力及版本；不要把内存实现复制进 Worker 或 Mod。

## MCP 开发

游戏内开关和连接地址属于 `mods/default/mcp-mod`；网络服务和十个基础查询的整理属于 `manager/worker/san14_worker`。其余领域语义由 GameAdapter 服务提供。新增工具或改参数时同步工具目录、Schema、语义接口和相应测试，私有侧修改单列依赖。

第一次按 README 完成默认构建，确认基线通过。随后针对修改的组件运行相关测试；涉及公共 ABI、存读档生命周期或 Hook 时补集成测试。功能定型后再做一次完整构建和打包核对。

默认构建自动取得 `manager/adapter.lock.json` 固定的公开 GameAdapter 编译件，并核对大小、SHA-256、ABI 和清单；公共 MCP／SDK／Mod 贡献不需要私有仓权限。改变适配器版本时，在一个 PR 中更新锁文件并验证相关能力；不要用 `latest` 替代已测试的版本。下载逻辑与运行包许可证保留分别由 `tests/test_fetch_adapter.py`、`tests/test_runtime_package.py` 验证。当前预览不提供可选曹操的运行时服务。

Worker 更新后必须用新的 `worker.integrity.json` 重编 Host；根构建入口会按顺序完成。不要混用不同构建的 Worker 与 Host。

## SDK 与 Mod

先检查 SDK 是否已有所需语义能力；缺少时先约定通用接口，再由 GameAdapter 实现。保持 ABI v1 既有前缀，只在兼容尾部扩展并检查 `struct_size`。不要用 `sizeof` 扩展后的整个可选表来拒绝仍能提供所需前缀的旧实现。

各 Mod 保持自己的 ID 和版本。新增 Mod 放在正确分组，提供清单、README 和必要测试。默认分组不自动改变用户已保存的启用偏好。作者和贡献者按真实贡献保留署名及姓名链接。

## 提交与联调

一个需求使用一个独立提交或最小有序提交组。PR 说明变更目的、组件版本、依赖、运行过的测试和待测范围。游戏版别、简繁、Build 和具体操作分别记录；无需所有游戏版本通过后才能开 PR，不支持的能力应明确返回不可用。

实机使用隔离安装和测试存档。确认 `get_capabilities`、`game_status`、相关工具的正常／拒绝路径；原生动作核对请求 ID、超时和实际结果，未知结果不得盲目重发。

上传前复核实际 Git 树及可达历史，不提交私人配置、凭据、用户邮箱、本机路径、日志、游戏文件、存档、转储或开发研究材料。提交作者使用本人的 GitHub 身份和已验证 noreply 邮箱。
