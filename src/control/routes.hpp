#pragma once
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace bacuri::vms::control
{
// Variable segments of a matched path in the order of the pattern.
struct PathMatch {
    std::vector<std::string> values;
    // The path after the segments matched by a final "*", without a leading slash.
    std::string rest;
};

// A table of path patterns checked in the order they were added.
//
// A pattern is a list of segments separated by "/". A segment is a literal, "{id}" for a canonical UUID or
// "{name}" for any non-empty segment. A final "*" matches the rest of the path, which can be empty.
class Routes
{
  public:
    using Handler = std::function<void(const PathMatch&)>;

    Routes& add(std::string_view pattern, Handler handler);

    // Runs the handler of the first matching pattern. Returns false when no pattern matches.
    bool dispatch(std::string_view path) const;

  private:
    enum class Kind { literal, id, name, rest };
    struct Segment {
        Kind kind;
        std::string text;
    };
    struct Route {
        std::vector<Segment> segments;
        Handler handler;
    };
    static bool match(const Route& route, const std::vector<std::string_view>& parts, PathMatch& result);
    std::vector<Route> routes_;
};
} // namespace bacuri::vms::control
