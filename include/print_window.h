// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

/**
 * @file print_window.h
 * @brief Print hours: what a running print does at the edges of a daily window.
 *
 * The user sets a stop time and a resume time (20:00 and 09:00, say). A print
 * running at the stop time pauses at its next layer change, where the seam is
 * least visible, through the printer's own SET_PAUSE_NEXT_LAYER
 * (layer_pause.h). The printer's pause keeps the bed at temperature. At the
 * resume time the print resumes, but only a pause the window took.
 *
 * Times are half-hour slots of the local day, 0 = 00:00 .. 47 = 23:30. A stop
 * slot after the resume slot wraps over midnight; equal slots make an empty
 * window, which never stops anything.
 *
 * This module only decides. PrintWindowController feeds it the clock and the
 * print's state and carries the decision out.
 */
namespace helix::print_window {

inline constexpr int kSlotMinutes = 30;
inline constexpr int kSlotsPerDay = 24 * 60 / kSlotMinutes;
inline constexpr int kDefaultStopSlot = 40;   ///< 20:00
inline constexpr int kDefaultResumeSlot = 18; ///< 09:00
/// A layer change this long in coming means a huge layer: pause where the
/// print is rather than print on into the quiet hours.
inline constexpr int kMaxLayerWaitMinutes = 45;

struct Window {
    bool enabled = false;
    int stop_slot = kDefaultStopSlot;
    int resume_slot = kDefaultResumeSlot;
};

/// "HH:MM" for a slot; out-of-range slots are clamped.
[[nodiscard]] std::string slot_label(int slot);
/// Every slot's label, newline-separated, for a dropdown.
[[nodiscard]] std::string slot_options();

/// Whether @p minute_of_day (0-1439) falls inside the quiet hours.
[[nodiscard]] bool in_quiet_hours(const Window& w, int minute_of_day);

/// What the print looks like to the window at one tick.
struct Status {
    bool printing = false; ///< running, or preparing to
    bool paused = false;
    /// The printer can pause at the next layer change of this print.
    bool layer_pause_ready = false;
    /// The print is on its last layer: nothing is left to pause before.
    bool last_layer = false;
    bool armed = false;            ///< we scheduled the next-layer pause
    int armed_minutes = 0;         ///< how long it has been scheduled
    bool paused_by_window = false; ///< the current pause is ours
    /// The user took the print back during these quiet hours (resumed our
    /// pause, or cancelled our scheduled one): leave it alone until they end.
    bool user_override = false;
};

enum class Step {
    None,
    ArmLayerPause, ///< schedule the pause at the next layer change
    PauseNow,      ///< no layer change to wait for: pause where the print is
    Disarm,        ///< cancel the scheduled pause, it is no longer wanted
    Resume,
};

[[nodiscard]] Step decide(const Window& w, int minute_of_day, const Status& s);

} // namespace helix::print_window
