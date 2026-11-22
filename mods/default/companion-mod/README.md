# 优化伴侣

精细专家模式调整邻接加成及手动战法；提供规则、游戏加速、官职、府官、报告演出、立绘及看海势力接管。上限、阵型、异族编辑及势力颜色由「增强设定与编辑」提供。

版本 `0.2.1`，内部 ID `companion-mod`。本目录属于 `default` 分组。

启用“历史事件自动发生”后，配套 GameAdapter 会在看海时自动确认历史事件对白，每段留短暂间隔，保留原版事件结果。切回自己操作时仍由玩家确认；关闭该开关后恢复原版行为。此行为需要包含看海对白确认能力的 GameAdapter，单独更新插件不能补齐旧适配器的能力。

从仓库根目录构建与打包：

```powershell
./build.ps1 -Target mods -Mod companion-mod -Tests -ArtifactRoot build/companion-mod
```

作者：[清都jsl](https://space.bilibili.com/3690994744691528)；贡献者：[纸箱墨镜还有狗](https://space.bilibili.com/230969570)。

「电脑和军团99命令书」已移到「电脑智商优化 → 常规」第一页。本 Mod 不再提供该项；配套 Host 会迁移原启用偏好，避免两个 Mod 同时控制此规则。
