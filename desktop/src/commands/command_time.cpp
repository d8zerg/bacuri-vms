#include "commands/command_time.hpp"
#include <QRegularExpression>

namespace bacuri::vms::client
{
namespace
{
qint64 seconds(const QRegularExpressionMatch& match)
{
    const auto unit = match.captured(2);
    return match.captured(1).toLongLong() * (unit == "s" ? 1 : unit == "m" ? 60 : unit == "h" ? 3600 : 86400);
}
QDateTime parseTime(QString input, const QTimeZone& zone, const QDateTime& now, bool todayOnly)
{
    input = input.simplified();
    const auto iso = QDateTime::fromString(input, Qt::ISODateWithMs);
    if (iso.isValid() && iso.timeSpec() != Qt::LocalTime)
        return todayOnly ? QDateTime{} : iso.toUTC();
    input = input.toCaseFolded();
    static const QRegularExpression relative(R"(^-(\d{1,8})([smhd])$)");
    if (const auto match = relative.match(input); match.hasMatch())
        return todayOnly ? QDateTime{} : now.toUTC().addSecs(-seconds(match));
    static const QRegularExpression clock(
        R"(^(?:(yesterday|вчера) )?(\d{1,2})(?::(\d{2})(?::(\d{2}))?)?\s*(am|pm)?(?: (msk|мск|utc))?$)");
    const auto match = clock.match(input);
    if (!match.hasMatch() || (match.captured(3).isEmpty() && match.captured(5).isEmpty()))
        return {};
    const auto suffix = match.captured(6);
    const QTimeZone selected = suffix.isEmpty()  ? zone
                               : suffix == "utc" ? QTimeZone::utc()
                                                 : QTimeZone("Europe/Moscow");
    const auto local = now.toTimeZone(selected);
    int hour = match.captured(2).toInt();
    if (!match.captured(5).isEmpty()) {
        if (hour < 1 || hour > 12)
            return {};
        hour = hour % 12 + (match.captured(5) == "pm" ? 12 : 0);
    }
    const QTime time(hour, match.captured(3).toInt(), match.captured(4).toInt());
    if (!time.isValid())
        return {};
    const bool explicitYesterday = !match.captured(1).isEmpty();
    if (todayOnly && explicitYesterday)
        return {};
    const bool yesterday = explicitYesterday || (!todayOnly && time > local.time());
    // Ambiguous or skipped wall times need an explicit ISO offset.
    const QDateTime resolved(local.date().addDays(yesterday ? -1 : 0), time, selected,
                             QDateTime::TransitionResolution::Reject);
    return resolved.isValid() ? resolved.toUTC() : QDateTime{};
}
} // namespace
QDateTime commandTime(QString input, const QTimeZone& zone, const QDateTime& now)
{
    return parseTime(std::move(input), zone, now, false);
}
CommandInterval commandInterval(const QString& input, const QTimeZone& zone, const QDateTime& now)
{
    static const QRegularExpression duration(R"(\+(\d{1,8})([smhd])$)");
    const auto match = duration.match(input.trimmed());
    if (match.hasMatch()) {
        const auto startText = input.trimmed().left(match.capturedStart());
        const auto start = commandTime(startText, zone, now);
        auto end = start.addSecs(seconds(match));
        if (end > now && parseTime(startText, zone, now, true) == start)
            end = now.toUTC();
        return {start, end};
    }
    const auto parts = input.split('/');
    if (parts.size() != 2)
        return {};
    const auto startToday = parseTime(parts.first(), zone, now, true);
    const auto endToday = parseTime(parts.last(), zone, now, true);
    if (startToday.isValid() && endToday.isValid() && startToday <= now && endToday > now)
        return {startToday, now.toUTC()};
    const auto end = commandTime(parts.last(), zone, now);
    // For a pair of wall times crossing midnight, anchor the start to the end's date.
    const auto first = parts.first().simplified();
    const bool bareClock = !first.isEmpty() && first.front().isDigit() && !first.contains('-');
    const auto start = commandTime(first, zone, bareClock && end.isValid() ? end : now);
    return {start, end};
}
QString displayTime(const QDateTime& time, const QTimeZone& zone)
{
    const auto local = time.toTimeZone(zone);
    return local.toString("yyyy-MM-dd HH:mm:ss ") + local.timeZoneAbbreviation() + " (" +
           time.toUTC().toString("HH:mm:ss 'UTC'") + ")";
}
} // namespace bacuri::vms::client
