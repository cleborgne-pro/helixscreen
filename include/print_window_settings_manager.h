// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "lvgl/lvgl.h"
#include "persisted_setting.h"
#include "print_window.h"
#include "subject_managed_panel.h"

namespace helix {

/**
 * @brief Print hours settings (print_window.h), per printer.
 *
 * Subjects (XML): settings_print_window_enabled (0/1),
 * settings_print_window_stop and settings_print_window_resume (half-hour
 * slots, 0-47, the dropdowns' selected index).
 */
class PrintWindowSettingsManager {
  public:
    static PrintWindowSettingsManager& instance();

    static settings::TableView persisted_table();

    PrintWindowSettingsManager(const PrintWindowSettingsManager&) = delete;
    PrintWindowSettingsManager& operator=(const PrintWindowSettingsManager&) = delete;

    void init_subjects();
    void deinit_subjects();

    /// The window as set for the active printer.
    [[nodiscard]] print_window::Window window() const;

    bool get_enabled() const {
        return settings_.get_bool(Key::Enabled);
    }
    int get_stop_slot() const {
        return settings_.get(Key::StopSlot);
    }
    int get_resume_slot() const {
        return settings_.get(Key::ResumeSlot);
    }
    void set_enabled(bool enabled);
    void set_stop_slot(int slot);
    void set_resume_slot(int slot);

    lv_subject_t* subject_enabled() {
        return settings_.subject(Key::Enabled);
    }
    lv_subject_t* subject_stop_slot() {
        return settings_.subject(Key::StopSlot);
    }
    lv_subject_t* subject_resume_slot() {
        return settings_.subject(Key::ResumeSlot);
    }

  private:
    PrintWindowSettingsManager();
    ~PrintWindowSettingsManager() = default;

    enum class Key : uint8_t { Enabled, StopSlot, ResumeSlot, COUNT };

    SubjectManager subjects_;
    settings::PersistedSettings<Key, static_cast<size_t>(Key::COUNT)> settings_;
    bool subjects_initialized_ = false;
};

} // namespace helix
