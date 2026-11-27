#include "map/map_view.hpp"

#include <QGuiApplication>
#include <QMapLibre/Map>
#include <QMapLibre/Settings>
#include <QOpenGLFramebufferObject>
#include <QQuickWindow>
#include <QScreen>
#include <QStandardPaths>

#include <chrono>

namespace bacuri::vms::client
{
namespace
{
std::function<std::string(const std::string&)>& resource_transform()
{
    static std::function<std::string(const std::string&)> transform;
    return transform;
}

class MapRenderer : public QQuickFramebufferObject::Renderer
{
  public:
    explicit MapRenderer(MapView* item) : item_{item}
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
    MapView* item_;
    QSize size_;
};
} // namespace

MapView::MapView(QQuickItem* parent) : QQuickFramebufferObject{parent}
{
    QMapLibre::Settings settings;
    if (resource_transform())
        settings.setResourceTransform(resource_transform());
    settings.setCacheDatabasePath(QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
                                  QStringLiteral("/bacuri-map-cache.db"));
    const auto ratio =
        QGuiApplication::primaryScreen() != nullptr ? QGuiApplication::primaryScreen()->devicePixelRatio() : 1.0;
    map_ = std::make_unique<QMapLibre::Map>(nullptr, settings, QSize{256, 256}, ratio);
    connect(map_.get(), &QMapLibre::Map::needsRendering, this, &QQuickItem::update);
    setMirrorVertically(true);
}

MapView::~MapView() = default;

void MapView::set_resource_transform(std::function<std::string(const std::string&)> transform)
{
    resource_transform() = std::move(transform);
}

QQuickFramebufferObject::Renderer* MapView::createRenderer() const
{
    return new MapRenderer(const_cast<MapView*>(this));
}

void MapView::setStyleUrl(const QString& url)
{
    if (url == style_url_)
        return;
    style_url_ = url;
    map_->setStyleUrl(url);
    apply_camera();
    emit styleUrlChanged();
}

void MapView::setLatitude(double value)
{
    latitude_ = value;
    apply_camera();
}

void MapView::setLongitude(double value)
{
    longitude_ = value;
    apply_camera();
}

void MapView::setZoom(double value)
{
    zoom_ = value;
    apply_camera();
}

void MapView::setBearing(double value)
{
    bearing_ = value;
    map_->setBearing(value);
    update();
}

void MapView::apply_camera()
{
    map_->setCoordinateZoom(QMapLibre::Coordinate{latitude_, longitude_}, zoom_);
    update();
}

void MapView::record_render(double milliseconds)
{
    std::scoped_lock lock{mutex_};
    renders_.push_back(milliseconds);
}

QVariantMap MapView::statistics() const
{
    std::scoped_lock lock{mutex_};
    QVariantList values;
    for (const auto value : renders_)
        values.append(value);
    return {{"renders", static_cast<qlonglong>(renders_.size())}, {"render_ms", values}};
}
} // namespace bacuri::vms::client
