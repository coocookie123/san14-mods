# 基建规则调整

调整设施与陷阱的原版规则。每项都是独立开关，默认关闭，关闭即恢复原版。

## 陷阱近身触发

原版的陷阱、落石、猛火陷阱只要敌军进入 2 格内就发动，常常是打头的部队刚碰到范围边缘就触发，后面的部队还没进效果范围，陷阱白白用掉。

开启后，敌军要走到陷阱所在格或紧挨它的 6 格内才发动：

- 对所有陷阱类设施生效：陷阱、落石、猛火陷阱，以及火陷阱、火船（这两种本来就是 1 格，不变）。
- 伤害、状态异常和效果范围照原版。
- 双方的陷阱一样处理；设施说明里的「发动范围」也显示 1。
- 军乐台、箭楼、投石台、阵、砦等常驻设施没有「触发」，不受影响。

需要配套 GameAdapter 提供命名规则 `facilities.traps.adjacent_trigger`。目前只适用于简中传承版 1.0.2（Build 25953386）；适配器未提供该规则时开关返回「不支持」。

版本 `0.1.0`，内部 ID `facility-rules-mod`。本目录属于 `default` 分组。

从仓库根目录构建与打包：

```powershell
./build.ps1 -Target mods -Mod facility-rules-mod -Tests -ArtifactRoot build/facility-rules-mod
```

作者：[黑色曲奇](https://steamcommunity.com/profiles/76561198090135353)。
