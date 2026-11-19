# Mod 存档附加数据

SDK 的 `storage.read/write_atomic` 现在支持 `save`、`playthrough`，并提供可选尾部服务 `host->save_storage` 查询状态和删除键。管理器观察原版存读档流程，管理附加文件；Mod 不需要知道存档目录、Steam 账号或槽位。

## 使用语义

| scope | 用途 | 持久化与回退 |
|---|---|---|
| `global` | 跨剧本设置、收藏、预设 | 保持原有插件全局目录和立即落盘行为 |
| `save` | 当前游戏进度、统计、任务等 | 在内存中更新；原版保存成功时，将保存开始时的快照写入对应附加文件 |
| `playthrough` | 带本局身份的 Mod 数据 | 与本局 UUID 一起进入每份存档；读旧档也回退，另存保留 UUID；不合并不同分支的最新数据 |

`save` 和 `playthrough` 是两个独立的逻辑数据区，均按 Mod ID 隔离。写入成功只代表当前内存更新成功，不代表原版游戏已经保存。暂时禁用或尚未安装的 Mod 的已有数据会原样带入下一次存档。

每次原版存档旁生成一个附加文件，例如：

```text
svdexceSC00.s14
svdexceSC00.s14.san14mods
autosdexceSC50.s14
autosdexceSC50.s14.san14mods
```

原版文件的内容和格式保持不变。附加文件包含格式版本、本局 UUID、原版文件 SHA-256、按 Mod 分区的数据及完整性校验。覆盖槽位只更新该槽附加文件；读回其他槽位恢复该槽数据。没有附加文件的旧档可以正常使用，从空数据开始；相同剧本重新开局获得新的 UUID。

在管理器正常参与的游戏内覆盖保存中，原版保存成功后，同名`.san14mods`由当前保存快照完整替换；不会为每次覆盖另留一份旧附加文件。另一局覆盖该槽时，附加文件也随当前局切换。当前快照里的统计、事迹和停用Mod数据仍按存储协议保留，不因“清理旧槽”丢弃。保存失败不提前删除旧附加文件，正常写入的临时文件在提交后改名、异常写入时尝试清除。其它槽位、共享全局设置和已安装Mod资源不属于该槽专属文件，不随覆盖删除。

这管理的是通过 SDK 提交的二进制数据。JSON、计数、序列化对象和自定义文件内容均可作为值提交；任意文件系统写入不会被截获。复制、备份或跨电脑迁移时应同时携带原档和 `.san14mods` 文件。外部重命名、删除原档，以及 Steam 云同步并不会自动操作附加文件；本功能不承诺 Steam 上传这些自定义文件。

## 接入顺序

1. 清单声明 `storage.save` 和/或 `storage.playthrough` 权限。
2. 检查 Host `struct_size` 覆盖 `save_storage`，其指针非空，服务 `struct_size` 覆盖所需函数且函数非空。原有内嵌 `storage` 表布局未改变。
3. 在启用后、UI 线程调用 `save_storage->context`。确认 `state == SAN14_STORAGE_READY`；保存返回的 `revision`。
4. 使用 `storage.read` 读取自己的键。只有 `SAN14_E_NOT_FOUND` 表示没有该键；其他错误不能当成零值后覆盖。
5. 每次修改立即调用 `storage.write_atomic` 更新当前内存，成功后更新自己的缓存。不要等到 `SAVE_SAVING` 通知再提交。
6. 在 `STORAGE_CHANGED` 通知或 `UI_IDLE` 中重新查询 context；revision 改变时丢弃旧局缓存，重新读取。

`context` 会为该 Mod 记录当前 revision。后续按档 read/write/erase 在同一存储锁内检查这个 revision；若中间发生了切档，返回 `SAN14_E_STALE_GENERATION`。Mod 需要重新查询并重读，不能把旧缓存直接重试写入。通知中的 revision、`save_storage` revision 和 `world.current_generation` 是不同概念。

```cpp
San14SaveStorageInfo info{};
info.struct_size = sizeof(info);
auto result = host->save_storage->context(host->host_context, &info);
if (result != SAN14_OK || info.state != SAN14_STORAGE_READY) return;
// revision 变化时先清除本地缓存，再 read("save", "statistics")。
// write_atomic("save", "statistics", bytes, size) 由 Host 复制数据。
```

完整可编译示例：[save-storage](../examples/save-storage/README.md)。示例提供计数器，可用“增加 → 原版保存 A → 再增加 → 保存 B → 读 A”检查回退。

## 接口约束

- 按档 API 只允许已启用插件在 UI 线程调用；`on_load` 尚未启用，不能读取当前局。
- 键是 1–128 字节 ASCII 字母、数字、`-_.`；禁止 `.`、`..`，不接受目录路径。值可以为零字节。
- 每个值最多 1 MiB；每个 Mod 合计最多 16 MiB；整局最多 64 MiB、65,536 条，配额含条目元数据。超限拒绝本次写入。
- `read` 的空缓冲区查询返回需要的大小，非空值返回 `BUFFER_TOO_SMALL`；零长度值可直接返回 `OK`。
- `save_storage->erase(context, scope, key)` 只删除本 Mod 当前局中的键，下次成功保存才持久化。不存在的键返回 `NOT_FOUND`。
- `San14SaveStorageInfo` 提供状态、revision、本局 ID、当前保存文件名和限额，不暴露账号或完整路径。新局尚未保存时文件名为空。

## 事件与错误

开放 `SAVE_LOADING`、`SAVE_LOADED`、`SAVE_UNLOADED`、`SAVE_SAVING`、`SAVE_SAVED`、`SAVE_FAILED`、`STORAGE_CHANGED`、`NEW_GAME`。载荷为 `San14StorageEventInfo`；订阅需要任一种按档存储权限。它们由 Host 在 UI pump 中分发，原生工作线程不会调用插件。

事件是已观察操作的通知，可能晚于实际发生时间。处理时应查询当前 context，不能假定队列中较早事件仍代表当前状态。`SAVE_SAVING` 不提供同步刷新机会；保存快照在原版保存 worker 开始时已经冻结。`SAVE_SAVED` 只有原版保存和附加文件写入都成功才发布。

`NEW_GAME`（`0x0108`）只在管理器确认原版剧本加载成功、随后首次进入有效地图时发布一次，同一 revision 的 `STORAGE_CHANGED` 在前，本次 pump 的 `UI_IDLE` 在后。载入没有附加文件的旧档、指纹不匹配后建立空数据，以及中途挂接已有地图，都不会生成此事件。事件不会因启用 Mod 而重放；空文件名和新生成的本局 UUID 本身不能证明是开局。

需要开局基准的 Mod 应在 `on_enable` 读取并记录启用时的 revision；只接受 revision 严格大于该值、与当前 READY context 一致且成功的 `NEW_GAME`，避免晚启用时接收尚未分发的旧事件。事件回调中立即读取初值并写入按档存储，不要延迟到 `UI_IDLE`。没有可信基准时应显示“未记录开局基准”，不能用启用时现值冒充开局值。禁用后再启用可读取已有基准，但不能据空数据补建历史。

存盘快照仍在原版 worker 开始时冻结。若原版存盘先于新局通知分发，已经开始的那次保存不包含稍后捕获的基准；数据仅能进入后续保存，不能回写或宣称已补齐先前快照。

读档开始立即撤销旧数据；加载失败、损坏/未知版本附加文件、无法确认原版文件路径或原生字节指纹时进入 `ERROR`，不会把旧局数据留给新局。附加文件与原档指纹不同（例如原版单独覆盖过槽位）时创建空数据并报告 `STALE_GENERATION` 警示。原版保存失败不会提交附加文件；附加写入失败时保留内存数据并报告错误。

原档和附加文件是两个文件，无法作跨文件原子提交。附加文件本身采用同目录临时文件、落盘和原子替换；若两次写入之间断电，下次读取用原档指纹拒绝关联过时数据。

## 当前适配与验证

当前只适配管理器已有的简中 Build 25953386。保存/读取 worker、Steam 流读取完成和写入结果位置均按固定映像代码范围校验。手动保存和自动保存共用原版保存 worker；文件名只接受本版本两类存档格式。

原档目录由本机 Steam 安装位置及活动账号解析，随后必须与本次原版实际读写字节的 SHA-256 相符才允许关联。无法解析或不匹配时不扫描其他账号猜测文件。

核心、会话、事件、Host 权限与原生适配器使用离线回归测试。离线模拟不等价于实机全流程验收；原生保存/读档与各 Steam 运行模式的实机覆盖需按发布验证记录判断。

### 显式编辑已有存档的附加记录（可选尾部）

`San14SaveStorageApi.edit_saved`是可选追加字段，调用前检查服务`struct_size`与函数指针。需要`storage.saved-edit`及所访问的`storage.save`／`storage.playthrough`权限，限已启用Mod的UI线程。先调用`context`，提交该次revision；保存／加载进行中、旧revision或重入均拒绝。调用由Host解析当前账号目录，插件不提交或取得文件路径。

目标`CURRENT`要求当前局已绑定现存的原档；`ALL`枚举当前活动Steam账号、本版本认可的手动／自动原档。没有附加文件的原档可以创建新附加记录；损坏、未知格式或原档指纹不符的已有附加文件阻止整批写入，不能作为空数据覆盖。CURRENT还检查当前槽原档与实际载入／成功保存时的原档指纹一致。

Host先复制当前会话和每个目标的快照，分别调用`San14SavedDataTransform`。回调只能使用传入的临时`San14SavedDataAccess.read/write`访问本Mod自己的save／playthrough键，不得调用其它Host服务、重入、保留指针或操作UI。回调应按字段合并已有记录；缺键与损坏值必须区分。所有回调及完整性／额度检查完成后才发布。原档保持只读锁，原有附加文件先备份为`.before-随机ID`，每个目标原子替换。失败逆序回退；`San14SavedEditResult`报告目标数、仍更新的文件数及回退失败标志。多文件／断电不具备整体原子保证，不能仅凭非零更新数报告全部成功。临时数据合计超过256MiB拒绝，以限制UI事务占用。

成功时同时更新当前会话中本Mod提交的记录；不会把当前统计等其它未保存的进度写入旧槽，也不改变任何原生存档字节。正常存档继续沿用原有冻结快照协议。全局默认不由本接口处理，调用者须独立提交并处理失败；向玩家明确区分参数已持久化与当前局运行效果是否应用成功。
