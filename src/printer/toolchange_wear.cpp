// SPDX-License-Identifier: GPL-3.0-or-later
#include "toolchange_wear.h"

#include "ams_type.h"
#include "klipper_extruder_naming.h"
#include "printer_discovery.h"

#include <algorithm>

#include "hv/json.hpp"

namespace helix::toolchange_wear {

namespace {

std::optional<int> int_field(const nlohmann::json& obj, const char* key) {
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_number()) {
        return std::nullopt;
    }
    return static_cast<int>(it->get<double>());
}

} // namespace

bool supported(const PrinterDiscovery& hw) {
    // The counters are fields of the Snapmaker fork's extruder objects.
    // VENDOR_OK: no object or macro of their own announces them
    return hw.mmu_type() == AmsType::SNAPMAKER;
}

nlohmann::json required_status_objects(const PrinterDiscovery& hw) {
    nlohmann::json objects = nlohmann::json::object();
    if (!supported(hw)) {
        return objects;
    }
    // Only the heads discovery found: a name Klipper does not know makes
    // Moonraker refuse the whole subscription.
    for (const auto& name : hw.heaters()) {
        if (is_extruder_name(name)) {
            objects[name] = {"switch_count", "last_maintenance_count"};
        }
    }
    return objects;
}

Reading read_status(const nlohmann::json& status) {
    Reading reading{};
    if (!status.is_object()) {
        return reading;
    }
    for (int head = 0; head < kHeads; ++head) {
        const auto it = status.find(extruder_name_for_tool(head));
        if (it == status.end() || !it->is_object()) {
            continue;
        }
        auto& r = reading[static_cast<size_t>(head)];
        r.switches = int_field(*it, "switch_count");
        r.serviced_at = int_field(*it, "last_maintenance_count");
    }
    return reading;
}

int since_service(int switches, int serviced_at) {
    return std::max(0, switches - serviced_at);
}

int used_pct(int since) {
    return std::clamp(static_cast<int>(100LL * since / kServiceInterval), 0, 100);
}

Status status_of(int since) {
    if (since >= kServiceInterval) {
        return Status::Due;
    }
    return used_pct(since) >= kWarnPct ? Status::Warn : Status::Normal;
}

const char* reset_gcode() {
    return "RESET_EXTRUDER_MAINTENANCE_COUNT";
}

} // namespace helix::toolchange_wear
