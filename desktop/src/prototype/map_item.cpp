#include "prototype/map_item.hpp"

#include <QGuiApplication>
#include <QMapLibre/Map>
#include <QMapLibre/Settings>
#include <QOpenGLFramebufferObject>
#include <QQuickWindow>
#include <QScreen>
#include <QStandardPaths>

#include <chrono>

namespace bacuri::desktop
{
namespace
{
class MapRenderer : public QQuickFramebufferObject::Renderer
{
  public:
    explicit MapRenderer(MapItem* item) : item_{item}
    {
        item_->map()->createRenderer();
    }
    MapRenderer(const MapRenderer&) = delete;
    MapRenderer& operator=(const MapRenderer&) = delete;
    ~MapRenderer() override
    {
        item_->map()->destroyRenderer();
    }

    QOpenGLFramebufferObject* createFramebufferObject(const QSize& size) override
    {
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        return new QOpenGLFramebufferObject(size, format);
    }

    void synchronize(QQuickFramebufferObject* item) override
    {
        const auto ratio = item->window() != nullptr ? item->window()->effectiveDevicePixelRatio() : 1.0;
        size_ = (item->size() * ratio).toSize();
        item_->map()->resize(item->size().toSize());
    }

    void render() override
    {
        const auto started = std::chrono::steady_clock::now();
        item_->map()->setOpenGLFramebufferObject(framebufferObject()->handle(), size_);
        item_->map()->render();
        item_->record_render(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
    }

  private:
    MapItem* item_;
    QSize size_;
};
} // namespace

MapItem::MapItem(QQuickItem* parent) : QQuickFramebufferObject{parent}
{
    QMapLibre::Settings settings;
    settings.setCacheDatabasePath(QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
                                  QStringLiteral("/bacuri-map-cache.db"));
    const auto ratio =
        QGuiApplication::primaryScreen() != nullptr ? QGuiApplication::primaryScreen()->devicePixelRatio() : 1.0;
    map_ = std::make_unique<QMapLibre::Map>(nullptr, settings, QSize{256, 256}, ratio);
    connect(map_.get(), &QMapLibre::Map::needsRendering, this, &QQuickItem::update);
    setMirrorVertically(true);
}

MapItem::~MapItem() = default;

QQuickFramebufferObject::Renderer* MapItem::createRenderer() const
{
    return new MapRenderer(const_cast<MapItem*>(this));
}

void MapItem::setStyleUrl(const QString& url)
{
    if (url == style_url_)
        return;
    style_url_ = url;
    map_->setStyleUrl(url);
    apply_camera();
    emit styleUrlChanged();
}

void MapItem::setLatitude(double value)
{
    latitude_ = value;
    apply_camera();
}

void MapItem::setLongitude(double value)
{
    longitude_ = value;
    apply_camera();
}

void MapItem::setZoom(double value)
{
    zoom_ = value;
    apply_camera();
}

void MapItem::setBearing(double value)
{
    bearing_ = value;
    map_->setBearing(value);
    update();
}

void MapItem::apply_camera()
{
    map_->setCoordinateZoom(QMapLibre::Coordinate{latitude_, longitude_}, zoom_);
    update();
}

void MapItem::record_render(double milliseconds)
{
    std::scoped_lock lock{mutex_};
    renders_.push_back(milliseconds);
}

QVariantMap MapItem::statistics() const
{
    std::scoped_lock lock{mutex_};
    QVariantList values;
    for (const auto value : renders_)
        values.append(value);
    return {{"renders", static_cast<qlonglong>(renders_.size())}, {"render_ms", values}};
}
} // namespace bacuri::desktop
