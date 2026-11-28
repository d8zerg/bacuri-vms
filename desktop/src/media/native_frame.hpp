#pragma once
#include <QImage>
#include <QObject>
#include <QPointer>
#include <gst/gst.h>
#include <memory>
#include <mutex>

namespace bacuri::vms::client
{
class NativeFrame final : public QObject
{
    Q_OBJECT
  public:
    ~NativeFrame() override;
    using Sample = std::shared_ptr<GstSample>;
    static NativeFrame* source(QObject* item);
    void attach(QObject* item);
    void receive(GstPad* pad, GstBuffer* buffer);
    void clear();
    Sample sample() const;
    struct Picture {
        QImage image;
        quint64 pts{};
    };
    static Picture decode(const Sample& sample);

  private:
    mutable std::mutex mutex_;
    Sample sample_;
    QPointer<QObject> target_;
};
} // namespace bacuri::vms::client
