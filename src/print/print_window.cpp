// SPDX-License-Identifier: GPL-3.0-or-later
#include "print_window.h"

#include <spdlog/fmt/fmt.h>

#include <algorithm>

namespace helix::print_window {

namespace {

int clamp_slot(int slot) {
    return std::clamp(slot, 0, kSlotsPerDay - 1);
}

int slot_start(int slot) {
    return clamp_slot(slot) * kSlotMinutes;
}

} // namespace

std::string slot_label(int slot) {
    const int minutes = slot_start(slot);
    return fmt::format("{:02d}:{:02d}", minutes / 60, minutes % 60);
}

std::string slot_options() {
    std::string options;
    for (int slot = 0; slot < kSlotsPerDay; ++slot) {
        if (slot > 0) {
            options += '\n';
        }
        options += slot_label(slot);
    }
    return options;
}

bool in_quiet_hours(const Window& w, int minute_of_day) {
    const int stop = slot_start(w.stop_slot);
    const int resume = slot_start(w.resume_slot);
    if (stop == resume) {
        return false;
    }
    if (stop < resume) {
        return minute_of_day >= stop && minute_of_day < resume;
    }
    // Over midnight: from the stop time to the end of the day, then from the
    // start of the day to the resume time.
    return minute_of_day >= stop || minute_of_day < resume;
}

Step decide(const Window& w, int minute_of_day, const Status& s) {
    const bool quiet = w.enabled && in_quiet_hours(w, minute_of_day);
    if (!quiet) {
        if (s.paused && s.paused_by_window) {
            return Step::Resume;
        }
        return s.armed ? Step::Disarm : Step::None;
    }
    if (!s.printing || s.user_override) {
        return Step::None;
    }
    if (s.armed) {
        return s.armed_minutes >= kMaxLayerWaitMinutes ? Step::PauseNow : Step::None;
    }
    if (s.last_layer) {
        // Pausing now would mark the top surface; the print is nearly done.
        return Step::None;
    }
    return s.layer_pause_ready ? Step::ArmLayerPause : Step::PauseNow;
}

} // namespace helix::print_window
