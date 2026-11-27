#pragma once
#include "domain/acceleration.hpp"
#include <nlohmann/json_fwd.hpp>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bacuri::vms::acceleration
{
struct Task {
    const char* name;
    const char* operation;
    int rate;
    bool encoder;
};
inline constexpr std::array tasks{Task{"live_mask", "mask", 25, true},
                                  Task{"live_profile", "profile", 25, true},
                                  Task{"live_analysis", "analysis", 25, false},
                                  Task{"export", "export", 25, true},
                                  Task{"archive_analysis", "analysis", 25, false},
                                  Task{"keyframe", "keyframe", 1, false},
                                  Task{"thumbnail", "thumbnail", 1, false}};
struct Cost {
    unsigned time{};
    unsigned encoders{};
    std::uint64_t memory{};
    bool exclusive{};
    bool operator==(const Cost&) const = default;
};
struct Device {
    std::string id;
    unsigned sessions{};
    std::uint64_t memory{};
    Cost used;
    std::array<std::optional<Cost>, tasks.size()> costs{};
    bool session_limit_observed{};
    bool operator==(const Device&) const = default;
    bool fits(const Cost& reserved, const Cost& cost) const;
};
std::array<std::string, tasks.size()> preferred_devices(const domain::AccelerationSettings& settings,
                                                        const std::vector<Device>& devices);
void reserve(Cost& used, const Cost& cost);
std::vector<Device> budgets(const nlohmann::json& probe);
} // namespace bacuri::vms::acceleration
