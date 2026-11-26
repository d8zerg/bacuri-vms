#pragma once

#include <QQuickFramebufferObject>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <mutex>
#include <vector>

namespace QMapLibre
{
class Map;
}

namespace bacuri::desktop
{
// A MapLibre map drawn into an OpenGL framebuffer of the Qt Quick scene graph. The map object
// lives on the GUI thread, its renderer on the render thread of the window.
class MapItem : public QQuickFramebufferObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString styleUrl READ styleUrl WRITE setStyleUrl NOTIFY styleUrlChanged)
    Q_PROPERTY(double latitude MEMBER latitude_ WRITE setLatitude)
    Q_PROPERTY(double longitude MEMBER longitude_ WRITE setLongitude)
    Q_PROPERTY(double zoom MEMBER zoom_ WRITE setZoom)
    Q_PROPERTY(double bearing MEMBER bearing_ WRITE setBearing)

  public:
    explicit MapItem(QQuickItem* parent = nullptr);
    ~MapItem() override;

    Renderer* createRenderer() const override;

    QString styleUrl() const
    {
        return style_url_;
    }
    void setStyleUrl(const QString& url);
    void setLatitude(double value);
    void setLongitude(double value);
    void setZoom(double value);
    void setBearing(double value);

    // Render count and render time of the map in milliseconds.
    Q_INVOKABLE QVariantMap statistics() const;

    QMapLibre::Map* map() const
    {
        return map_.get();
    }
    void record_render(double milliseconds);

  signals:
    void styleUrlChanged();

  private:
    void apply_camera();

    std::unique_ptr<QMapLibre::Map> map_;
    QString style_url_;
    double latitude_{};
    double longitude_{};
    double zoom_{14};
    double bearing_{};
    mutable std::mutex mutex_;
    std::vector<double> renders_;
};
} // namespace bacuri::desktop
