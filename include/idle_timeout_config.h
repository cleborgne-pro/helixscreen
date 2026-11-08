// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "json_fwd.h"

#include <optional>

namespace helix {

/// Klipper's default idle_timeout.timeout, used when printer.cfg sets none.
inline constexpr int kKlipperDefaultIdleTimeoutS = 600;

/**
 * @brief The idle_timeout.timeout printer.cfg sets, in seconds.
 *
 * @p config is configfile.config (every value a string as written). Klipper's
 * default when the section or the option is absent; nullopt when the value
 * cannot be read or is not positive, so a caller that would restore it holds
 * nothing it cannot put back.
 */
[[nodiscard]] std::optional<int> configured_idle_timeout_s(const nlohmann::json& config);

} // namespace helix
