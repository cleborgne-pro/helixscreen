// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "json_fwd.h"

#include <array>
#include <optional>
#include <string>

/**
 * @file toolchange_wear.h
 * @brief Tool changes each head has made since it was last serviced.
 *
 * The Snapmaker U1 firmware counts every pick of a head (its extruder switch
 * recorder, klippy/kinematics/extruder.py) and keeps the count reached at the
 * last service. Each `extruder*` status object carries both, `switch_count`
 * and `last_maintenance_count`. Past kServiceInterval changes on one head it
 * raises fault 0001-0523-0000-0037; RESET_EXTRUDER_MAINTENANCE_COUNT takes the
 * current counts as the new baseline for every head.
 *
 * This module only reads: which fields to subscribe, and what a frame says.
 * ToolchangeWearState keeps the readings and publishes them.
 */
namespace helix {
class PrinterDiscovery;
} // namespace helix

namespace helix::toolchange_wear {

inline constexpr int kHeads = 4;
/// The firmware's default individual_maintenance_threshold. Not in any status
/// object, so a printer.cfg that changes it is not seen here.
inline constexpr int kServiceInterval = 25100;
/// Share of the interval past which the head reads as due soon.
inline constexpr int kWarnPct = 80;

/// One head's counters; a field the frame did not carry is unset.
struct HeadReading {
    std::optional<int> switches;
    std::optional<int> serviced_at;
};
using Reading = std::array<HeadReading, kHeads>;

enum class Status { Normal = 0, Warn = 1, Due = 2 };

/// Whether the printer keeps the counters (the Snapmaker toolchanger).
[[nodiscard]] bool supported(const PrinterDiscovery& hw);

/// The extruder fields to subscribe, merged with the other modules' field lists.
[[nodiscard]] nlohmann::json required_status_objects(const PrinterDiscovery& hw);

/// What one status frame says about each head.
[[nodiscard]] Reading read_status(const nlohmann::json& status);

/// Changes since the last service; never negative.
[[nodiscard]] int since_service(int switches, int serviced_at);
[[nodiscard]] int used_pct(int since);
[[nodiscard]] Status status_of(int since);

/// The command that starts a new interval for every head.
[[nodiscard]] const char* reset_gcode();

} // namespace helix::toolchange_wear
