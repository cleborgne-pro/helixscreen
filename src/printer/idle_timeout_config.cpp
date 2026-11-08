// SPDX-License-Identifier: GPL-3.0-or-later
#include "idle_timeout_config.h"

#include "core/text_io.h"

#include "hv/json.hpp"

namespace helix {

std::optional<int> configured_idle_timeout_s(const nlohmann::json& config) {
    int configured_s = kKlipperDefaultIdleTimeoutS;
    if (config.is_object() && config.contains("idle_timeout") &&
        config["idle_timeout"].contains("timeout")) {
        const auto& t = config["idle_timeout"]["timeout"];
        if (t.is_string()) {
            const auto parsed = text_io::parse_leading<float>(t.get_ref<const std::string&>());
            if (!parsed) {
                return std::nullopt;
            }
            configured_s = static_cast<int>(*parsed);
        } else if (t.is_number()) {
            configured_s = static_cast<int>(t.get<double>());
        }
    }
    if (configured_s <= 0) {
        return std::nullopt;
    }
    return configured_s;
}

} // namespace helix
