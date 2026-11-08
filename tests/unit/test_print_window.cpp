// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "print_window.h"

#include <algorithm>

#include "../catch_amalgamated.hpp"

namespace pw = helix::print_window;

namespace {

constexpr int at(int hour, int minute = 0) {
    return hour * 60 + minute;
}

pw::Window night() {
    return {true, 40, 18}; // 20:00 -> 09:00
}

pw::Status running() {
    pw::Status s;
    s.printing = true;
    s.layer_pause_ready = true;
    return s;
}

} // namespace

TEST_CASE("print hours: the quiet hours wrap over midnight", "[print_window]") {
    const pw::Window w = night();
    CHECK_FALSE(pw::in_quiet_hours(w, at(19, 59)));
    CHECK(pw::in_quiet_hours(w, at(20)));
    CHECK(pw::in_quiet_hours(w, at(0, 30)));
    CHECK(pw::in_quiet_hours(w, at(8, 59)));
    CHECK_FALSE(pw::in_quiet_hours(w, at(9)));

    const pw::Window lunch{true, 24, 28}; // 12:00 -> 14:00, same day
    CHECK(pw::in_quiet_hours(lunch, at(13)));
    CHECK_FALSE(pw::in_quiet_hours(lunch, at(14)));
    CHECK_FALSE(pw::in_quiet_hours(lunch, at(11, 59)));

    // Equal times stop nothing.
    CHECK_FALSE(pw::in_quiet_hours({true, 18, 18}, at(9)));
}

TEST_CASE("print hours: slots read as times of day", "[print_window]") {
    CHECK(pw::slot_label(0) == "00:00");
    CHECK(pw::slot_label(19) == "09:30");
    CHECK(pw::slot_label(47) == "23:30");
    CHECK(pw::slot_label(99) == "23:30");
    const std::string options = pw::slot_options();
    CHECK(options.rfind("00:00\n00:30\n", 0) == 0);
    CHECK(std::count(options.begin(), options.end(), '\n') == pw::kSlotsPerDay - 1);
}

TEST_CASE("print hours: a running print pauses at its next layer change", "[print_window]") {
    CHECK(pw::decide(night(), at(19, 59), running()) == pw::Step::None);
    CHECK(pw::decide(night(), at(20), running()) == pw::Step::ArmLayerPause);

    pw::Status armed = running();
    armed.armed = true;
    armed.armed_minutes = 10;
    CHECK(pw::decide(night(), at(20, 10), armed) == pw::Step::None);
    // A layer that takes too long: pause where the print is.
    armed.armed_minutes = pw::kMaxLayerWaitMinutes;
    CHECK(pw::decide(night(), at(20, 45), armed) == pw::Step::PauseNow);
}

TEST_CASE("print hours: without a layer to wait for", "[print_window]") {
    pw::Status no_layers = running();
    no_layers.layer_pause_ready = false;
    CHECK(pw::decide(night(), at(21), no_layers) == pw::Step::PauseNow);

    // The last layer finishes: a pause would mark the top surface.
    pw::Status last = running();
    last.layer_pause_ready = false;
    last.last_layer = true;
    CHECK(pw::decide(night(), at(21), last) == pw::Step::None);
}

TEST_CASE("print hours: only the window's own pause resumes", "[print_window]") {
    pw::Status paused;
    paused.paused = true;
    paused.paused_by_window = true;
    CHECK(pw::decide(night(), at(8, 59), paused) == pw::Step::None);
    CHECK(pw::decide(night(), at(9), paused) == pw::Step::Resume);

    paused.paused_by_window = false;
    CHECK(pw::decide(night(), at(9), paused) == pw::Step::None);
}

TEST_CASE("print hours: the user's choice stands until the quiet hours end", "[print_window]") {
    pw::Status taken_back = running();
    taken_back.user_override = true;
    CHECK(pw::decide(night(), at(23), taken_back) == pw::Step::None);
}

TEST_CASE("print hours: a schedule no longer wanted is cancelled", "[print_window]") {
    pw::Status armed = running();
    armed.armed = true;
    CHECK(pw::decide(night(), at(9), armed) == pw::Step::Disarm);

    pw::Window off = night();
    off.enabled = false;
    CHECK(pw::decide(off, at(22), armed) == pw::Step::Disarm);
    CHECK(pw::decide(off, at(22), running()) == pw::Step::None);
}
