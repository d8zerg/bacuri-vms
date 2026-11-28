#include "live/live_commands.hpp"

#include "media/native_frame.hpp"
#include <QCoreApplication>
#include <QQuickItem>

namespace bacuri::vms::client
{
using W = WindowCommands;
using K = QKeySequence;
using S = CommandScope;
using P = ParameterType;

LiveCommands::LiveCommands(WindowContext& context) : context_(context)
{
    motionTimer_.setSingleShot(true);
    connect(&motionTimer_, &QTimer::timeout, this, &LiveCommands::sendMotion);
}

LiveCommands::~LiveCommands()
{
    stopMotion();
    cancelCommands();
    if (controlsRead_)
        controlsRead_->abort();
}

void LiveCommands::cancelCommands()
{
    const auto jobs = jobs_;
    for (auto* reply : jobs)
        reply->abort();
}

bool LiveCommands::canMove(const QString& direction) const
{
    const auto control = context_.camera().value("ptz").toMap();
    const auto actions = control.value("commands").toList();
    return !control.value("device_id").toString().isEmpty() && actions.contains(direction) &&
           actions.contains("stop");
}

bool LiveCommands::canCapture(const QVariantMap& point) const
{
    auto* frame = context_.frameFor(point);
    return frame && frame->isVisible() && NativeFrame::source(frame) && point.value("can_snapshot").toBool();
}

void LiveCommands::install(CommandRegistry& registry, ContextAccess context, FeatureAccess<LiveCommands> live)
{
    struct Motion {
        const char* id;
        const char* name;
        const char* key;
    };
    for (const auto motion : {Motion{"left", "Pan left", "Left"}, Motion{"right", "Pan right", "Right"},
                              Motion{"up", "Tilt up", "Up"}, Motion{"down", "Tilt down", "Down"},
                              Motion{"zoom_in", "Zoom in", "+"}, Motion{"zoom_out", "Zoom out", "-"}}) {
        registry.add({"ptz-" + QString(motion.id),
                      motion.name,
                      "move",
                      {K(motion.key)},
                      S::Video,
                      P::None,
                      [live, motion](const W& w) { return live(w).canMove(motion.id); },
                      [live, motion](W& w) {
                          auto& feature = live(w);
                          feature.startMotion(feature.context_.focusedCamera(), motion.id);
                      },
                      true});
    }
    addCameraCommand(
        registry, context, "snapshot", "Save frame", "scan-line", {}, P::None,
        [live](W& w) {
            auto& feature = live(w);
            feature.snapshot(feature.context_.focusedCamera());
        },
        {}, [live](const W& w, const QVariantMap& point) { return live(w).canCapture(point); });
    addCameraCommand(
        registry, context, "ptz", "PTZ controls", "move", {}, P::Preset, {},
        [live](W& w, const CommandInput& input) { live(w).sendPtz(input); },
        [](const W&, const QVariantMap& point) {
            return !point.value("ptz").toMap().value("device_id").toString().isEmpty();
        });
}

QString LiveCommands::ptzName(const QString& command)
{
    static const QHash<QString, const char*> names{{"left", "Pan left"},
                                                   {"right", "Pan right"},
                                                   {"up", "Tilt up"},
                                                   {"down", "Tilt down"},
                                                   {"stop", "Stop"},
                                                   {"zoom_in", "Zoom in"},
                                                   {"zoom_out", "Zoom out"},
                                                   {"focus_near", "Focus near"},
                                                   {"focus_far", "Focus far"},
                                                   {"focus_stop", "Stop focus"},
                                                   {"preset_goto", "Go to preset"},
                                                   {"tour_start", "Start tour"},
                                                   {"tour_stop", "Stop tour"},
                                                   {"light_on", "Light on"},
                                                   {"light_off", "Light off"},
                                                   {"wiper_on", "Wiper on"},
                                                   {"wiper_off", "Wiper off"},
                                                   {"relay_on", "Relay on"},
                                                   {"relay_off", "Relay off"}};
    return names.contains(command) ? QCoreApplication::translate("Shell", names.value(command)) : command;
}
} // namespace bacuri::vms::client
