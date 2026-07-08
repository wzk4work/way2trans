# way2trans 开发规约

way2trans 是一个 Linux 划词 / 截图翻译工具（对标 macOS 的 Bob），使用 **C++20 + Qt 6 + CMake** 开发。
支持 X11（GNOME/KDE/Cinnamon/XFCE 等所有桌面的 X11 会话）和 Wayland（KDE Plasma、wlroots 系）。
**明确不适配 GNOME Wayland**（缺少 data-control 与 layer-shell 协议，不做模拟按键等妥协方案）。

完整设计方案见 [docs/DESIGN.md](docs/DESIGN.md)，改动涉及架构时先读它，架构变更需同步更新该文档。

## 技术栈（不要偏离）

- C++20，Qt >= 6.4（Core / Gui / Widgets / Network / DBus / Sql）
- 本地数据（历史记录、生词本）：SQLite（Qt Sql 的 QSQLITE 驱动），禁止另引 ORM 或其他数据库
- 构建：CMake >= 3.22 + Ninja，使用 CMake Presets
- Wayland 弹窗定位：LayerShellQt；Wayland 取词：data-control 协议（MVP 阶段通过 `wl-paste` 子进程）
- OCR：微信 OCR（运行时探测用户本机组件，走独立 helper 进程）、Tesseract（保底）、RapidOCR/ONNX Runtime（二期）
- 私有二进制（微信 OCR 等）两条铁律：**绝不打进任何安装包**；**绝不在主进程加载**，只能在独立 helper 进程中使用，主程序只认 stdio JSON 接口
- 测试：Qt Test
- 禁止引入：WebEngine/WebView、Electron/Tauri 类方案、GPL-only 的 Qt 模块（Qt Charts、Qt Data Visualization、Qt Virtual Keyboard 等）

## 常用命令

```bash
cmake --preset dev          # 配置（build/dev 目录，Debug + 警告全开）
cmake --build --preset dev  # 构建
ctest --preset dev          # 跑测试
./build/dev/bin/way2trans   # 运行
```

## 目录结构与分层规则

```
src/app/        程序引导：单实例、D-Bus 服务、托盘、生命周期
src/core/       平台无关业务逻辑：翻译服务、OCR 服务、配置、语言检测
src/platform/   平台适配层：接口 + 各后端实现（selection/ hotkey/ screenshot/ popup/）
src/engines/    翻译引擎实现（每个引擎一个类，实现 TranslateEngine 接口）
src/ocr/        OCR 后端实现
src/ui/         窗口与控件：弹窗、设置、截图选区覆盖层
tests/          单元测试，目录结构镜像 src/
docs/           设计文档
packaging/      AppImage / Flatpak / deb 打包配置
```

**分层铁律：**

1. 平台差异（X11 vs Wayland、桌面环境判断）**只允许出现在 `src/platform/` 内部**。
   `core/`、`ui/`、`engines/` 不得出现 `#ifdef`、`XDG_SESSION_TYPE` 判断或任何平台探测代码。
2. `core/` 不依赖 `ui/`；`platform/` 不依赖 `engines/`。依赖方向：`app → {ui, core, platform, engines, ocr}`，`ui → core`，`engines/ocr → core` 的接口。
3. 新增平台后端 = 在 `platform/<能力>/` 下新增一个实现类并在工厂里注册，不改动其他层。
4. 网络请求一律走 `core/` 注入的 `QNetworkAccessManager`，引擎内不得自建。

## C++ / Qt 代码风格

- 类名 `PascalCase`；方法与变量 `camelCase`；成员变量 `m_` 前缀；常量 `k` 前缀
- 文件名与类名一致（`TranslateService.h/.cpp`），头文件用 `#pragma once`
- 信号槽一律用新式指针语法 `connect(a, &A::sig, b, &B::slot)`，禁止 SIGNAL/SLOT 宏
- QObject 派生类通过 parent 管理生命周期；非 QObject 用智能指针，禁止裸 `new`/`delete`
- 不使用异常做控制流；可失败操作返回 `std::optional` / 带错误信息的 result 结构，或走信号上报
- 异步优先信号槽，不阻塞 UI 线程；子进程用 `QProcess` 异步接口
- 标识符与注释用英文；用户可见字符串一律 `tr()` 包裹（源语言英文，中文走翻译文件）
- 提交前跑 `clang-format`（配置在仓库根 `.clang-format`，Qt 风格）

## Git 规约

- Conventional Commits：`feat:` `fix:` `refactor:` `docs:` `build:` `test:` `chore:`，scope 用模块名，如 `feat(platform): add wayland data-control selection source`
- 提交信息用英文；一个提交只做一件事
- `main` 保持可构建；功能在分支上开发

## 其他约定

- D-Bus 服务名 / 应用 ID：`io.github.way2trans`（打包、桌面文件、portal 交互均使用此 ID）
- 配置文件：`~/.config/way2trans/way2trans.conf`（QSettings INI 格式）
- 日志用 `QLoggingCategory`，分类命名 `way2trans.<module>`，禁止 `qDebug()` 裸用
- 涉及 portal / Wayland 协议的行为差异，在代码注释里写明"哪个合成器、什么行为"，这是本项目最容易踩坑的地方
