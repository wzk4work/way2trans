#pragma once

#include "ScreenshotBackend.h"

#include <QString>

class QProcess;

// wlroots screenshot fallback via the `grim` CLI (DESIGN.md §4.3).
//
// Used on wlroots compositors (Sway / Hyprland / River) when the screenshot
// portal is not available. grim writes a PNG of the whole output layout to a
// temporary file; we load it and delete the file. Like the other backends this
// is fully asynchronous (QProcess), so a hung grim can't block the UI thread.
class GrimScreenshotBackend : public ScreenshotBackend {
    Q_OBJECT

public:
    explicit GrimScreenshotBackend(QObject *parent = nullptr);

    void captureFullScreen() override;
    QString name() const override;

    // Whether the `grim` executable is resolvable on PATH. PlatformFactory
    // probes this before choosing this backend.
    static bool isGrimAvailable();

private:
    static constexpr int kTimeoutMs = 4000;

    QProcess *m_process = nullptr;
    QString m_outputPath;
};
