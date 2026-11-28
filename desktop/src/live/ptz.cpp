#include "live/live_commands.hpp"
#include <QJsonDocument>
#include <QCoreApplication>
#include <QJsonArray>

namespace bacuri::vms::client
{
namespace
{
QString message(const char* source)
{
    return QCoreApplication::translate("Shell", source);
}
} // namespace
void LiveCommands::readCameraControls()
{
    const auto id = context_.focusedCamera();
    const auto point = context_.site().point(id);
    const auto device = point.value("ptz").toMap().value("device_id").toString();
    const auto key = !id.isEmpty() && !device.isEmpty()
                         ? id + "/" + device + "/" + point.value("access_revision").toString()
                         : QString{};
    if (key == controlsKey_)
        return;
    controlsKey_ = key;
    if (auto* previous = controlsRead_.data()) {
        controlsRead_.clear();
        previous->abort();
    }
    cameraActions_.clear();
    presets_.clear();
    if (key.isEmpty())
        return;
    auto* reply = context_.site().pointRequest(id, "ptz", {});
    if (!reply)
        return;
    controlsRead_ = reply;
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    connect(reply, &QNetworkReply::finished, this, [this, reply, device] {
        if (controlsRead_ != reply)
            return;
        controlsRead_.clear();
        if (reply->error() != QNetworkReply::NoError)
            return;
        const auto catalog = QJsonDocument::fromJson(reply->readAll()).object();
        if (catalog.value("device_id").toString() != device)
            return;
        const auto actions = catalog.value("actions").toArray();
        for (const auto entry : actions) {
            const auto action = entry.toObject().toVariantMap();
            const auto command = action.value("command").toString();
            if (command.isEmpty() ||
                QStringList{"left", "right", "up", "down", "zoom_in", "zoom_out", "stop"}.contains(command))
                continue;
            cameraActions_.append(action);
            if (command == "preset_goto" && !action.value("target").toString().isEmpty() && presets_.size() < 8)
                presets_.append(action);
        }
        emit changed();
    });
}
QVariantList LiveCommands::ptzOptions() const
{
    QVariantList choices;
    for (const auto& value : cameraActions_) {
        auto option = value.toMap();
        const auto name = ptzName(option.value("command").toString());
        option["text"] =
            option.value("target").toString().isEmpty() ? name : name + " · " + option.value("label").toString();
        option["value"] = choices.size();
        choices.append(option);
    }
    return choices;
}
void LiveCommands::confirmPreset(int number)
{
    if (context_.focusedCamera().isEmpty() || number < 1 || number > presets_.size())
        return;
    auto point = context_.site().point(context_.focusedCamera());
    point["preset"] = presets_.at(number - 1).toMap().value("target");
    context_.tool("ptz", point);
}
void LiveCommands::startMotion(const QString& id, const QString& command)
{
    static const QStringList directions{"left", "right", "up", "down", "zoom_in", "zoom_out"};
    if (!motionPoint_.isEmpty() || !directions.contains(command) || id.isEmpty() || context_.focusedCamera() != id)
        return;
    const auto ptz = context_.site().point(id).value("ptz").toMap();
    const auto commands = ptz.value("commands").toList();
    if (!commands.contains(command) || !commands.contains("stop"))
        return;
    motionDevice_ = ptz.value("device_id").toString();
    motionPoint_ = id;
    motionDirection_ = command;
    ++motionGeneration_;
    sendMotion();
}
void LiveCommands::sendMotion()
{
    if (motionPoint_.isEmpty())
        return;
    const auto id = motionPoint_, device = motionDevice_;
    auto* reply =
        context_.site().pointRequest(id, "ptz", {}, {{"device_id", device}, {"command", motionDirection_}});
    if (!reply) {
        stopMotion();
        return;
    }
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    connect(reply, &QNetworkReply::finished, &context_.site(),
            [owner = QPointer<LiveCommands>(this), workspace = &context_.site(), reply, id, device,
             generation = motionGeneration_] {
                if (owner && owner->motionGeneration_ == generation && owner->motionPoint_ == id) {
                    if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 204) {
                        // The camera's continuous move expires after one second.
                        owner->motionTimer_.start(450);
                    } else {
                        owner->stopMotion();
                        owner->context_.feedback(workspace->actionFailure(reply));
                    }
                    return;
                }
                // Release may precede acknowledgement of a move that was already in flight.
                if (owner && owner->motionPoint_ == id && owner->motionDevice_ == device)
                    return;
                if (auto* stop =
                        workspace->pointRequest(id, "ptz", {}, {{"device_id", device}, {"command", "stop"}}))
                    QObject::connect(stop, &QNetworkReply::finished, stop, &QObject::deleteLater);
            });
}
void LiveCommands::stopMotion()
{
    motionTimer_.stop();
    ++motionGeneration_;
    if (motionPoint_.isEmpty())
        return;
    const auto id = std::exchange(motionPoint_, {}), device = std::exchange(motionDevice_, {});
    emit motionStopped();
    if (auto* stop = context_.site().pointRequest(id, "ptz", {}, {{"device_id", device}, {"command", "stop"}}))
        connect(stop, &QNetworkReply::finished, stop, &QObject::deleteLater);
}
void LiveCommands::sendPtz(const CommandInput& input)
{
    const auto& [id, start, end, argument] = input;
    const auto point = context_.site().point(id);
    const auto ptz = point.value("ptz").toMap();
    if (!std::ranges::any_of(cameraActions_, [&](const QVariant& value) {
            const auto action = value.toMap();
            return action.value("command") == start && action.value("target") == argument;
        })) {
        context_.feedback(message("This action is unavailable."));
        return;
    }
    QJsonObject body{{"device_id", ptz.value("device_id").toString()}, {"command", start}};
    if (!argument.isEmpty())
        body["target"] = argument;
    auto* reply = context_.site().pointRequest(id, "ptz", {}, body);
    if (!reply) {
        context_.feedback(message("This action is unavailable."));
        return;
    }
    context_.submitted();
    jobs_.insert(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        jobs_.remove(reply);
        reply->deleteLater();
        if (reply->error() == QNetworkReply::OperationCanceledError)
            return;
        context_.feedback(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute) == 204
                              ? message("Camera command acknowledged.")
                              : context_.site().actionFailure(reply));
    });
}
} // namespace bacuri::vms::client
