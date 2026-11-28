#include "live/live_commands.hpp"
#include <QDir>
#include <QJsonArray>

#include "media/native_frame.hpp"
#include <QtConcurrentRun>
#include <QFutureWatcher>
#include <QCryptographicHash>
#include <QSaveFile>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QQuickItem>
#include <QUuid>
#include <memory>

namespace bacuri::vms::client
{
namespace
{
QString message(const char* source)
{
    return QCoreApplication::translate("Shell", source);
}
QString outputPath(const QString& extension)
{
    const auto dir = qEnvironmentVariable("BACURI_CAPTURE_DIRECTORY",
                                          QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
    if (dir.isEmpty() || !QDir().mkpath(dir))
        return {};
    return QDir(dir).filePath("Bacuri-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + extension);
}
} // namespace
void LiveCommands::snapshot(const QString& id)
{
    const auto point = context_.site().point(id);
    const auto frame = QPointer(context_.frameFor(point));
    const auto* source = NativeFrame::source(frame);
    auto sample = source ? source->sample() : nullptr;
    if (!sample) {
        context_.feedback(message("No frame is available."));
        return;
    }
    const auto generation = context_.site().accessGeneration();
    auto* watcher = new QFutureWatcher<NativeFrame::Picture>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, frame, point, id, generation] {
        auto picture = watcher->result();
        watcher->deleteLater();
        const auto current = context_.site().point(id);
        if (context_.site().accessGeneration() != generation || !frame || frame != context_.frameFor(current) ||
            current.value("access_revision") != point.value("access_revision") || !canCapture(current))
            return;
        auto& image = picture.image;
        if (image.isNull()) {
            context_.feedback(message("No frame is available."));
            return;
        }
        const auto bytes = QByteArrayView(reinterpret_cast<const char*>(image.constBits()), image.sizeInBytes());
        image.setText("sha256_rgba8",
                      QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()));
        image.setText("point_id", id);
        image.setText("site_id", point.value("site_id").toString());
        image.setText("access_revision", point.value("access_revision").toString());
        image.setText("pts_ns", QString::number(picture.pts));
        auto* receipt = context_.site().pointRequest(id, "snapshot-receipts", {},
                                                     {{"access_revision", image.text("access_revision")},
                                                      {"pts_ns", image.text("pts_ns")},
                                                      {"width", image.width()},
                                                      {"height", image.height()},
                                                      {"sha256_rgba8", image.text("sha256_rgba8")}});
        if (!receipt) {
            context_.feedback(message("This action is unavailable."));
            return;
        }
        connect(receipt, &QNetworkReply::finished, receipt, &QObject::deleteLater);
        connect(receipt, &QNetworkReply::finished, this,
                [this, receipt, frame, point, id, generation, image = std::move(image)]() mutable {
                    if (receipt->error() != QNetworkReply::NoError) {
                        context_.feedback(context_.site().actionFailure(receipt));
                        return;
                    }
                    const auto audit =
                        QJsonDocument::fromJson(receipt->readAll()).object().value("receipt").toString();
                    if (QUuid(audit).isNull()) {
                        context_.feedback(message("The site could not complete the action."));
                        return;
                    }
                    if (context_.site().accessGeneration() != generation ||
                        frame != context_.frameFor(context_.site().point(id)) ||
                        context_.site().point(id).value("access_revision") != point.value("access_revision") ||
                        !canCapture(context_.site().point(id)))
                        return;
                    image.setText("audit_receipt", audit);
                    const auto path = outputPath("-" + image.text("pts_ns") + ".png");
                    QSaveFile out(path);
                    if (path.isEmpty() || !out.open(QIODevice::WriteOnly) || !image.save(&out, "PNG") ||
                        !out.commit()) {
                        context_.feedback(message("Could not save the file."));
                        return;
                    }
                    context_.feedback(message("Saved to %1").arg(path));
                });
    });
    watcher->setFuture(QtConcurrent::run([sample = std::move(sample)] { return NativeFrame::decode(sample); }));
}
} // namespace bacuri::vms::client
