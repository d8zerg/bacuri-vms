#pragma once

#include <gio/gio.h>
#include <memory>
#include <array>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bacuri::vms::control
{

using Key = std::array<unsigned char, 32>;

std::string setting(const char* name);
Key load_key(const std::filesystem::path& path);
std::string token();
std::string fingerprint(const Key& key, std::string_view purpose, std::string_view value);
std::string seal(const Key& key, std::string_view context, std::string_view plaintext);
std::string unseal(std::string_view ciphertext, const Key& key, std::string_view context);
std::string password_hash(std::string_view password);
bool password_matches(const std::string& hash, std::string_view password);
bool canonical_id(std::string_view value);
class CameraNetworks
{
public:
    explicit CameraNetworks(const std::string& specification);
    bool permits(const std::string& url) const;

private:
    std::vector<std::shared_ptr<GInetAddressMask>> masks_;
};

} // namespace bacuri::vms::control
