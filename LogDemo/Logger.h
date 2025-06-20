// Logger.h
#pragma once
#include <QDebug>

namespace Logger
{
#define LogDebug qDebug() << "| "
#define LogInfo qInfo() << "| "
#define LogWarn qWarning() << "| "
#define LogErr qCritical() << "| "

void initLog(const QString &logPath = QStringLiteral("Log"), int logMaxCount = 1024, bool async = true);
} // namespace Logger
