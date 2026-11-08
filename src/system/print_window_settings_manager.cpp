// SPDX-License-Identifier: GPL-3.0-or-later

#include "print_window_settings_manager.h"

#include "core/static_subject_registry.h"
#include "printer_cache_registry.h"

#include <spdlog/spdlog.h>

#include <iterator>

using namespace helix;

using settings::Scope;
// Row order is PrintWindowSettingsManager::Key. Per printer: each printer has its
// own room and its own neighbours to keep quiet for.
static constexpr settings::PersistedSetting kPrintWindowSettings[] = {
    {"settings_print_window_enabled", "print_window/enabled", Scope::PerPrinter, true, 0, 0, 1,
     "print_window_enabled"},
    {"settings_print_window_stop", "print_window/stop_slot", Scope::PerPrinter, false,
     print_window::kDefaultStopSlot, 0, print_window::kSlotsPerDay - 1, "print_window_stop_slot"},
    {"settings_print_window_resume", "print_window/resume_slot", Scope::PerPrinter, false,
     print_window::kDefaultResumeSlot, 0, print_window::kSlotsPerDay - 1,
     "print_window_resume_slot"},
};

PrintWindowSettingsManager& PrintWindowSettingsManager::instance() {
    static PrintWindowSettingsManager instance;
    return instance;
}

settings::TableView PrintWindowSettingsManager::persisted_table() {
    return {kPrintWindowSettings, std::size(kPrintWindowSettings)};
}

PrintWindowSettingsManager::PrintWindowSettingsManager() : settings_(kPrintWindowSettings) {}

void PrintWindowSettingsManager::init_subjects() {
    if (subjects_initialized_) {
        return;
    }
    settings_.init(subjects_);
    subjects_initialized_ = true;
    PrinterCacheRegistry::instance().register_invalidator("PrintWindowSettingsManager", [] {
        auto& self = PrintWindowSettingsManager::instance();
        if (self.subjects_initialized_) {
            self.settings_.reload();
        }
    });
    StaticSubjectRegistry::instance().register_deinit("PrintWindowSettingsManager", []() {
        PrintWindowSettingsManager::instance().deinit_subjects();
    });
}

void PrintWindowSettingsManager::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }
    subjects_.deinit_all();
    subjects_initialized_ = false;
}

print_window::Window PrintWindowSettingsManager::window() const {
    return {get_enabled(), get_stop_slot(), get_resume_slot()};
}

void PrintWindowSettingsManager::set_enabled(bool enabled) {
    spdlog::info("[PrintWindow] Print hours {}", enabled ? "on" : "off");
    settings_.set(Key::Enabled, enabled);
}

void PrintWindowSettingsManager::set_stop_slot(int slot) {
    spdlog::info("[PrintWindow] Stop at {}", print_window::slot_label(slot));
    settings_.set(Key::StopSlot, slot);
}

void PrintWindowSettingsManager::set_resume_slot(int slot) {
    spdlog::info("[PrintWindow] Resume at {}", print_window::slot_label(slot));
    settings_.set(Key::ResumeSlot, slot);
}
