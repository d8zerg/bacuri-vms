#pragma once
#include <QDateTime>
#include <QTimeZone>

namespace bacuri::vms::client
{
struct CommandInterval {
    QDateTime start;
    QDateTime end;
};
QDateTime commandTime(QString input, const QTimeZone& zone, const QDateTime& now);
CommandInterval commandInterval(const QString& input, const QTimeZone& zone, const QDateTime& now);
QString displayTime(const QDateTime& time, const QTimeZone& zone);
} // namespace bacuri::vms::client
