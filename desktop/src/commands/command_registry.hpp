#pragma once
#include <QObject>
#include <QKeySequence>
#include <QHash>
#include <QVariantMap>
#include <functional>

namespace bacuri::vms::client
{
class WindowCommands;
struct CommandInput {
    QString point, start, end, argument;
};
using CommandHandler = std::function<void(WindowCommands&, const CommandInput&)>;
enum class CommandScope { Global, Video, Timeline, Text };
enum class ParameterType { None, Time, Interval, Screen, Preset };
struct Command {
    QString id;
    const char* name;
    QString icon;
    QList<QKeySequence> defaults;
    CommandScope scope;
    ParameterType parameter;
    std::function<bool(const WindowCommands&)> available;
    std::function<void(WindowCommands&)> run;
    bool hold{};
    CommandHandler submit{};
};
class CommandRegistry final : public QObject
{
    Q_OBJECT
  public:
    using QObject::QObject;
    void add(Command command)
    {
        commands_.append(std::move(command));
    }
    void add(std::initializer_list<Command> commands)
    {
        for (const auto& command : commands)
            add(command);
    }
    const QList<Command>& commands() const
    {
        return commands_;
    }
    const Command* find(const QString& id) const;
    QVariantMap bindings(const QString& profile) const;
    void bind(const QString& profile, const QString& id, const QString& value);
    struct Usage {
        qint64 count{};
        qint64 last{};
    };
    Usage usage(const QString& profile, const QString& key) const;
    void remember(const QString& profile, const QString& key);
  signals:
    void bindingsChanged(QString profile);

  private:
    void load(const QString& profile) const;
    QList<Command> commands_;
    mutable QHash<QString, QHash<QString, Usage>> usage_;
    QVariantMap guestBindings_;
};
} // namespace bacuri::vms::client
