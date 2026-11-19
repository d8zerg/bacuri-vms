#include "media/live.hpp"
#include "media/lease.hpp"
#include "media/webrtc.hpp"
#include "control/support.hpp"
#include <cstdio>
#include <future>

namespace bacuri::vms::media
{
namespace
{
using namespace std::chrono_literals;
using Result = std::pair<grpc::Status, std::string>;
void run(std::stop_token stop, const v1::LiveOffer& offer, std::promise<Result> result)
{
    bool answered = false;
    try {
        Lease lease{offer.scope()};
        lease.deadline();
        WebRtcPeer peer{offer.sdp()};
        const auto setup = Clock::now() + 5s;
        auto sdp = peer.answer(setup);
        lease.deadline();
        result.set_value({grpc::Status::OK, std::move(sdp)});
        answered = true;
        grpc::ClientContext source_context;
        source_context.set_deadline(std::chrono::system_clock::now() + std::chrono::minutes{5});
        std::atomic_bool ended{};
        std::atomic<Clock::time_point> authorized_until{};
        std::jthread source;
        struct Shutdown {
            WebRtcPeer& peer;
            grpc::ClientContext& rpc;
            Lease& lease;
            ~Shutdown()
            {
                peer.stop();
                rpc.TryCancel();
                lease.close_live();
            }
        } shutdown{peer, source_context, lease};

        bool connected = false;
        while (!stop.stop_requested() && !ended) {
            auto valid = lease.deadline();
            authorized_until = valid;
            if (!connected && Clock::now() >= setup) {
                break;
            }
            const auto state = peer.state();
            if (state == GST_WEBRTC_PEER_CONNECTION_STATE_CONNECTED && !connected) {
                connected = true;
                source = std::jthread{[&] {
                    try {
                        auto stub = v1::PreviewSource::NewStub(local_channel("BACURI_PREVIEW_SOCKET"));
                        auto reader = stub->Subscribe(&source_context, offer.scope().point());
                        v1::PreviewFrame frame;
                        while (reader->Read(&frame) && Clock::now() < authorized_until.load()) {
                            peer.push(frame.access_unit(), frame.keyframe());
                        }
                        source_context.TryCancel();
                        reader->Finish();
                    } catch (const std::exception&) {
                        std::fputs("Preview stream interrupted\n", stderr);
                    }
                    ended = true;
                }};
            }
            if (state == GST_WEBRTC_PEER_CONNECTION_STATE_FAILED ||
                state == GST_WEBRTC_PEER_CONNECTION_STATE_CLOSED ||
                state == GST_WEBRTC_PEER_CONNECTION_STATE_DISCONNECTED) {
                break;
            }
            const auto next_check = std::min(valid, Clock::now() + 40ms);
            std::this_thread::sleep_until(connected ? next_check : std::min(next_check, setup));
        }
    } catch (const std::invalid_argument&) {
        if (!answered) {
            result.set_value({{grpc::StatusCode::INVALID_ARGUMENT, "Unsupported SDP offer"}, {}});
        }
    } catch (const std::exception&) {
        if (!answered) {
            result.set_value({{grpc::StatusCode::UNAVAILABLE, "Live delivery unavailable"}, {}});
        }
    }
}
} // namespace
grpc::Status LiveDelivery::Negotiate(grpc::ServerContext* context, const v1::LiveOffer* offer,
                                     v1::LiveAnswer* answer)
{
    if (!offer->scope().has_live() || !control::canonical_id(offer->scope().media_session_id())) {
        return {grpc::StatusCode::INVALID_ARGUMENT, "Invalid live session"};
    }
    std::promise<Result> ready;
    auto future = ready.get_future();
    {
        std::scoped_lock guard{mutex_};
        std::erase_if(peers_, [](const auto& entry) { return entry.second->done.load(); });
        auto id = offer->scope().media_session_id();
        if (peers_.contains(id)) {
            return {grpc::StatusCode::ALREADY_EXISTS, "Session already negotiated"};
        }
        if (peers_.size() >= 16) {
            return {grpc::StatusCode::RESOURCE_EXHAUSTED, "Live capacity reached"};
        }
        auto peer = std::make_unique<Peer>();
        peer->worker = std::jthread{
            [entry = peer.get(), request = *offer, result = std::move(ready)](std::stop_token stop) mutable {
                run(stop, request, std::move(result));
                entry->done = true;
            }};
        peers_.emplace(std::move(id), std::move(peer));
    }
    if (future.wait_until(context->deadline()) != std::future_status::ready) {
        return {grpc::StatusCode::DEADLINE_EXCEEDED, "Live setup timed out"};
    }
    auto [status, sdp] = future.get();
    answer->set_sdp(std::move(sdp));
    return status;
}
} // namespace bacuri::vms::media
