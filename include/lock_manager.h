// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <functional>
#include <string>

// Forward declaration for test access (defined in tests/unit/test_lock_manager.cpp)
class LockManagerTestAccess;

namespace helix {

/**
 * @brief Screen lock manager — PIN storage, lock state, auto-lock flag.
 *
 * PINs are stored as SHA-256 hashes via PicoSHA2 and persisted to Config,
 * together with the locked state so a restart comes back locked.
 * LVGL subjects (for UI binding) are initialised lazily in init_subjects()
 * and are NOT declared in this header so that the class compiles cleanly
 * in test contexts where LVGL is not fully available.
 *
 * Thread safety: main UI thread only.
 */
class LockManager {
  public:
    using Clock = std::chrono::steady_clock;

    static LockManager& instance();

    /** Returns true if a PIN hash is stored. */
    bool has_pin() const;

    /**
     * @brief Set a new PIN (4-6 digits).
     * @return true on success, false if length is out of range.
     */
    bool set_pin(const std::string& pin);

    /** Remove the stored PIN and immediately unlock. */
    void remove_pin();

    /** Verify a candidate PIN against the stored hash. */
    bool verify_pin(const std::string& pin) const;

    /** Returns true when the screen is currently locked. */
    bool is_locked() const;

    /**
     * @brief Lock the screen.
     * Does nothing if no PIN is set.
     */
    void lock();

    /**
     * @brief Attempt to unlock with a PIN.
     *
     * From the kFreeAttempts-th wrong PIN in a row, every wrong PIN starts a
     * wait that doubles each time; attempts during the wait are refused without
     * checking the PIN. The count survives a restart.
     *
     * @return true and clears locked state on success, false otherwise.
     */
    bool try_unlock(const std::string& pin);

    /** Seconds left before try_unlock() accepts another attempt (0 = now). */
    int unlock_wait_seconds() const;

    bool auto_lock_enabled() const;
    void set_auto_lock(bool enabled);

    /** Initialise LVGL subjects for UI binding (call after lv_init()). */
    void init_subjects();

  private:
    LockManager();

    // Allow test fixture to reset internal state without public test methods.
    friend class ::LockManagerTestAccess;

    std::string hash_pin(const std::string& pin) const;
    void start_wait_for_failures();
    void load_from_config();
    void save_to_config();

    std::string pin_hash_;
    bool locked_ = false;
    bool auto_lock_ = false;
    bool subjects_initialized_ = false;
    int failed_attempts_ = 0;
    Clock::time_point retry_at_{};
    std::function<Clock::time_point()> now_ = [] { return Clock::now(); };

    static constexpr int kMinPinLength = 4;
    static constexpr int kMaxPinLength = 6;
    static constexpr int kFreeAttempts = 3;
    static constexpr int kFirstWaitSec = 30;
    static constexpr int kMaxWaitSec = 15 * 60;
};

} // namespace helix
