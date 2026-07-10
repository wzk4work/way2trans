#pragma once

class QWidget;

namespace w2t::ui {

// Builders for the simple settings pages (DESIGN.md §7). Each returns a fresh
// widget whose controls read from Settings and write back on change, so edits
// take effect immediately (no restart, DESIGN.md §10). The pages sit directly
// in the main window's sidebar navigation; the services page is its own class
// (ServicesPage) since it manages the instance list.
QWidget *buildGeneralPage(QWidget *parent);
QWidget *buildHotkeysPage(QWidget *parent);
QWidget *buildOcrPage(QWidget *parent);
QWidget *buildPlatformPage(QWidget *parent);

} // namespace w2t::ui
