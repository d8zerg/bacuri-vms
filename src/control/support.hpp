#pragma once

// The control module uses the shared platform services under its own namespace. Other modules include
// the platform headers directly and do not depend on control.
#include "platform/camera_networks.hpp"
#include "platform/identifiers.hpp"
#include "platform/secrets.hpp"
#include "platform/settings.hpp"

namespace bacuri::vms::control
{
using platform::CameraNetworks;
using platform::canonical_id;
using platform::fingerprint;
using platform::Key;
using platform::load_key;
using platform::optional_setting;
using platform::password_hash;
using platform::password_matches;
using platform::seal;
using platform::setting;
using platform::token;
using platform::unseal;
} // namespace bacuri::vms::control
