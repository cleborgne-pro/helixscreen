// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_observer_guard.h"
#include "ui_timer_guard.h"

#include "async_lifetime_guard.h"
#include "print_lifecycle_state.h"
#include "print_window.h"

#include <chrono>
#include <functional>
#include <optional>
#include <string>

namespace helix {

class IMoonrakerAPI;
class PrinterState;

/**
 * @brief Carries out print hours (print_window.h) on the connected printer.
 *
 * Every kTickSeconds it asks print_window::decide() what the print should do
 * at this time of day, and does it:
 *  - schedules the printer's pause at the next layer change
 *    (SET_PAUSE_NEXT_LAYER), so the print stops where the seam shows least;
 *    a print that reports no layers, or whose layer has not changed after
 *    kMaxLayerWaitMinutes, pauses where it is instead;
 *  - holds Klipper's idle_timeout off for the night, so a printer whose
 *    timeout also runs while paused does not turn the bed and the motors off;
 *  - at the resume time resumes only a pause it took, and only while the bed
 *    is still heated and no filament sensor reports a runout.
 *
 * A user who resumes the print, or cancels the scheduled pause, during the
 * quiet hours keeps it running until they end.
 */
class PrintWindowController {
  public:
    using MinuteClock = std::function<int()>; ///< minute of the local day, 0-1439
    using WindowSource = std::function<print_window::Window()>;
    using SteadyClock = std::function<std::chrono::steady_clock::time_point()>;

    static constexpr uint32_t kTickSeconds = 20;

    PrintWindowController(IMoonrakerAPI* api, PrinterState& state, WindowSource window,
                          MinuteClock minute_of_day, SteadyClock steady = {});
    ~PrintWindowController();
    PrintWindowController(const PrintWindowController&) = delete;
    PrintWindowController& operator=(const PrintWindowController&) = delete;

    /// One decision. The timer calls it; tests call it with their own clocks.
    void tick();

    /// The local clock's minute of the day.
    static int local_minute_of_day();

  private:
    void on_lifecycle(PrintState now);
    void on_paused();
    void on_running_again();
    void reset_for_print();

    void arm_layer_pause();
    void pause_now();
    void disarm();
    void resume();

    void send(const std::string& gcode, std::function<void()> on_sent = {});
    void hold_idle_timeout(int minutes);
    void release_idle_timeout();
    [[nodiscard]] int minutes_until_resume() const;

    IMoonrakerAPI* api_;
    PrinterState& state_;
    WindowSource window_;
    MinuteClock minute_of_day_;
    SteadyClock steady_;

    bool armed_ = false;
    std::chrono::steady_clock::time_point armed_at_{};
    bool paused_by_window_ = false;
    bool pause_sent_ = false;
    bool resume_sent_ = false;
    bool user_override_ = false;
    int bed_target_at_pause_ = 0;
    /// The configured idle_timeout, while a longer one is held; nullopt otherwise.
    std::optional<int> idle_restore_s_;
    std::optional<PrintState> last_lifecycle_;

    ObserverGuard lifecycle_observer_;
    helix::ui::LvglTimerGuard timer_;
    AsyncLifetimeGuard lifetime_;
};

} // namespace helix
