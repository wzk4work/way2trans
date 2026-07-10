# 对标 Bob：界面 / 功能 / 体验差距盘点

日期：2026-07-11
状态：调研结论，供 M4 之后排期参考；被采纳的条目按规约先更新 [DESIGN.md](DESIGN.md) 再实现。
资料来源：[Bob 官方文档](https://bobtranslate.com/guide/)（[翻译窗口](https://bobtranslate.com/guide/quickstart/translate.html)、[添加服务](https://bobtranslate.com/guide/advance/service.html)）、[Bob GitHub README](https://github.com/ripperhe/Bob)。

## 总体结论

way2trans 的架构与路线图已覆盖 Bob 的主干（多服务并列结果、词典卡片、历史/生词本、
多 OCR 后端、服务=模板+多实例）。真实差距集中在**高频小交互**和三个 DESIGN.md
未提及的功能点：静默截图 OCR、程序员向文本处理（驼峰/蛇形拆分）、弹窗快捷键体系。

## 一、界面与弹窗交互

Bob 弹窗构成：语言切换栏（含服务图标）→ 可编辑原文区 → 每服务一张结果卡；宽度可拖、
高度自适应带上限。way2trans 的 PopupWindow 结构一致，差距如下：

1. **弹窗内快捷键体系**（差距最明显）。Bob 全键盘可操作：`⌘C` 复制、`⌘R` 重试、
   `⌘S` 收藏、`⌘P` 钉住、`⌘W`/`Esc` 关闭、`⌘+/-` 调字号。way2trans 目前只有 Esc
   关闭、Ctrl+Enter 重翻；钉住/星标/复制均需鼠标。建议补全：Ctrl+P 钉住、Ctrl+S
   星标、Ctrl+C（无选区时）复制首个结果、Ctrl+滚轮或 Ctrl+=/- 调字号并持久化。
2. **语言对快速切换**。Bob 在弹窗顶部直接切目标语言并重翻；way2trans 语言对是全局
   设置，翻译页只有"去设置改"的提示 label。建议弹窗与主窗翻译页放轻量语言对切换控件。
3. **临时服务钉住**。Bob 语言栏右侧可放"偶尔用"的服务图标，点击临时启用一次、
   下次翻译自动收起。锦上添花，可后置。
4. **弹窗宽度用户可拖拽**。Bob 支持；PopupWindow 需确认，不支持则是小补充。

## 二、功能设计

### DESIGN.md 未提及、建议补进路线图

5. **静默截图 OCR + 截图 OCR**。Bob 三件套里 way2trans 只做了"截图翻译"。
   静默截图 OCR（框选 → 后台识别 → 直接进剪贴板，不弹窗）是重度用户使用频率最高的
   功能之一；普通截图 OCR 为识别后悬浮显示、不翻译。现有链路
   （ScreenshotBackend → CaptureOverlay → OcrService）全部复用，只需新增
   `capture-ocr` / `capture-ocr-silent` 两个 action 与快捷键。**成本极低、价值高。**
6. **驼峰/蛇形拆分 + 复制为变量名格式**。Bob 翻译前把 `camelCase`/`snake_case`
   标识符拆成词组再翻；英文结果可按 camelCase / UpperCamelCase / snake_case /
   kebab-case / MACRO_CASE 复制。对程序员用户（Linux 桌面主力人群）价值高，
   纯文本处理无平台依赖：拆分进 `core/TextCleaner`，复制格式进结果卡复制菜单。
7. **图片文件 OCR**。Bob 有"访达选图 OCR"（支持批量）。Linux 等价物：主窗口拖放
   图片识别 + CLI `way2trans --ocr file.png`（D-Bus/CLI 骨架已有，兼顾脚本用户）。
8. **OCR 智能分段**。Bob 按版面还原段落。TextCleaner 的合并断行是部分覆盖；
   微信 OCR 返回的文本行坐标（DESIGN §6.1 已留数据）可做按坐标真分段。

### Bob 有、建议明确列入非目标（DESIGN §1.2）

9. **替换翻译**（译文直接替换选中文本）：Wayland 无法向其他窗口注入内容，本质需要
   模拟按键，项目非目标已排除模拟按键方案。建议点名写入，避免反复讨论。
10. 二维码识别、连续识别、PopClip 类系统集成：低价值或无 Linux 对应物，不追。

### 已在路线图、无需动作

TTS 朗读与自动播放（M6+）、剪贴板监听（M6+）、词典卡片（M4 已落地）、
历史+收藏（M4 已落地）、插件系统（一期静态注册、二期再议）。

## 三、体验细节（均为小改动）

11. **主窗口 Enter 即翻译**：Bob 为 `↩︎` 翻译、`⇧↩︎` 换行；way2trans 目前
    Ctrl+Enter。查词场景 Enter 直翻顺手得多，建议对齐（或做成开关）。
12. **翻译完成自动复制**：Bob/pot 均有"自动复制首个结果到剪贴板"选项。
13. **历史页收藏过滤**：`favorite` 字段与右键收藏已有，缺"只看收藏"过滤入口。
14. **全局字号设置**：弹窗/结果区字号目前写死 14px，高分屏与视力需求下需要可调。

## 优先级建议

| 优先级 | 项 | 理由 |
|---|---|---|
| 高 | 静默截图 OCR、弹窗快捷键、Enter 翻译、自动复制 | 成本低、使用频率最高 |
| 中 | 驼峰拆分与复制格式、语言对快速切换、历史收藏过滤、图片文件 OCR | 差异化价值（程序员用户） |
| 低 | 临时服务钉住、字号调节、OCR 坐标分段 | 锦上添花 |
