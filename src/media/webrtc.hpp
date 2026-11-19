#pragma once
#include <gst/app/gstappsrc.h>
#include <gst/webrtc/webrtc.h>
#include <chrono>
#include <memory>
#include <string>
#include <string_view>

namespace bacuri::vms::media
{
template <class T, auto Release> struct GstDeleter {
    void operator()(T* object) const
    {
        Release(object);
    }
};
template <class T, auto Release> using GstHandle = std::unique_ptr<T, GstDeleter<T, Release>>;

class WebRtcPeer
{
  public:
    explicit WebRtcPeer(const std::string& offer);
    ~WebRtcPeer();
    void stop() noexcept;
    std::string answer(std::chrono::steady_clock::time_point deadline);
    GstWebRTCPeerConnectionState state() const;
    void push(std::string_view access_unit, bool keyframe);

  private:
    GstHandle<GstWebRTCSessionDescription, gst_webrtc_session_description_free> offer_;
    GstHandle<GstElement, gst_object_unref> pipeline_, rtc_, input_;
    bool need_keyframe_{true};
};
} // namespace bacuri::vms::media
