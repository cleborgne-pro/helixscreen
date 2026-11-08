// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../test_helpers/gcode_recording_api.h"
#include "core/update_queue.h"
#include "lvgl_test_fixture.h"
#include "moonraker_client_mock.h"
#include "print_lifecycle_state.h"
#include "print_window_controller.h"
#include "printer_state.h"

#include <algorithm>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using json = nlohmann::json;
namespace pw = helix::print_window;

namespace {

constexpr int at(int hour, int minute = 0) {
    return hour * 60 + minute;
}

/// Answers printer.cfg at once, with the idle_timeout a stock config leaves.
struct WindowApi : public helix::test::GcodeRecordingApi {
    using GcodeRecordingApi::GcodeRecordingApi;
    void query_configfile(JsonCallback on_success, ErrorCallback) override {
        on_success(json{{"idle_timeout", {{"timeout", "600"}}}});
    }
};

class PrintWindowFixture : public LVGLTestFixture {
  public:
    PrintWindowFixture() : client(MoonrakerClientMock::PrinterType::VORON_24), api(client, state) {
        state.init_subjects(false);
        controller = std::make_unique<helix::PrintWindowController>(
            &api, state, [this] { return window; }, [this] { return minute; },
            [this] { return steady; });
        print("printing", 3);
        drain();
    }
    ~PrintWindowFixture() override {
        controller.reset();
        drain();
        state.deinit_subjects();
    }

    static void drain() {
        helix::ui::UpdateQueue::instance().drain();
    }
    void print(const char* wire_state, int layer = 3) {
        state.update_from_status({{"print_stats",
                                   {{"filename", "night.gcode"},
                                    {"state", wire_state},
                                    {"info", {{"current_layer", layer}, {"total_layer", 100}}}}}});
        drain();
    }
    void tick_at(int minute_of_day) {
        minute = minute_of_day;
        controller->tick();
        drain();
    }
    bool sent(const std::string& gcode) const {
        return std::find(api.sent.begin(), api.sent.end(), gcode) != api.sent.end();
    }
    /// The printer's next-layer macro fires: it clears its schedule and pauses.
    void layer_pause_fires() {
        state.print_state().note_layer_pause_next_layer(false);
        print("paused", 4);
        drain(); // the idle_timeout hold waits for printer.cfg's answer
    }

    MoonrakerClientMock client;
    helix::PrinterState state;
    WindowApi api;
    pw::Window window{true, 40, 18}; // 20:00 -> 09:00
    int minute = at(19);
    std::chrono::steady_clock::time_point steady{};
    std::unique_ptr<helix::PrintWindowController> controller;
};

} // namespace

TEST_CASE_METHOD(PrintWindowFixture,
                 "print hours: a night's pause at a layer change, bed held, resumed in the morning",
                 "[print_window][controller]") {
    REQUIRE(state.print_state().get_print_lifecycle() == PrintState::Printing);
    tick_at(at(19, 59));
    CHECK(api.sent.empty());

    tick_at(at(20));
    REQUIRE(api.sent == std::vector<std::string>{"SET_PAUSE_NEXT_LAYER ENABLE=1"});
    tick_at(at(20, 5)); // waiting for the layer change sends nothing more
    CHECK(api.sent.size() == 1);

    layer_pause_fires();
    // Paused at 20:05: held off until an hour past 09:00 (12 h 55 + 1 h).
    CHECK(sent("SET_IDLE_TIMEOUT TIMEOUT=50100"));

    tick_at(at(8, 59));
    CHECK_FALSE(sent("RESUME"));
    tick_at(at(9));
    CHECK(sent("RESUME"));

    print("printing", 4);
    CHECK(api.sent.back() == "SET_IDLE_TIMEOUT TIMEOUT=600");
}

TEST_CASE_METHOD(PrintWindowFixture, "print hours: a print taken back by hand stays running",
                 "[print_window][controller]") {
    tick_at(at(20));
    layer_pause_fires();
    print("printing", 4); // the user resumes at once
    api.sent.clear();

    tick_at(at(23));
    CHECK(api.sent.empty());
    tick_at(at(9));
    CHECK(api.sent.empty());
    // The next evening the window applies again.
    tick_at(at(20));
    CHECK(sent("SET_PAUSE_NEXT_LAYER ENABLE=1"));
}

TEST_CASE_METHOD(PrintWindowFixture, "print hours: another reason's pause is not resumed",
                 "[print_window][controller]") {
    tick_at(at(20));
    // A fault pauses the print before the layer change: the firmware reports it.
    state.update_from_status(
        {{"print_stats",
          {{"state", "paused"},
           {"exception", {{"id", 523}, {"index", 1}, {"code", 38}, {"message", "tangled"}}}}}});
    drain();
    tick_at(at(9));
    CHECK_FALSE(sent("RESUME"));
}

TEST_CASE_METHOD(PrintWindowFixture, "print hours: a layer that never ends pauses where it is",
                 "[print_window][controller]") {
    tick_at(at(20));
    steady += std::chrono::minutes(pw::kMaxLayerWaitMinutes);
    tick_at(at(20, 45));
    // The schedule goes first, or it would pause again after the resume.
    REQUIRE(api.sent.size() == 3);
    CHECK(api.sent[1] == "SET_PAUSE_NEXT_LAYER ENABLE=0");
    CHECK(api.sent[2] == "PAUSE");

    print("paused", 3);
    tick_at(at(9));
    CHECK(sent("RESUME"));
}

TEST_CASE_METHOD(PrintWindowFixture,
                 "print hours: a cold bed in the morning keeps the print paused",
                 "[print_window][controller]") {
    state.update_from_status({{"heater_bed", {{"temperature", 60.0}, {"target", 60.0}}}});
    tick_at(at(20));
    layer_pause_fires();
    state.update_from_status({{"heater_bed", {{"temperature", 20.0}, {"target", 0.0}}}});
    drain();
    tick_at(at(9));
    CHECK_FALSE(sent("RESUME"));
}

TEST_CASE_METHOD(PrintWindowFixture, "print hours: turned off, nothing happens",
                 "[print_window][controller]") {
    window.enabled = false;
    tick_at(at(22));
    CHECK(api.sent.empty());
}
