# CLAUDE.md

@AGENTS.md

以上引入的 AGENTS.md 是本项目唯一的规约来源，所有约定以它为准（避免两份文件漂移，规约改动只改 AGENTS.md）。

Claude Code 补充说明：

- 动手改代码前，先确认改动落在 AGENTS.md「目录结构与分层规则」允许的层里；平台探测代码出现在 `src/platform/` 之外一律视为错误。
- 架构级改动（新增子系统、改依赖方向、换协议方案）需先更新 [docs/DESIGN.md](docs/DESIGN.md) 再实现。
- 本机验证 Wayland 专属路径（data-control、layer-shell、portal）时，注意当前会话类型：`echo $XDG_SESSION_TYPE $XDG_CURRENT_DESKTOP`，在不匹配的会话里测试结果不可信。
- UI 疑难 bug（样式表误伤、布局尺寸异常等）先写最小离屏复现再改产品代码：单文件 Qt 程序 + `QT_QPA_PLATFORM=offscreen` + `QWidget::grab()` 存 PNG，做"无样式 / 复现坏样式 / 候选修复"多变体对照，链接 `build/dev/src/*/lib*.a` 静态库可以直接复用真实控件（如 PopupWindow）。注意两个盲区：`grab()` 是全量重绘，滚动位块残影类问题只能在真实显示器上验证；offscreen 平台不回传窗口系统事件，异步 resize 时序问题的结论要再在 xcb 上跑一遍。
- 修 UI bug 后用真实应用验证：直接启动 `./build/dev/bin/way2trans`（后台运行），用 `gnome-screenshot -w` 截当前窗口给自己看。
