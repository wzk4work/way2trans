# way2trans 设计方案

版本：v0.1（2026-07-08）
状态：初稿，随实现演进更新

---

## 1. 概述

way2trans 是一个 Linux 桌面翻译工具，对标 macOS 的 Bob，核心能力：

1. **划词翻译**：选中任意应用中的文本，按快捷键，在弹窗中显示翻译结果。
2. **截图翻译**：按快捷键框选屏幕区域，OCR 识别后翻译。
3. **输入翻译**：打开主窗口手动输入文本翻译。
4. **多引擎**：Google / Bing / DeepL / LLM（OpenAI 兼容 API）等，可同时展示多个引擎结果。

### 1.1 目标

- X11 与 Wayland 双平台，一份代码，运行时自动选择后端。
- 划词到出结果的延迟 < 1s（不含翻译 API 网络耗时），弹窗即点即用。
- 常驻内存 < 100MB，无 WebView。
- 引擎与 OCR 后端可插拔，新增引擎不触碰核心代码。

### 1.2 非目标（明确不做）

- **不适配 GNOME Wayland**：GNOME 不实现 data-control 与 layer-shell 协议，划词需要模拟按键、弹窗无法定位，体验残缺。GNOME 用户可使用 X11 会话。
- 不做鼠标划词自动弹出（Wayland 无法监听全局鼠标），交互统一为「选中 + 快捷键」。
- 不内置间隔重复（SRS）背单词系统——生词本只做「收集、管理、导出」，复习交给 Anki（提供导出格式对接，见 §8.3）。
- 不做 Windows / macOS 移植（架构上不排斥，但不投入）。

### 1.3 平台支持矩阵

| 会话类型 | 桌面环境 | 支持级别 | 说明 |
|---|---|---|---|
| X11 | KDE / Cinnamon / XFCE / MATE / GNOME-X11 等 | ✅ 完整 | 一级支持目标 |
| Wayland | KDE Plasma (5.27+) | ✅ 完整 | data-control + layer-shell + portal 全齐 |
| Wayland | wlroots 系（Sway / Hyprland / River 等） | ✅ 完整* | *全局快捷键依赖合成器配置绑定命令（Hyprland 有 GlobalShortcuts portal） |
| Wayland | GNOME | ❌ 不支持 | 见非目标；启动时检测到该环境给出明确提示 |

---

## 2. 总体架构

```
┌──────────────────────────────────────────────────────────┐
│ app/  Application(单实例) · DBusService · TrayIcon        │
└───────┬──────────────────────────────────────────────────┘
        │ 编排
┌───────▼───────────────┐   ┌──────────────────────────────┐
│ core/                 │   │ ui/                          │
│  TranslateService     │◄──│  PopupWindow(结果弹窗)        │
│  OcrService           │   │  MainWindow(输入翻译)         │
│  LangDetector         │   │  CaptureOverlay(截图选区)     │
│  Settings             │   │  SettingsPage                │
└──┬────────────────┬───┘   └──────────────┬───────────────┘
   │ 接口            │ 接口                 │ 接口
┌──▼──────────┐ ┌───▼──────────┐ ┌─────────▼────────────────┐
│ engines/    │ │ ocr/         │ │ platform/                │
│ GoogleEng.  │ │ TesseractOcr │ │ selection/ hotkey/       │
│ DeepLEng.   │ │ RapidOcr(二期)│ │ screenshot/ popup/       │
│ LlmEngine…  │ │              │ │ (X11 与 Wayland 各后端)   │
└─────────────┘ └──────────────┘ └──────────────────────────┘
```

分层规则见 AGENTS.md，核心一句话：**平台差异全部封死在 `platform/`，业务与 UI 只面向接口。**

### 2.1 后端选择策略

启动时 `PlatformFactory` 按以下顺序探测并实例化每类能力的后端：

1. `QGuiApplication::platformName()`（`xcb` / `wayland`）定大方向；
2. Wayland 下枚举 `wl_registry` 全局对象，确认 `ext_data_control_manager_v1` / `zwlr_data_control_manager_v1`、`zwlr_layer_shell_v1` 是否存在；
3. D-Bus 探测 portal 接口版本（`org.freedesktop.portal.GlobalShortcuts`、`org.freedesktop.portal.Screenshot`）；
4. 每类能力按优先级取第一个可用后端；全部不可用时功能降级并在设置页明示（例如快捷键降级为「请在系统设置中绑定 `way2trans trigger-selection` 命令」）。
5. 配置文件可强制指定某个后端（排障用）：`[platform] selection=wl-data-control` 等。

---

## 3. 核心流程

### 3.1 划词翻译

```
用户选中文本，按下快捷键
  → HotkeyBackend 发出 activated("translate-selection")
  → SelectionSource::fetchSelection()  (异步)
      X11:     QClipboard(Selection 模式) 读 PRIMARY
      Wayland: data-control 协议读 primary selection
  → 文本非空 → LangDetector 判定源语言 → 确定目标语言(配置的语言对规则)
  → TranslateService 并发调用已启用引擎
  → PopupWindow 立即弹出(骨架态)，结果流式填充，每个引擎一个卡片
```

要点：

- 弹窗**先弹再填**，不等网络返回，保证体感速度。
- 选区为空时静默或轻提示（气泡「未检测到选中文本」），不弹主窗口。
- 文本清洗：去掉 PDF/终端复制常见的硬换行（行尾无标点且下一行小写开头则合并）、多余空白；该逻辑在 `core/TextCleaner`，可在设置中关闭。

### 3.2 截图翻译

```
快捷键 "translate-capture"
  → ScreenshotBackend::captureFullScreen() 抓全屏(见 §6.3)
  → CaptureOverlay 全屏覆盖层显示截图，用户拖拽框选(体验同 Flameshot)
  → 裁剪区域 → OcrService::recognize(image, langHint)
  → OCR 文本 → 同 3.1 的翻译链路 → PopupWindow(附带识别原文，可编辑后重翻)
```

统一「自己抓全屏 + 自绘选区」而不是依赖 portal 的交互式截图，理由：各 portal 实现的框选 UI 不一致（wlr 的甚至没有），自绘保证三个平台体验完全相同。

### 3.3 输入翻译 / 剪贴板翻译

- 主窗口：输入框 + 语言选择 + 引擎结果区，快捷键 `show-main` 唤起。
- `translate-clipboard` 动作：读 CLIPBOARD（非 PRIMARY）走同一链路，作为 GNOME/受限环境的兜底入口。

---

## 4. 平台适配层设计（platform/）

四个能力接口，全部为 QObject、异步信号风格。

### 4.1 取词 SelectionSource

```cpp
class SelectionSource : public QObject {
    Q_OBJECT
public:
    virtual void fetchSelection() = 0;   // 异步，结果走信号
signals:
    void selectionFetched(const QString &text);
    void fetchFailed(const QString &reason);
};
```

| 实现 | 平台 | 机制 |
|---|---|---|
| `X11SelectionSource` | X11 | `QGuiApplication::clipboard()->text(QClipboard::Selection)`，Qt 原生支持，同步转异步 |
| `WlrSelectionSource` | Wayland KDE/wlroots | **MVP**：`QProcess` 调 `wl-paste --primary --no-newline`（wl-clipboard 内部走 data-control，无需焦点）。**二期**：用 wayland-scanner 生成 `ext-data-control-v1` 代码原生实现，去掉外部依赖 |

### 4.2 全局快捷键 HotkeyBackend

```cpp
class HotkeyBackend : public QObject {
    Q_OBJECT
public:
    // actionId: "translate-selection" | "translate-capture" | "show-main" | "translate-clipboard"
    virtual bool registerAction(const QString &actionId,
                                const QKeySequence &sequence,
                                const QString &description) = 0;
    virtual bool canRebindAtRuntime() const = 0;
signals:
    void activated(const QString &actionId);
};
```

| 实现 | 平台 | 机制 |
|---|---|---|
| `X11HotkeyBackend` | X11 | XGrabKey（xcb），处理 NumLock/CapsLock 修饰符组合 |
| `PortalHotkeyBackend` | Wayland KDE / Hyprland | `org.freedesktop.portal.GlobalShortcuts`（QDBus）。首次注册弹系统授权框，session token 持久化到配置避免重复授权 |
| `NullHotkeyBackend` | 任何环境兜底 | 不注册任何键；设置页显示指引：在系统/合成器快捷键设置中绑定 `way2trans trigger <actionId>`（经 D-Bus 转发到运行实例，见 §8）。Sway 用户的常规路径 |

三个后端可叠加：portal 注册失败的动作自动落到 Null 指引，不是整体二选一。

### 4.3 截图 ScreenshotBackend

```cpp
class ScreenshotBackend : public QObject {
    Q_OBJECT
public:
    virtual void captureFullScreen() = 0;  // 多屏时抓全部屏幕，附带各屏几何信息
signals:
    void captured(const QList<ScreenImage> &screens); // {QImage, QRect geometry, qreal dpr}
    void captureFailed(const QString &reason);
};
```

| 实现 | 平台 | 机制 |
|---|---|---|
| `X11ScreenshotBackend` | X11 | `QScreen::grabWindow(0)`，逐屏抓取 |
| `PortalScreenshotBackend` | Wayland | `org.freedesktop.portal.Screenshot`（`interactive=false`），读返回的 file URI 后立即删除临时文件。KDE 会弹一次授权（可记住） |
| `GrimScreenshotBackend` | wlroots 备选 | `QProcess` 调 `grim`，portal 不可用时使用 |

### 4.4 弹窗定位 PopupPlacement

```cpp
class PopupPlacement {
public:
    // 让 window 显示在“触发点附近”或配置的锚定位置
    virtual void place(QWindow *window, const QSize &hint) = 0;
    virtual bool supportsCursorPlacement() const = 0;
};
```

| 实现 | 平台 | 行为 |
|---|---|---|
| `X11CursorPlacement` | X11 | `QCursor::pos()` 旁弹出，无边框 `Qt::Popup` 风格窗口，防出屏钳制 |
| `LayerShellPlacement` | Wayland KDE/wlroots | LayerShellQt，layer=overlay，按配置锚定（默认屏幕右上角；可选居中）。**Wayland 拿不到全局光标坐标，跟随光标不可行**，这是平台限制不是缺陷，文档与设置页明示 |
| `PlainPlacement` | 兜底 | 普通窗口，记住上次位置 |

截图翻译场景例外：用户刚在 CaptureOverlay 上框选，**选区坐标是已知的**，弹窗定位在选区旁（两个平台都可做到，Wayland 下 overlay 本身是 layer surface，坐标系一致）。

---

## 5. 翻译引擎子系统（engines/）

```cpp
struct TranslateRequest {
    QString text;
    QString sourceLang;   // "auto" 或 BCP-47
    QString targetLang;
};
struct TranslateResult {
    QString engineId;
    QString translatedText;
    QString detectedSourceLang;
    std::optional<DictEntry> dict;  // 词典释义：音标/词性/释义列表/例句(单词场景)
    QString error;                  // 非空表示失败
};

class TranslateEngine : public QObject {
    Q_OBJECT
public:
    virtual QString id() const = 0;              // "google" / "deepl" / "llm-openai"...
    virtual QString displayName() const = 0;
    virtual QStringList configKeys() const = 0;  // 需要的配置项(apiKey/endpoint/model...)
    virtual void translate(const TranslateRequest &req) = 0;
    virtual void cancel() = 0;
signals:
    void resultReady(const TranslateResult &result);
    void partialResult(const QString &delta);    // LLM 流式输出
};
```

- `TranslateService` 持有启用的引擎实例，广播请求、聚合结果；单个引擎失败不影响其他引擎，失败卡片显示错误与重试按钮。
- 一期内置引擎：`GoogleWebEngine`（免 key）、`BingWebEngine`（免 key）、`DeepLEngine`（官方 API key）、`LlmEngine`（OpenAI 兼容：endpoint + key + model + 可编辑 prompt 模板，支持流式）。

### 5.1 免费引擎预置（开箱即用）

零配置是硬性要求：首次启动不填任何 key 就能翻译。

- **默认启用 `GoogleWebEngine` + `BingWebEngine`**，互为备份。
- `GoogleWebEngine`：走 `translate.googleapis.com/translate_a/single?client=gtx` 免 key 接口，能返回词典数据（音标/词性/多释义），是 `DictEntry` 的主要来源。**endpoint 做成可配置**：国内网络直连不可用，允许用户填自建反代/镜像地址，仅换域名、协议不变。
- `BingWebEngine`：Bing 网页端接口（先取页面获得 IG/IID token 再调 `ttranslatev3`），token 缓存并过期自刷新。国内直连可用，作为无代理环境的默认主力。
- 免 key 接口本质是逆向 web 端，随时可能变更：请求封装收敛在各引擎单文件内，改动面小；返回非 200/结构异常时指数退避并在卡片上明示「接口可能已变更，请检查更新」。
- 频控自律：同一请求文本 30s 内命中内存缓存不重发；并发引擎数即上限，不做批量轰炸，降低被封风险。
- 引擎在 `EngineRegistry` 静态注册；二期如需第三方扩展再考虑 QPluginLoader 动态插件，一期不做。
- 短文本单词（≤2 个词）优先请求词典型结果（`DictEntry`），Bob 的核心体验之一。
- 语言检测 `core/LangDetector`：一期用书写系统启发式（CJK/西里尔/拉丁… + 常用词表）区分「中↔外」场景已够；拉丁语系细分交给引擎的 auto。

## 6. OCR 子系统（ocr/）

```cpp
class OcrBackend : public QObject {
    Q_OBJECT
public:
    virtual void recognize(const QImage &image, const QStringList &langHints) = 0;
signals:
    void recognized(const QString &text);
    void failed(const QString &reason);
};
```

后端一览（设置页可选默认后端，探测不到时自动降级到下一个可用项）：

| 后端 | 依赖 | 中文质量 | 定位 |
|---|---|---|---|
| `WeChatOcr` | 用户本机微信的 OCR 组件（不随包分发） | ★★★★★ | 检测到可用时的**推荐默认** |
| `TesseractOcr` | libtesseract + 语言包 | ★★☆ | 无微信环境的保底，一期实现 |
| `RapidOcr` | ONNX Runtime + PP-OCRv4 模型 | ★★★★ | 二期，无微信环境的高质量方案 |

- `TesseractOcr`：libtesseract 直链，语言包检测（缺 `chi_sim` 等给出安装指引），跑在 `QThreadPool` 避免卡 UI。预处理：2x 放大 + 灰度 + 自适应二值化（小字号屏幕文本识别率关键）。
- `RapidOcr`：模型文件（约 15MB）首次使用时下载到 `~/.local/share/way2trans/models/`。
- 所有 OCR 结果统一过 `TextCleaner`（合并断行、去误识别噪声字符）。

### 6.1 微信 OCR 后端（WeChatOcr）

微信自带的 OCR 引擎中文识别质量极佳且纯本地推理，社区已有成熟的提取/调用方案（如 GitHub 的 swigger/wechat-ocr 及各语言绑定，Linux 原生微信 4.x 自带所需组件）。设计要点：

- **不分发、只借用**：微信的 `.so` 是腾讯私有组件，**绝不打进安装包**。运行时探测用户本机微信安装目录（`/opt/wechat`、Flatpak `com.tencent.WeChat` 等常见路径），也允许在设置页手动指定路径；探测不到则该后端不可见。文档明确说明该功能依赖用户已安装的微信、仅限个人使用。
- **进程隔离**：私有二进制的稳定性与行为不可控，一律运行在独立辅助进程 `way2trans-ocr-helper` 中，主程序与其通过 stdio JSON 协议通信（发图片路径/接文本+坐标），helper 崩溃只导致单次识别失败并自动降级到下一后端，绝不拖垮主程序。helper 内部按社区方案与微信 OCR 组件的 mojo IPC 对接。
- **版本适配面收敛**：微信升级可能改动组件路径/协议，全部适配逻辑（各版本路径、协议差异）封在 helper 内，主程序只认 stdio JSON 接口。
- 返回结果带文本行坐标，为后续「译文贴原位」类功能（M6+）留好数据。

## 7. UI 设计（ui/）

- **PopupWindow**：无边框圆角卡片。顶部原文（可折叠）+ 语言对与切换；下方每个引擎一张结果卡（引擎名、结果、复制按钮、朗读按钮二期）；`Esc`/失焦关闭（layer-shell 下用 keyboard interactivity=on_demand 拿 Esc）。固定态按钮（📌）：钉住后失焦不关。
- **CaptureOverlay**:全屏截图冻结帧 + 变暗 + 拖拽选框 + 尺寸提示，多屏各一个 overlay。X11 下全屏无边框窗口；Wayland 下 layer-shell overlay 层 + 独占键盘（Esc 取消）。
- **MainWindow**：左侧导航栏 + 堆叠页（主流划词翻译工具的布局）——翻译（输入翻译）/ 历史 / 生词本（后两者见 §8）/ 设置（SettingsPage）。
- **SettingsPage**（主窗口内嵌页，左侧二级分区列表）：常规（语言对、启动项）/ 快捷键（含 Null 后端时的绑定指引与一键复制命令）/ 服务（每引擎一张可展开卡片：启停、上下移排序、key 配置、连通性测试按钮）/ OCR / 平台（后端探测结果展示与强制覆盖）。引擎配置改动经 app 层防抖后热重载引擎集。
- **TrayIcon**：StatusNotifierItem（Qt 自动走 SNI/D-Bus，KDE/XFCE/Cinnamon 都支持），菜单含各动作入口。
- 主题跟随系统（Qt 6.5+ `colorScheme`），深浅色两套样式表。
- **国际化**：所有用户可见字符串用 `tr()`、源串英文；`zh_CN` 翻译随功能同步维护（`translations/way2trans_zh_CN.ts`，构建时 `lrelease` 成 `.qm` 嵌入 `:/i18n`，运行时按系统 locale 加载）。开发规约见 AGENTS.md「国际化」。一期只维护中文，其他语言 M5 补齐。

## 8. 数据持久化：历史记录与生词本

### 8.1 存储层（core/storage/）

- **SQLite**，经 Qt Sql 模块（`QSQLITE` 驱动，Qt 自带）访问，数据库文件 `~/.local/share/way2trans/way2trans.db`，权限 0600。
- WAL 模式；写入量极小（每次翻译一行），全部在主线程用预编译语句完成，不引入线程复杂度。
- Schema 版本管理用 SQLite `user_version` + 顺序迁移脚本（`storage/migrations.cpp`），启动时自动升级。
- 结构：`Storage`（连接与迁移）→ `HistoryStore` / `VocabStore`（各自的 CRUD + 查询），`core/` 内部服务，UI 通过 model（`QAbstractListModel` 派生）消费。

```sql
CREATE TABLE history (
  id           INTEGER PRIMARY KEY,
  created_at   INTEGER NOT NULL,          -- unix epoch
  trigger      TEXT    NOT NULL,          -- selection | capture | input | clipboard
  source_text  TEXT    NOT NULL,
  source_lang  TEXT, target_lang TEXT,
  engine_id    TEXT,                      -- 展示用主结果来自哪个引擎
  result_text  TEXT,
  favorite     INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX idx_history_created ON history(created_at DESC);
CREATE VIRTUAL TABLE history_fts USING fts5(  -- 全文搜索
  source_text, result_text, content='history', content_rowid='id');

CREATE TABLE vocab (
  id            INTEGER PRIMARY KEY,
  word          TEXT NOT NULL,
  lang          TEXT NOT NULL,            -- 词条语言
  phonetic      TEXT,
  dict_json     TEXT,                     -- DictEntry 序列化(词性/释义/例句)
  translation   TEXT,                     -- 无词典结果时退化存翻译文本
  context       TEXT,                     -- 收藏时的上下文句子(可为空,见 8.3)
  note          TEXT,                     -- 用户笔记
  tags          TEXT,                     -- JSON 数组
  status        INTEGER NOT NULL DEFAULT 0,  -- 0 生词 / 1 熟悉 / 2 已掌握
  added_at      INTEGER NOT NULL,
  last_seen_at  INTEGER NOT NULL,
  lookup_count  INTEGER NOT NULL DEFAULT 1,  -- 累计查询次数
  UNIQUE(word, lang)
);
```

### 8.2 历史记录

- **写入时机**：每次翻译请求得到首个成功结果后写一行；同一 `source_text` 在 10 分钟内重复触发只更新时间戳，不堆积重复行。
- **保留策略**：默认保留 1000 条，超出按时间淘汰（收藏的不淘汰）；条数可配置，可设为「不限」或「关闭历史」。
- **隐私**：划词内容可能敏感——托盘菜单提供「暂停记录」快速开关；设置页提供一键清空；数据永远只在本地。
- **UI（MainWindow 历史页）**：搜索框（走 FTS5）+ 按日期分组的列表；单击条目回填到翻译页重新翻译；右键菜单：复制原文/译文、收藏、加入生词本、删除。

### 8.3 生词本

- **入口**：PopupWindow 词典卡片右上角星标（☆/★）一键加入/移除；历史页右键；主窗口手动添加。
- **重复查询即信号**：查词时发现该词已在生词本 → `lookup_count++`、刷新 `last_seen_at`、弹窗星标点亮。生词本列表支持按 `lookup_count` 排序——查得越多说明越没记住，这是比人工标记更诚实的优先级。
- **上下文**：`context` 存收藏时的完整选区文本（选中整句查词时天然获得）；只划了单词时拿不到所在句（PRIMARY 选区只含选中部分，平台限制），字段留空、详情页可手动补。
- **词条内容**：优先存引擎返回的 `DictEntry`（音标、词性、释义、例句）；纯翻译结果退化存 `translation`。词条与词典数据快照式保存，不依赖引擎在线可用。
- **管理 UI（MainWindow 生词本页）**：列表（词、音标、首条释义、状态徽章、查询次数）+ 详情面板（完整释义、上下文、笔记、标签）；按状态/标签筛选，按添加时间/查询次数排序；状态流转 生词→熟悉→已掌握 快捷键循环切换。
- **导出 / 备份**：
  - **Anki TSV**：字段 `word | phonetic | definitions | context | note`，Anki 直接导入建卡——复习功能由此外包，本项目不做 SRS；
  - CSV（通用表格）与 JSON（全量备份，含全部字段，可重新导入合并，`UNIQUE(word,lang)` 冲突时保留较高 `lookup_count`）。
  - 二期可选 AnkiConnect（本地 HTTP 8765）一键推送，做成可选集成不做依赖。

## 9. 单实例、D-Bus 与 CLI

- D-Bus 服务名 `io.github.way2trans`，对象 `/io/github/way2trans`，接口方法：
  `TriggerSelection()` / `TriggerCapture()` / `TriggerClipboard()` / `ShowMain()` / `Translate(s text) → s`。
- 启动时若服务名已被占用：把本次 CLI 参数转发给已运行实例后退出（单实例机制本体，不再额外用锁文件）。
- CLI：`way2trans trigger <action>`、`way2trans --text "hello"`。这既是 Null 快捷键后端的落点，也天然支持脚本化。

## 10. 配置（core/Settings）

- `~/.config/way2trans/way2trans.conf`，QSettings INI，文件权限 0600（含 API key）。
- 分组：`[general]` 语言对规则、开机自启；`[hotkeys]`；`[engines/<id>]` enabled/order/key…；`[ocr]`；`[platform]` 后端强制覆盖；`[popup]` 锚定位置。
- API key 一期明文存本地（0600 + 文档说明），二期可选 QtKeychain。
- 配置变更全部通过 `Settings` 单例信号广播，无需重启生效。

## 11. 依赖与构建

| 依赖 | 用途 | 必需性 |
|---|---|---|
| Qt 6.4+ (Core/Gui/Widgets/Network/DBus/Sql) | 框架（Sql 用于历史/生词本） | 必需 |
| LayerShellQt | Wayland 弹窗/overlay 定位 | Wayland 必需，编译时可选(无则 X11-only 构建) |
| libtesseract + leptonica | OCR 保底后端 | 可选（无则依赖微信 OCR） |
| 微信 OCR 组件（用户本机） | 高质量中文 OCR | 运行时探测，零编译期依赖，不随包分发 |
| xcb 系列 (xcb, xcb-keysyms) | X11 快捷键 | X11 必需 |
| wl-clipboard（运行时） | Wayland 取词 MVP | Wayland 运行时依赖，二期移除 |
| ONNX Runtime | RapidOCR | 二期可选 |

- CMake Presets：`dev`（Debug、-Wall -Wextra、ASan 可选）/ `release`（RelWithDebInfo、LTO）。
- 可选依赖一律 `find_package(... QUIET)` + feature summary，缺失时对应功能编译裁剪并运行时提示，**不硬性失败**——保证在最小环境也能构建出 X11 版本。
- CI（GitHub Actions）：Ubuntu 24.04 + Arch 容器双矩阵构建 + ctest + clang-format 检查。

## 12. 打包分发

1. **AUR**（`way2trans-git`）：Arch/KDE 用户重合度最高，最先做。
2. **AppImage**：linuxdeploy + Qt 插件，注意打入 wayland/xcb 双 QPA 插件与 LayerShellQt。
3. **Flatpak**（Flathub）：`io.github.way2trans`。所需权限：`--socket=wayland --socket=fallback-x11 --talk-name=org.freedesktop.portal.*`；X11 取词与 XGrabKey 在沙箱内受限，Flatpak 版主推 Wayland 路径。
4. deb/rpm 后置，等版本稳定。

许可证：**GPL-3.0**（应用本体开源；依赖链 Qt-LGPL / Tesseract-Apache2 / LayerShellQt-LGPL 均兼容）。若未来转闭源需在无外部贡献者签 CLA 前决定。

## 13. 里程碑

| 阶段 | 内容 | 验收标准 |
|---|---|---|
| **M0 骨架** | CMake 工程、单实例 + D-Bus + CLI、托盘、Settings、日志、CI | `way2trans trigger show-main` 能唤起空主窗口 |
| **M1 X11 划词 MVP** | X11 取词/快捷键/光标弹窗 + Google/Bing 引擎 + TextCleaner | X11 会话全流程可用，选中→出结果 < 1s |
| **M2 Wayland** | wl-paste 取词、portal 快捷键、LayerShellQt 弹窗、平台探测完整化 | KDE Wayland 与 Sway 全流程可用 |
| **M3 截图翻译** | ScreenshotBackend ×3、CaptureOverlay、Tesseract 接入、OCR helper 进程框架 + 微信 OCR 后端 | 三类环境截图翻译可用，有微信环境时默认走微信 OCR |
| **M4 引擎与数据** | DeepL、LLM 流式引擎、词典卡片、SQLite 存储层 + 历史记录 + 生词本（含 Anki/CSV/JSON 导出）、设置页完整 | 对标 Bob 的核心日常体验 |
| **M5 分发** | AUR + AppImage + Flatpak、文档、其他语言的 i18n（`zh_CN` 已随功能持续维护，见 §7） | 外部用户可安装使用 |
| M6+ | RapidOCR、原生 data-control、朗读 TTS、剪贴板监听模式、AnkiConnect 推送 | — |

## 14. 主要风险与对策

| 风险 | 影响 | 对策 |
|---|---|---|
| KDE 各版本 portal 行为差异（授权框、token 失效） | 快捷键/截图偶发失效 | 平台代码集中、日志分类细、设置页暴露探测结果便于报障；Null 后端永远可用 |
| wl-paste 依赖外部程序 | 未安装则 Wayland 取词不可用 | 启动探测 + 明确安装提示；二期原生实现移除依赖 |
| Wayland 无法光标旁弹窗 | 体验与 X11 有差异 | 锚定位置可配置 + 截图场景用已知选区坐标；文档预期管理 |
| Tesseract 中文识别率一般 | 截图翻译质量 | 预处理调优兜底，M6 上 RapidOCR |
| 免 key 的 Google/Bing web 接口不稳定 | 默认引擎失效 | 多引擎并存互为备份；接口封装薄、更新快；Google endpoint 可换镜像 |
| 微信 OCR：微信升级改动组件/协议；私有组件的合规边界 | 后端失效 / 分发风险 | 只探测本机安装绝不打包分发；适配面收敛在 helper 进程；Tesseract/RapidOCR 永远兜底 |
| XWayland 应用取词 | 混合环境选区不同步 | 合成器负责同步 X11/Wayland 选区（KDE/wlroots 均已实现），data-control 读到的是合并后的 seat selection，理论无问题；列入 M2 测试项 |
