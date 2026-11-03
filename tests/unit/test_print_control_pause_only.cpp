// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_print_control_pause_only.cpp
 * @brief The lock screen's Pause: pauses a running print and never resumes one.
 */

#include "ui_update_queue.h"

#include "../lvgl_test_fixture.h"
#include "../test_helpers/moonraker_client_mock_test_access.h"
#include "../test_helpers/print_control_buttons_test_access.h"
#include "../test_helpers/printer_state_test_access.h"
#include "../test_helpers/update_queue_test_access.h"
#include "app_globals.h"
#include "moonraker_api.h"
#include "moonraker_client_mock.h"
#include "print_control_buttons.h"
#include "printer_discovery.h"
#include "printer_state.h"
#include "standard_macros.h"

#include <memory>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

using helix::ui::PendingAction;
using helix::ui::PrintControlButtons;
using helix::ui::PrintControlButtonsTestAccess;
using json = nlohmann::json;

namespace {

struct PauseOnlyFixture : LVGLTestFixture {
    PauseOnlyFixture() : client(MoonrakerClientMock::PrinterType::VORON_24) {
        PrintControlButtonsTestAccess::reset();
        helix::PrinterStateTestAccess::reset(get_printer_state());
        get_printer_state().init_subjects(false);
        // Below READY, the API refuses G-code before it reaches the client.
        get_printer_state().set_klippy_state_sync(helix::KlippyState::READY);

        helix::PrinterDiscovery hardware;
        hardware.parse_objects(json{"extruder", "gcode_macro PAUSE", "gcode_macro RESUME",
                                    "gcode_macro CANCEL_PRINT"});
        StandardMacros::instance().reset();
        StandardMacros::instance().init(hardware);

        PrintControlButtons::instance().init_subjects();

        client.connect("ws://mock/websocket", []() {}, []() {});
        client.clear_gcode_script_history();
        api = std::make_unique<MoonrakerAPI>(client, get_printer_state());
        PrintControlButtons::instance().set_api(api.get());
    }

    ~PauseOnlyFixture() override {
        PrintControlButtons::instance().set_api(nullptr);
        StandardMacros::instance().reset();
        drain();
    }

    static void drain() {
        helix::ui::UpdateQueueTestAccess::drain_all(helix::ui::UpdateQueue::instance());
    }

    static void set_job_state(const std::string& state) {
        get_printer_state().update_from_status(
            json{{"print_stats", {{"state", state}, {"filename", "part.gcode"}}}});
        drain();
    }

    static int pause_available() {
        return lv_subject_get_int(lv_xml_get_subject(nullptr, "print_control_pause_available"));
    }

    /// Every script sent that names one of the pause/resume macros.
    std::vector<std::string> print_control_scripts() const {
        std::vector<std::string> out;
        for (const auto& script : client.gcode_script_history()) {
            if (script.find("PAUSE") != std::string::npos ||
                script.find("RESUME") != std::string::npos)
                out.push_back(script);
        }
        return out;
    }

    MoonrakerClientMock client;
    std::unique_ptr<MoonrakerAPI> api;
};

} // namespace

TEST_CASE_METHOD(PauseOnlyFixture, "pause-only pauses a running print", "[print_control][lock]") {
    set_job_state("printing");
    REQUIRE(pause_available() == 1);

    PrintControlButtons::instance().request_pause();
    drain();

    const auto scripts = print_control_scripts();
    REQUIRE(scripts.size() == 1);
    CHECK(scripts[0] == "PAUSE");
    CHECK(PrintControlButtonsTestAccess::pending() == PendingAction::Pausing);
    CHECK(pause_available() == 0);
}

TEST_CASE_METHOD(PauseOnlyFixture, "pause-only never resumes a paused print",
                 "[print_control][lock]") {
    set_job_state("paused");
    CHECK(pause_available() == 0);

    PrintControlButtons::instance().request_pause();
    drain();

    CHECK(print_control_scripts().empty());
    CHECK(PrintControlButtonsTestAccess::pending() == PendingAction::None);
}

TEST_CASE_METHOD(PauseOnlyFixture, "pause-only does nothing without a print",
                 "[print_control][lock]") {
    set_job_state("standby");
    CHECK(pause_available() == 0);

    PrintControlButtons::instance().request_pause();
    drain();

    CHECK(print_control_scripts().empty());
}
