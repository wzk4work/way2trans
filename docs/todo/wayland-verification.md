# Wayland 会话待验证点

这些改动在 X11（Cinnamon）会话里已实测通过，但涉及的路径在 Wayland 下行为可能不同，
需要分别在 **KDE Plasma (Wayland)** 和 **wlroots 系（Sway/Hyprland）** 里验证。

开始前确认会话类型（规约要求，不匹配的会话里测试结果不可信）：

```bash
echo $XDG_SESSION_TYPE $XDG_CURRENT_DESKTOP
```

## 弹窗高度封顶 + 内部滚动（PopupWindow 改动，2026-07）

背景：划词弹窗现在把标题栏以下的内容装进 QScrollArea，整窗高度封顶为屏幕可用高度的
70%（`adjustPopupSize()`），超长结果内部滚动。相关代码 `src/ui/PopupWindow.cpp`。

- [ ] **LayerShellPlacement 定位尺寸**：`place()` 用 `PopupWindow::sizeHint()` 决定
      layer-shell surface 的尺寸/锚点，`sizeHint()` 已改为返回封顶后的高度。选一段
      很长的文本触发划词翻译，确认弹窗不超过屏幕 70% 高、定位不越界、不被裁切。
- [ ] **异步 configure 下的高度收敛**：弹窗高度靠 `setFixedHeight` 钉死，几像素的
      滚动残差靠竖直滚动条 `rangeChanged` 信号吸收（增长式）。X11 上窗口 resize 的
      回包（ConfigureNotify）是异步的，Wayland 的 configure/ack 时序不同——确认短
      结果弹窗最终稳定、无几像素的滚动条残留、无高度振荡（反复闪动地长高缩短）。
- [ ] **屏幕可用几何**：`screen()->availableGeometry()` 在 Wayland 合成器上往往等于
      整个屏幕（没有 X11 工作区概念），70% 上限是否仍合适；有面板（Plasma 任务栏）
      时弹窗底部是否会压到面板上。
- [ ] **流式结果**：LLM 引擎流式输出时（`applyPartial` 高频触发，合并后每个事件循环
      迭代最多调整一次尺寸），观察弹窗增长是否平滑、到达上限后是否稳定。

## 弹窗程序性移动（已知风险，非本次改动引入）

Wayland 不允许普通 toplevel 自行定位/移动，以下两处 `move()` 在 Wayland 下大概率
无效或行为异常，需确认实际表现并决定是否需要 layer-shell 专属路径：

- [ ] **拖动弹窗**：`PopupWindow::eventFilter` 里 drag-to-move 用 `move()` 实现
      （`src/ui/PopupWindow.cpp`）。在 layer-shell surface 上验证拖动是否生效；
      无效的话考虑通过 LayerShellQt 改锚点/margin 实现，或在 Wayland 下禁用拖动。
- [ ] **截图翻译锚定**：`Application.cpp` 里截图流程用
      `m_popup->move(m_captureAnchor.bottomLeft())` 把弹窗钉在选区下方。Wayland 下
      验证弹窗实际出现的位置；无效则该路径也要走 `m_popupPlacement`。

## 验证方法备注

- 长文本用例：选中一整段英文文章（≥500 字符）触发 Ctrl+Alt+D，多引擎同时启用。
- 短文本用例：选中一个单词，确认弹窗紧凑、无滚动条。
- 每个合成器测完后在对应条目后标注环境，如 `（KDE 6.x Wayland 通过）`。
