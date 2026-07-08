#include "EngineResultCard.h"

#include <QApplication>
#include <QClipboard>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>

EngineResultCard::EngineResultCard(const QString &engineId, const QString &displayName, QWidget *parent)
    : QWidget(parent)
    , m_engineId(engineId)
{
    setObjectName(QStringLiteral("engineCard"));
    m_layout = new QVBoxLayout(this);
    // The card is just a layout group now; the rounded box is the body itself,
    // so keep the card flush and let the body sit close to the popup edges.
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(4);

    auto *headerRow = new QHBoxLayout;
    m_header = new QLabel(displayName, this);
    m_header->setObjectName(QStringLiteral("engineName"));
    headerRow->addWidget(m_header);
    headerRow->addStretch();

    m_copyButton = new QToolButton(this);
    m_copyButton->setObjectName(QStringLiteral("cardButton"));
    m_copyButton->setText(tr("复制"));
    m_copyButton->setToolTip(tr("复制翻译结果"));
    m_copyButton->setCursor(Qt::PointingHandCursor);
    m_copyButton->hide();
    connect(m_copyButton, &QToolButton::clicked, this,
        [this]() { QApplication::clipboard()->setText(m_body ? m_body->toPlainText() : QString()); });
    headerRow->addWidget(m_copyButton);

    m_retryButton = new QToolButton(this);
    m_retryButton->setObjectName(QStringLiteral("cardButton"));
    m_retryButton->setText(tr("重试"));
    m_retryButton->setCursor(Qt::PointingHandCursor);
    m_retryButton->hide();
    connect(m_retryButton, &QToolButton::clicked, this, [this]() { emit retryRequested(m_engineId); });
    headerRow->addWidget(m_retryButton);

    m_layout->addLayout(headerRow);

    // The rounded box is a plain frame (QFrame rounds cleanly). The QTextEdit
    // sits inside it fully transparent, so its rectangular viewport can't paint
    // over the frame's rounded corners.
    m_bodyFrame = new QFrame(this);
    m_bodyFrame->setObjectName(QStringLiteral("resultBox"));
    auto *bodyLayout = new QVBoxLayout(m_bodyFrame);
    bodyLayout->setContentsMargins(8, 5, 8, 5);
    bodyLayout->setSpacing(0);

    m_body = new QTextEdit(m_bodyFrame);
    m_body->setObjectName(QStringLiteral("resultBody"));
    m_body->setReadOnly(true);
    m_body->setFrameShape(QFrame::NoFrame);
    m_body->document()->setDocumentMargin(0);
    m_body->viewport()->setAutoFillBackground(false);
    m_body->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_body->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_body->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    bodyLayout->addWidget(m_body);
    m_layout->addWidget(m_bodyFrame);

    showPending();
}

void EngineResultCard::showPending()
{
    m_copyButton->hide();
    m_retryButton->hide();
    m_pending = true;
    m_maxBodyHeight = 260;
    if (m_body) {
        m_body->setPlainText(tr("Translating…"));
        m_body->setProperty("state", QStringLiteral("pending"));
        // Keep the skeleton compact; it grows to fit once the result arrives.
        m_body->setFixedHeight(20);
    }
}

void EngineResultCard::showResult(const TranslateResult &result, bool preferDict)
{
    m_pending = false;
    if (!result.ok()) {
        m_copyButton->hide();
        m_retryButton->show();
        setBody(result.error);
        m_body->setProperty("state", QStringLiteral("error"));
        return;
    }

    m_retryButton->hide();
    m_copyButton->show();

    if (preferDict && result.dict.has_value() && !result.dict->isEmpty()) {
        setDictBody(*result.dict, result.translatedText);
    } else {
        setBody(result.translatedText);
    }
    m_body->setProperty("state", QStringLiteral("ok"));
}

void EngineResultCard::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (!m_pending)
        updateBodyHeight();
}

void EngineResultCard::updateBodyHeight()
{
    if (!m_body)
        return;
    // viewport()->width() reflects the frame padding once the card is laid out,
    // so wrapping here matches what is actually rendered. The rounded frame adds
    // its own padding, so the body is sized to just the content (+ descenders).
    m_body->document()->setTextWidth(m_body->viewport()->width());
    const int h = int(m_body->document()->size().height()) + 4;
    m_body->setFixedHeight(qBound(20, h, m_maxBodyHeight));
}

void EngineResultCard::setBody(const QString &plainText)
{
    m_maxBodyHeight = 260;
    m_body->setPlainText(plainText);
    updateBodyHeight();
}

void EngineResultCard::setDictBody(const DictEntry &dict, const QString &fallbackText)
{
    QString html;
    if (!dict.phonetic.isEmpty())
        html += QStringLiteral("<div style='color:gray'>/%1/</div>").arg(dict.phonetic.toHtmlEscaped());
    if (!fallbackText.isEmpty())
        html += QStringLiteral("<div><b>%1</b></div>").arg(fallbackText.toHtmlEscaped());
    for (const DictMeaning &m : dict.meanings) {
        if (!m.partOfSpeech.isEmpty())
            html += QStringLiteral("<div style='color:#3a7'><i>%1</i></div>").arg(m.partOfSpeech.toHtmlEscaped());
        html += QStringLiteral("<ul style='margin-top:2px;margin-bottom:2px'>");
        for (const QString &def : m.definitions)
            html += QStringLiteral("<li>%1</li>").arg(def.toHtmlEscaped());
        html += QStringLiteral("</ul>");
    }
    m_maxBodyHeight = 320;
    m_body->setHtml(html);
    updateBodyHeight();
}
