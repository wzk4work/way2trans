#pragma once

#include "OcrBackend.h"

#include <QString>

class QProcess;

// WeChat OCR backend (DESIGN.md §6.1).
//
// WeChat ships a high-quality, fully-local Chinese OCR engine. We do NOT bundle
// or link any of its (proprietary Tencent) binaries — two hard rules from
// AGENTS.md: never package them, and never load them in the main process. This
// backend only *drives* the engine, and it does so exclusively through a
// separate helper executable `way2trans-ocr-helper`, over a line-delimited
// stdio JSON protocol (send an image path, receive text + line boxes). If the
// helper crashes, only the single recognise fails and OcrService falls back to
// the next backend; the main process is never at risk.
//
// Availability: probeInstallPath() looks for a local WeChat install (and honours
// a Settings-provided path). When nothing is found the backend is simply not
// offered — no packaged component, no runtime download.
class WeChatOcr : public OcrBackend {
    Q_OBJECT

public:
    explicit WeChatOcr(QObject *parent = nullptr);
    ~WeChatOcr() override;

    QString id() const override;
    QString displayName() const override;
    void recognize(const QImage &image, const QStringList &langHints) override;

    // Locate a usable WeChat OCR component directory (DESIGN.md §6.1). Order:
    //   1. Settings [ocr] wechatPath (user-specified),
    //   2. common native install roots (/opt/wechat, ~/.local/... , flatpak).
    // A directory qualifies when it contains the OCR runtime bits (the `wxocr`
    // executable, the mojo IPC lib libmmmojo.so, and an ocr_model dir). Returns
    // an empty string when WeChat OCR is not present. Pure/probe-only.
    static QString probeInstallPath();

    // Whether a WeChat OCR install is present (probeInstallPath() non-empty).
    static bool isAvailable();

    // Resolve the helper executable path. Looks next to the running binary
    // first (install layout), then on PATH. Empty when not found.
    static QString helperExecutablePath();

private:
    void startHelperIfNeeded();
    void failAndReset(const QString &reason);

    QString m_installPath;
    QProcess *m_helper = nullptr;
    QByteArray m_stdoutBuffer;
    QString m_pendingImagePath; // temp PNG for the in-flight request, to unlink
    bool m_busy = false;
};
