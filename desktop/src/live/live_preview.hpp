#pragma once
#include "site/site_access.hpp"
#include "media/stream_group.hpp"
#include <QQuickItem>
#include <QQmlEngine>
#include <QTimer>
#include <QElapsedTimer>
namespace bacuri::vms::client
{
class LivePreview : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QString point READ point NOTIFY changed FINAL)
    Q_PROPERTY(bool ready READ ready NOTIFY changed FINAL)
    Q_PROPERTY(QQuickItem* surface MEMBER surface_ REQUIRED FINAL)
    Q_PROPERTY(bool preferMain MEMBER preferMain_ FINAL)
  public:
    using QQuickItem::QQuickItem;
    ~LivePreview() override;
    QString point() const
    {
        return point_;
    }
    bool ready() const
    {
        return ready_;
    }
    Q_INVOKABLE void open(QObject* site, const QString& id);
    Q_INVOKABLE void close();
    QJsonObject report() const;
  signals:
    void changed();

  private:
    void negotiate(const QString& codec);
    void tick();
    void start(SiteAccess* site, const QString& id, DecoderChoice decoder);
    QPointer<SiteAccess> workspace_;
    QQuickItem* surface_{};
    bool preferMain_{true};
    QMetaObject::Connection rights_, state_;
    QString point_, session_;
    QVariant revision_;
    quint64 generation_{};
    std::unique_ptr<StreamGroup> playback_;
    VideoStream* stream_{};
    GstElement* peer_{};
    QTimer poll_, expiry_;
    QElapsedTimer startup_;
    bool sent_{}, ready_{};
    DecoderChoice decoder_{DecoderChoice::automatic};
};
} // namespace bacuri::vms::client
