// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The tool changes each head has made since its last service, as the printer
// last reported them, and the subjects the Maintenance overlay binds.
// helix::toolchange_wear decides what a status frame says; this class keeps the
// last value of each counter (a frame that says nothing keeps it). Main thread
// only: PrinterState feeds it from the status path.
//
// Subjects (registered as XML globals):
//   toolchange_wear_available   int, 1 once the printer has reported a counter
//   toolchange_wear_h<N>_pct    int, share of the service interval used, 0-100
//   toolchange_wear_h<N>_status int, toolchange_wear::Status
//   toolchange_wear_h<N>_text   string, "<changes since service> / <interval>"
// with N the head, 0-3.

#include "subject_managed_panel.h"
#include "toolchange_wear.h"

#include <array>
#include <optional>

namespace helix {

class PrinterDiscovery;

class ToolchangeWearState {
  public:
    ToolchangeWearState() = default;
    ToolchangeWearState(const ToolchangeWearState&) = delete;
    ToolchangeWearState& operator=(const ToolchangeWearState&) = delete;

    void init_subjects(bool register_xml = true);
    void deinit_subjects();

    /// A new printer: forget every counter and take its support.
    void set_hardware(const PrinterDiscovery& hw);

    /// Merge one status frame's news; unset fields keep the last value.
    void apply(const toolchange_wear::Reading& r);

    /// Changes since the last service, nullopt until both counters are known.
    [[nodiscard]] std::optional<int> since_service(int head) const;

    lv_subject_t* available_subject() {
        return &toolchange_wear_available_;
    }

  private:
    void publish();

    struct HeadSubjects {
        lv_subject_t pct{};
        lv_subject_t status{};
        lv_subject_t text{};
        char text_buf[32]{};
    };

    SubjectManager subjects_;
    bool subjects_initialized_ = false;
    bool supported_ = false;
    toolchange_wear::Reading last_{};
    lv_subject_t toolchange_wear_available_{};
    std::array<HeadSubjects, toolchange_wear::kHeads> heads_{};
};

} // namespace helix
