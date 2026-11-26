#include "control/transport/routes.hpp"
#include "platform/identifiers.hpp"

#include <stdexcept>

namespace bacuri::vms::control
{
namespace
{
std::vector<std::string_view> split(std::string_view path)
{
    std::vector<std::string_view> parts;
    for (;;) {
        const auto slash = path.find('/');
        parts.push_back(path.substr(0, slash));
        if (slash == std::string_view::npos)
            return parts;
        path.remove_prefix(slash + 1);
    }
}
} // namespace

Routes& Routes::add(std::string_view pattern, Handler handler)
{
    Route route{{}, std::move(handler)};
    const auto parts = split(pattern);
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const auto part = parts[i];
        if (part == "*") {
            if (i + 1 != parts.size())
                throw std::invalid_argument("A rest segment must end the route pattern");
            route.segments.push_back({Kind::rest, {}});
        } else if (part == "{id}") {
            route.segments.push_back({Kind::id, {}});
        } else if (part == "{name}") {
            route.segments.push_back({Kind::name, {}});
        } else if (part.empty() || part.contains('{')) {
            throw std::invalid_argument("Invalid route pattern segment");
        } else {
            route.segments.push_back({Kind::literal, std::string{part}});
        }
    }
    routes_.push_back(std::move(route));
    return *this;
}

bool Routes::match(const Route& route, const std::vector<std::string_view>& parts, PathMatch& result)
{
    result = {};
    std::size_t i = 0;
    for (const auto& segment : route.segments) {
        if (segment.kind == Kind::rest) {
            for (std::size_t j = i; j < parts.size(); ++j) {
                if (j > i)
                    result.rest += '/';
                result.rest += parts[j];
            }
            return true;
        }
        if (i == parts.size())
            return false;
        const auto part = parts[i++];
        switch (segment.kind) {
        case Kind::literal:
            if (part != segment.text)
                return false;
            break;
        case Kind::id:
            if (!platform::canonical_id(part))
                return false;
            result.values.emplace_back(part);
            break;
        case Kind::name:
            if (part.empty())
                return false;
            result.values.emplace_back(part);
            break;
        case Kind::rest:
            break;
        }
    }
    return i == parts.size();
}

bool Routes::dispatch(std::string_view path) const
{
    const auto parts = split(path);
    PathMatch result;
    for (const auto& route : routes_) {
        if (match(route, parts, result)) {
            route.handler(result);
            return true;
        }
    }
    return false;
}
} // namespace bacuri::vms::control
