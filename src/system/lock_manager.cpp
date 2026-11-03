// SPDX-License-Identifier: GPL-3.0-or-later
#include "lock_manager.h"

#include "config.h"
#include "static_subject_registry.h"
#include "subject_managed_panel.h"
#include "system/sha256_util.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <lvgl.h>

// Module-level subject storage — kept out of the header so lock_manager.h
// compiles cleanly in test contexts where LVGL is not fully available.
static lv_subject_t s_pin_set_subject;
static bool s_subjects_initialized = false;
static SubjectManager s_subjects;

// Values behind the Security overlay's "Lock after" dropdown, in its order.
static constexpr std::array<int, 5> kAutoLockAfterOptions = {0, 30, 60, 120, 300};

namespace helix {

LockManager& LockManager::instance() {
    static LockManager inst;
    return inst;
}

LockManager::LockManager() {
    load_from_config();
}

bool LockManager::has_pin() const {
    return !pin_hash_.empty();
}

bool LockManager::set_pin(const std::string& pin) {
    if (static_cast<int>(pin.length()) < kMinPinLength ||
        static_cast<int>(pin.length()) > kMaxPinLength) {
        return false;
    }
    if (!std::all_of(pin.begin(), pin.end(), ::isdigit)) {
        return false;
    }
    pin_hash_ = hash_pin(pin);
    save_to_config();
    spdlog::info("[LockManager] PIN set");
    if (s_subjects_initialized) {
        lv_subject_set_int(&s_pin_set_subject, 1);
    }
    return true;
}

void LockManager::remove_pin() {
    pin_hash_.clear();
    locked_ = false;
    failed_attempts_ = 0;
    retry_at_ = {};
    save_to_config();
    spdlog::info("[LockManager] PIN removed, lock disabled");
    if (s_subjects_initialized) {
        lv_subject_set_int(&s_pin_set_subject, 0);
    }
}

bool LockManager::verify_pin(const std::string& pin) const {
    if (pin_hash_.empty() || pin.empty())
        return false;
    return hash_pin(pin) == pin_hash_;
}

bool LockManager::is_locked() const {
    return locked_;
}

void LockManager::lock() {
    if (!has_pin())
        return;
    if (!locked_) {
        locked_ = true;
        save_to_config();
    }
    spdlog::info("[LockManager] Screen locked");
}

bool LockManager::try_unlock(const std::string& pin) {
    if (unlock_wait_seconds() > 0) {
        spdlog::debug("[LockManager] Unlock refused — waiting after repeated wrong PINs");
        return false;
    }
    if (verify_pin(pin)) {
        locked_ = false;
        failed_attempts_ = 0;
        save_to_config();
        spdlog::info("[LockManager] Screen unlocked");
        return true;
    }
    ++failed_attempts_;
    start_wait_for_failures();
    save_to_config();
    spdlog::info("[LockManager] Unlock failed — wrong PIN ({} in a row)", failed_attempts_);
    return false;
}

int LockManager::unlock_wait_seconds() const {
    const auto left = retry_at_ - now_();
    if (left <= Clock::duration::zero())
        return 0;
    // Round up so the countdown never shows 0 while attempts are still refused.
    return static_cast<int>(std::chrono::ceil<std::chrono::seconds>(left).count());
}

void LockManager::start_wait_for_failures() {
    const int over = failed_attempts_ - kFreeAttempts;
    if (over < 0) {
        retry_at_ = {};
        return;
    }
    // 30 s, 60 s, 120 s ... capped: a 4-digit PIN cannot be walked by hand.
    int wait_sec = kFirstWaitSec;
    for (int i = 0; i < over && wait_sec < kMaxWaitSec; ++i)
        wait_sec *= 2;
    wait_sec = std::min(wait_sec, kMaxWaitSec);
    retry_at_ = now_() + std::chrono::seconds(wait_sec);
}

bool LockManager::auto_lock_enabled() const {
    return auto_lock_;
}

void LockManager::set_auto_lock(bool enabled) {
    auto_lock_ = enabled;
    save_to_config();
}

int LockManager::auto_lock_after_sec() const {
    return auto_lock_after_sec_;
}

void LockManager::set_auto_lock_after_sec(int seconds) {
    if (std::find(kAutoLockAfterOptions.begin(), kAutoLockAfterOptions.end(), seconds) ==
        kAutoLockAfterOptions.end()) {
        spdlog::warn("[LockManager] Ignoring unsupported auto-lock delay {}s", seconds);
        return;
    }
    auto_lock_after_sec_ = seconds;
    save_to_config();
}

int LockManager::auto_lock_after_index(int seconds) {
    const auto it = std::find(kAutoLockAfterOptions.begin(), kAutoLockAfterOptions.end(), seconds);
    return it == kAutoLockAfterOptions.end() ? 0
                                             : static_cast<int>(it - kAutoLockAfterOptions.begin());
}

int LockManager::auto_lock_after_sec_at(int index) {
    if (index < 0 || index >= static_cast<int>(kAutoLockAfterOptions.size()))
        return 0;
    return kAutoLockAfterOptions[static_cast<size_t>(index)];
}

std::string LockManager::hash_pin(const std::string& pin) const {
    return sha256_hex(pin);
}

void LockManager::load_from_config() {
    auto* config = Config::get_instance();
    pin_hash_ = config->get<std::string>("/security/pin_hash", "");
    auto_lock_ = config->get<bool>("/security/auto_lock", false);
    auto_lock_after_sec_ = auto_lock_after_sec_at(
        auto_lock_after_index(config->get<int>("/security/auto_lock_after_sec", 0)));
    // A restart must not hand over an unlocked screen: power-cycling the printer
    // would otherwise be the way past the PIN. Auto-lock treats the restart as idle time.
    locked_ = has_pin() && (config->get<bool>("/security/locked", false) || auto_lock_);
    // The wait restarts in full: rebooting must not be a shortcut past it.
    failed_attempts_ = std::max(0, config->get<int>("/security/failed_attempts", 0));
    start_wait_for_failures();
}

void LockManager::save_to_config() {
    auto* config = Config::get_instance();
    config->set<std::string>("/security/pin_hash", pin_hash_);
    config->set<bool>("/security/auto_lock", auto_lock_);
    config->set<int>("/security/auto_lock_after_sec", auto_lock_after_sec_);
    config->set<bool>("/security/locked", locked_);
    config->set<int>("/security/failed_attempts", failed_attempts_);
    config->save();
}

void LockManager::init_subjects() {
    if (s_subjects_initialized)
        return;

    lv_subject_init_int(&s_pin_set_subject, has_pin() ? 1 : 0);
    s_subjects.publish("lock_pin_set", &s_pin_set_subject);

    s_subjects_initialized = true;
    subjects_initialized_ = true;

    // Self-register cleanup — co-located with init to prevent forgotten registrations.
    // Runs before lv_deinit() in StaticSubjectRegistry::deinit_all().
    StaticSubjectRegistry::instance().register_deinit("LockManager", []() {
        if (s_subjects_initialized && lv_is_initialized()) {
            s_subjects.deinit_all();
            s_subjects_initialized = false;
            spdlog::trace("[LockManager] Subjects deinitialized");
        }
        LockManager::instance().subjects_initialized_ = false;
    });

    spdlog::debug("[LockManager] Subjects initialized (pin_set={})", has_pin() ? 1 : 0);
}

} // namespace helix
