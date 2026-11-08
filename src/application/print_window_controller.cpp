// SPDX-License-Identifier: GPL-3.0-or-later
#include "print_window_controller.h"

#include "core/observer_factory.h"
#include "filament_sensor_manager.h"
#include "i_moonraker_api.h"
#include "idle_timeout_config.h"
#include "layer_pause.h"
#include "non_filament_pause.h"
#include "notice.h"
#include "print_lifecycle_state.h"
#include "printer_state.h"
#include "translation_loader.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <ctime>

namespace helix {

namespace lp = helix::layer_pause;
namespace pw = helix::print_window;

namespace {

/// Kept past the resume time, so a resume that is held back (a runout, a
/// cold bed) still finds the motors holding the print where it stopped.
constexpr int kIdleHoldMarginMinutes = 60;
constexpr int kMinutesPerDay = 24 * 60;
constexpr uint32_t kNoticeMs = 8000;

void notify(NoticeSeverity severity, const std::string& text) {
    publish_notice({severity, text, kNoticeMs});
}

} // namespace

int PrintWindowController::local_minute_of_day() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    return local.tm_hour * 60 + local.tm_min;
}

PrintWindowController::PrintWindowController(IMoonrakerAPI* api, PrinterState& state,
                                             WindowSource window, MinuteClock minute_of_day,
                                             SteadyClock steady)
    : api_(api), state_(state), window_(std::move(window)),
      minute_of_day_(std::move(minute_of_day)), steady_(std::move(steady)) {
    if (!steady_) {
        steady_ = [] { return std::chrono::steady_clock::now(); };
    }
    lifecycle_observer_ = helix::ui::observe_print_lifecycle(
        state_.print_state().get_print_lifecycle_subject(), this,
        [](PrintWindowController* self, PrintState now) { self->on_lifecycle(now); },
        state_.get_subjects_lifetime());
    timer_.reset(lv_timer_create(
        [](lv_timer_t* t) {
            static_cast<PrintWindowController*>(lv_timer_get_user_data(t))->tick();
        },
        kTickSeconds * 1000, this));
}

PrintWindowController::~PrintWindowController() {
    lifetime_.invalidate();
}

void PrintWindowController::on_lifecycle(PrintState now) {
    const std::optional<PrintState> before = last_lifecycle_;
    last_lifecycle_ = now;
    switch (now) {
    case PrintState::Paused:
        if (before != PrintState::Paused) {
            on_paused();
        }
        break;
    case PrintState::Printing:
        if (before == PrintState::Paused) {
            on_running_again();
        }
        break;
    case PrintState::Preparing:
        if (before != PrintState::Printing && before != PrintState::Paused) {
            reset_for_print();
        }
        break;
    case PrintState::Idle:
    case PrintState::Complete:
    case PrintState::Cancelled:
    case PrintState::Error:
        reset_for_print();
        break;
    }
}

void PrintWindowController::on_paused() {
    // While ours is scheduled, a pause is ours unless something else explains
    // it: a runout, a camera detection, a fault the firmware reports. Those
    // are not ours to resume. The schedule's own read-back cannot decide it:
    // the macro clears it in a frame that may come after the pause.
    const bool explained_elsewhere = FilamentSensorManager::instance().has_real_runout() ||
                                     non_filament_pause::explained() ||
                                     state_.print_state().get_print_exception_id() >= 0;
    if (!pause_sent_ && !(armed_ && !explained_elsewhere)) {
        if (armed_) {
            notify(NoticeSeverity::WARNING,
                   lv_tr("The print paused for another reason: print hours will not resume it"));
        }
        return;
    }
    spdlog::info("[PrintWindow] Paused for the quiet hours ({})",
                 pause_sent_ ? "where it was" : "at a layer change");
    paused_by_window_ = true;
    armed_ = false;
    pause_sent_ = false;
    bed_target_at_pause_ = lv_subject_get_int(state_.temperature_state().get_bed_target_subject());
    hold_idle_timeout(minutes_until_resume() + kIdleHoldMarginMinutes);
}

void PrintWindowController::on_running_again() {
    if (paused_by_window_ && !resume_sent_) {
        // Taken back by hand: the user wants it running, whatever the hour.
        spdlog::info("[PrintWindow] Resumed by hand during the quiet hours");
        user_override_ = true;
    }
    paused_by_window_ = false;
    resume_sent_ = false;
    release_idle_timeout();
}

void PrintWindowController::reset_for_print() {
    armed_ = false;
    paused_by_window_ = false;
    pause_sent_ = false;
    resume_sent_ = false;
    user_override_ = false;
    release_idle_timeout();
}

void PrintWindowController::tick() {
    if (!api_) {
        return;
    }
    const pw::Window window = window_();
    const int minute = minute_of_day_();
    const bool quiet = window.enabled && pw::in_quiet_hours(window, minute);
    if (!quiet) {
        user_override_ = false;
    }
    const auto lifecycle = state_.print_state().get_print_lifecycle();
    const bool printing = lifecycle == PrintState::Printing;
    // Our schedule gone while the print still runs: the user cancelled it.
    if (armed_ && printing && !state_.print_state().get_layer_pause_schedule().next_layer) {
        spdlog::info("[PrintWindow] The scheduled pause was cancelled by hand");
        armed_ = false;
        user_override_ = quiet;
    }
    const auto availability = static_cast<lp::Availability>(
        lv_subject_get_int(state_.print_state().get_layer_pause_availability_subject()));
    pw::Status status;
    status.printing = printing;
    status.paused = lifecycle == PrintState::Paused;
    status.layer_pause_ready = availability == lp::Availability::Ready;
    status.last_layer = availability == lp::Availability::LastLayer;
    status.armed = armed_;
    status.armed_minutes =
        armed_
            ? static_cast<int>(
                  std::chrono::duration_cast<std::chrono::minutes>(steady_() - armed_at_).count())
            : 0;
    status.paused_by_window = paused_by_window_;
    status.user_override = user_override_;

    switch (pw::decide(window, minute, status)) {
    case pw::Step::None:
        break;
    case pw::Step::ArmLayerPause:
        arm_layer_pause();
        break;
    case pw::Step::PauseNow:
        pause_now();
        break;
    case pw::Step::Disarm:
        disarm();
        break;
    case pw::Step::Resume:
        resume();
        break;
    }
}

void PrintWindowController::arm_layer_pause() {
    armed_ = true;
    armed_at_ = steady_();
    spdlog::info("[PrintWindow] Quiet hours: pausing at the next layer change");
    send(lp::schedule_next_layer_command(),
         [this] { state_.print_state().note_layer_pause_next_layer(true); });
    const pw::Window window = window_();
    notify(NoticeSeverity::INFO,
           fmt::format(fmt::runtime(lv_tr("Print hours: the print pauses at the next layer "
                                          "change and resumes at {}")),
                       pw::slot_label(window.resume_slot)));
}

void PrintWindowController::pause_now() {
    if (armed_) {
        // Still scheduled, it would pause again at the first layer after the resume.
        send(lp::cancel_next_layer_command(),
             [this] { state_.print_state().note_layer_pause_next_layer(false); });
        armed_ = false;
    }
    spdlog::info("[PrintWindow] Quiet hours: pausing where the print is");
    pause_sent_ = true;
    send("PAUSE");
}

void PrintWindowController::disarm() {
    spdlog::info("[PrintWindow] Quiet hours over before a layer change: pause cancelled");
    armed_ = false;
    send(lp::cancel_next_layer_command(),
         [this] { state_.print_state().note_layer_pause_next_layer(false); });
}

void PrintWindowController::resume() {
    // Each check that holds the print back gives the decision to the user:
    // the window stops treating the pause as its own.
    std::string held_back;
    if (FilamentSensorManager::instance().has_real_runout()) {
        held_back = lv_tr("a filament sensor reports no filament");
    } else if (bed_target_at_pause_ > 0 &&
               lv_subject_get_int(state_.temperature_state().get_bed_target_subject()) == 0) {
        held_back = lv_tr("the bed was turned off, so the print may have come loose");
    }
    if (!held_back.empty()) {
        spdlog::warn("[PrintWindow] Not resuming: {}", held_back);
        paused_by_window_ = false;
        notify(
            NoticeSeverity::WARNING,
            fmt::format(fmt::runtime(lv_tr("Print hours are over, but the print stays paused: {}")),
                        held_back));
        return;
    }
    spdlog::info("[PrintWindow] Quiet hours over: resuming");
    resume_sent_ = true;
    send("RESUME");
    notify(NoticeSeverity::INFO, lv_tr("Print hours: resuming the print"));
}

void PrintWindowController::send(const std::string& gcode, std::function<void()> on_sent) {
    api_->execute_gcode(
        gcode,
        lifetime_.bg_cb("PrintWindowController::sent",
                        [on_sent = std::move(on_sent)]() {
                            if (on_sent) {
                                on_sent();
                            }
                        }),
        // Log only: Klipper's own `!!` line reaches the screen through
        // the error router, so this caller does not claim the report.
        [gcode](const MoonrakerError& err) {
            spdlog::error("[PrintWindow] '{}' failed: {}", gcode, err.message);
        },
        0, false, nullptr, /*caller_surfaces_errors=*/false);
}

void PrintWindowController::hold_idle_timeout(int minutes) {
    auto tok = lifetime_.token();
    api_->query_configfile(
        [this, tok, minutes](const nlohmann::json& config) {
            // Background (WS) thread: parse only. Without the configured value
            // there is nothing to put back afterwards, so nothing is held.
            const auto configured_s = configured_idle_timeout_s(config);
            if (!configured_s) {
                return;
            }
            tok.defer("PrintWindowController::hold_idle_timeout",
                      [this, minutes, s = *configured_s]() {
                          if (!paused_by_window_) {
                              return;
                          }
                          idle_restore_s_ = s;
                          send(fmt::format("SET_IDLE_TIMEOUT TIMEOUT={}", minutes * 60));
                      });
        },
        [](const MoonrakerError& err) {
            spdlog::warn("[PrintWindow] printer.cfg unreadable, idle_timeout not held: {}",
                         err.message);
        });
}

void PrintWindowController::release_idle_timeout() {
    if (!idle_restore_s_) {
        return;
    }
    send(fmt::format("SET_IDLE_TIMEOUT TIMEOUT={}", *idle_restore_s_));
    idle_restore_s_.reset();
}

int PrintWindowController::minutes_until_resume() const {
    const pw::Window window = window_();
    const int resume = window.resume_slot * pw::kSlotMinutes;
    return (resume - minute_of_day_() + kMinutesPerDay) % kMinutesPerDay;
}

} // namespace helix
