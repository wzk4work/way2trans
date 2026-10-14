# way2trans

[English](README.en.md)

way2trans 是一个 Linux 桌面划词 / 截图翻译工具，对标 macOS 的 [Bob](https://bobtranslate.com/)，使用 C++20 + Qt 6 开发，原生 UI、无 WebView、低内存占用。

## 功能

- **划词翻译**：选中任意应用中的文本，按快捷键，弹窗即显翻译结果
- **截图翻译**：快捷键框选屏幕区域，OCR 识别后翻译
- **静默截图 OCR**：框选识别文字，直接写入剪贴板，不弹窗
- **输入翻译 / 剪贴板翻译**：主窗口手动输入，或一键翻译剪贴板内容
- **多引擎并发**：Google / Bing（免 key，开箱即用）、DeepL、OpenAI 兼容的 LLM（支持流式输出），同一类型可建多个服务实例
- **词典卡片**：查单词时展示音标、词性、多释义
- **历史记录**：本地 SQLite 存储，全文搜索，可收藏，隐私数据不出本机
- **生词本**：星标收藏、查询次数统计、状态管理，支持导出 Anki TSV / CSV / JSON

## 平台支持

| 会话 | 桌面环境 | 支持 |
|---|---|---|
| X11 | KDE / Cinnamon / XFCE / MATE / GNOME-X11 等 | ✅ 完整 |
| Wayland | KDE Plasma 5.27+ | ✅ 完整 |
| Wayland | wlroots 系（Sway / Hyprland / River 等） | ✅ 完整* |
| Wayland | GNOME | ❌ 不支持 |

\* wlroots 系全局快捷键依赖合成器配置绑定命令。GNOME Wayland 缺少 data-control 与 layer-shell 协议，明确不适配；GNOME 用户请使用 X11 会话。

## 构建

依赖：CMake ≥ 3.22、Ninja、Qt ≥ 6.4（Core / Gui / Widgets / Network / DBus / Sql）。

可选依赖：LayerShellQt（Wayland 弹窗定位）、libtesseract（OCR 保底后端）、xcb-keysyms（X11 快捷键）、wl-clipboard（Wayland 取词运行时依赖）。

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev          # 运行测试
./build/dev/bin/way2trans   # 运行
```

可选依赖缺失时对应功能自动裁剪，不会构建失败。

## 使用

```bash
way2trans trigger translate-selection   # 划词翻译（可绑定到系统快捷键）
way2trans trigger translate-capture     # 截图翻译
way2trans trigger capture-ocr           # 静默截图 OCR 到剪贴板
way2trans trigger show-main             # 打开主窗口
way2trans --text "hello"                # 直接翻译指定文本
```

在系统 / 合成器的快捷键设置中把上述命令绑定到你习惯的按键即可（Wayland 下这是标准方式）。

## OCR 说明

- **微信 OCR**（可选，中文质量最佳）：运行时探测用户本机已安装的微信（Linux 微信 4.x）OCR 组件，在独立 helper 进程中调用。**本项目不分发任何微信组件**，探测不到时该后端自动隐藏。仅限个人使用。
- **Tesseract**：保底后端，安装 `chi_sim` 等语言包即可使用。

## 隐私

所有历史记录与生词本数据仅存储在本机（`~/.local/share/way2trans/`）。配置文件权限 0600。托盘菜单提供「暂停记录」开关。

## 许可证

GPL-3.0，见 [LICENSE](LICENSE)。
