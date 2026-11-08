// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../test_fixtures.h"
#include "moonraker_client_mock.h"
#include "printer_discovery.h"
#include "toolchange_wear.h"
#include "toolchange_wear_state.h"

#include "../catch_amalgamated.hpp"

using json = nlohmann::json;
namespace tw = helix::toolchange_wear;

namespace {

helix::PrinterDiscovery u1_discovery() {
    helix::PrinterDiscovery hw;
    hw.parse_objects(json::array(
        {"extruder", "extruder1", "extruder2", "extruder3", "heater_bed", "filament_detect"}));
    return hw;
}

} // namespace

TEST_CASE("toolchange_wear reads each head's counters from its extruder", "[toolchange_wear]") {
    const tw::Reading r =
        tw::read_status(json{{"extruder", {{"switch_count", 120}, {"last_maintenance_count", 20}}},
                             {"extruder2", {{"switch_count", 7}}},
                             {"heater_bed", {{"temperature", 60.0}}}});
    CHECK(r[0].switches == 120);
    CHECK(r[0].serviced_at == 20);
    CHECK_FALSE(r[1].switches.has_value());
    CHECK(r[2].switches == 7);
    CHECK_FALSE(r[2].serviced_at.has_value());
    CHECK_FALSE(r[3].switches.has_value());
}

TEST_CASE("toolchange_wear grades a head against the firmware's interval", "[toolchange_wear]") {
    CHECK(tw::since_service(500, 100) == 400);
    // A baseline past the count (a counter reset behind our back) reads as fresh.
    CHECK(tw::since_service(10, 400) == 0);
    CHECK(tw::status_of(0) == tw::Status::Normal);
    const int warn_from = tw::kServiceInterval * tw::kWarnPct / 100;
    CHECK(tw::status_of(warn_from - 100) == tw::Status::Normal);
    CHECK(tw::status_of(warn_from + 100) == tw::Status::Warn);
    CHECK(tw::status_of(tw::kServiceInterval) == tw::Status::Due);
    CHECK(tw::used_pct(tw::kServiceInterval * 3) == 100);
}

TEST_CASE("toolchange_wear subscribes the counters of the heads discovery found",
          "[toolchange_wear]") {
    const json objects = tw::required_status_objects(u1_discovery());
    CHECK(objects.size() == 4);
    CHECK(objects["extruder3"] == json::array({"switch_count", "last_maintenance_count"}));

    helix::PrinterDiscovery plain;
    plain.parse_objects(json::array({"extruder", "extruder1", "heater_bed"}));
    CHECK(tw::required_status_objects(plain).empty());
}

TEST_CASE_METHOD(HelixTestFixture, "ToolchangeWearState publishes per-head counts",
                 "[toolchange_wear]") {
    helix::ToolchangeWearState state;
    state.init_subjects(false);
    state.set_hardware(u1_discovery());
    CHECK(lv_subject_get_int(state.available_subject()) == 0);

    // Frames carry deltas: the baseline and the count can arrive apart.
    tw::Reading first{};
    first[1].switches = 21350;
    state.apply(first);
    CHECK_FALSE(state.since_service(1).has_value());
    CHECK(lv_subject_get_int(state.available_subject()) == 0);

    tw::Reading second{};
    second[1].serviced_at = 0;
    state.apply(second);
    CHECK(state.since_service(1) == 21350);
    CHECK(lv_subject_get_int(state.available_subject()) == 1);

    // A frame silent about a head keeps what it last said.
    tw::Reading other{};
    other[0].switches = 5;
    state.apply(other);
    CHECK(state.since_service(1) == 21350);

    // A new printer forgets the old one's counts.
    helix::PrinterDiscovery plain;
    plain.parse_objects(json::array({"extruder", "heater_bed"}));
    state.set_hardware(plain);
    CHECK_FALSE(state.since_service(1).has_value());
    CHECK(lv_subject_get_int(state.available_subject()) == 0);
    state.deinit_subjects();
}

TEST_CASE("the U1 mock takes RESET_EXTRUDER_MAINTENANCE_COUNT as a new baseline",
          "[toolchange_wear][mock]") {
    MoonrakerClientMock client(MoonrakerClientMock::PrinterType::SNAPMAKER_U1);
    const int switches = client.toolchange_counters(1)["switch_count"].get<int>();
    REQUIRE(client.toolchange_counters(1)["last_maintenance_count"] == 0);
    client.gcode_script(tw::reset_gcode());
    CHECK(client.toolchange_counters(1)["last_maintenance_count"] == switches);
    CHECK(client.toolchange_counters(4).empty());
}
