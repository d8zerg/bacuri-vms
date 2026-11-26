#pragma once

#include "application/control_api/http.hpp"
#include <functional>

namespace bacuri::vms::control
{
using HttpHandler = std::function<Response(const Request&, const std::string& peer)>;

// Handlers block on the database, cameras and password hashing. Each workload runs on its own thread
// pool, so slow work cannot delay the requests of an operator on duty.
enum class Workload {
    // Requests of the operator workplace and short administrative requests.
    interactive,
    // Password verification and hashing. Each one holds tens of megabytes for a fraction of a second.
    credential,
    // Requests that wait on cameras, remote map sources or large archive reads.
    long_running,
};
using WorkloadClassifier = std::function<Workload(const Request&)>;

struct HttpsSettings {
    std::string address;
    unsigned short port;
    std::string certificate_chain;
    std::string private_key;
};

void listen_https(const HttpsSettings& settings, const HttpHandler& handler, const WorkloadClassifier& workload);
} // namespace bacuri::vms::control
