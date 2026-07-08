#pragma once

#include <QLoggingCategory>

// Logging categories follow the AGENTS.md convention: way2trans.<module>.
// Never use a bare qDebug(); always log through one of these categories.
Q_DECLARE_LOGGING_CATEGORY(w2tApp)
Q_DECLARE_LOGGING_CATEGORY(w2tCore)
Q_DECLARE_LOGGING_CATEGORY(w2tPlatform)
Q_DECLARE_LOGGING_CATEGORY(w2tEngines)
Q_DECLARE_LOGGING_CATEGORY(w2tUi)
