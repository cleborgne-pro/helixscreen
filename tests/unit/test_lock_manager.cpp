// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "lock_manager.h"

#include "../catch_amalgamated.hpp"

#include <memory>

// [L065] Use friend class pattern instead of public test-only methods
class LockManagerTestAccess {
  public:
    static void reset(helix::LockManager& mgr) {
        mgr.pin_hash_.clear();
        mgr.locked_ = false;
        mgr.auto_lock_ = false;
        mgr.failed_attempts_ = 0;
        mgr.retry_at_ = {};
        mgr.now_ = [] { return helix::LockManager::Clock::now(); };
        mgr.save_to_config();
    }
    // Freezes the manager's clock at `now`, which the test then moves by hand.
    static void use_clock(helix::LockManager& mgr,
                          std::shared_ptr<helix::LockManager::Clock::time_point> now) {
        mgr.now_ = [now] { return *now; };
    }
    // What a restart sees: state rebuilt from the saved config.
    static void reload(helix::LockManager& mgr) {
        mgr.load_from_config();
    }
    static const std::string& pin_hash(const helix::LockManager& mgr) {
        return mgr.pin_hash_;
    }
};

TEST_CASE("LockManager: no PIN set by default", "[lock]") {
    auto& mgr = helix::LockManager::instance();
    LockManagerTestAccess::reset(mgr);

    CHECK_FALSE(mgr.has_pin());
    CHECK_FALSE(mgr.is_locked());
}

TEST_CASE("LockManager: set and verify PIN", "[lock]") {
    auto& mgr = helix::LockManager::instance();
    LockManagerTestAccess::reset(mgr);

    mgr.set_pin("1234");
    CHECK(mgr.has_pin());
    CHECK(mgr.verify_pin("1234"));
    CHECK_FALSE(mgr.verify_pin("0000"));
    CHECK_FALSE(mgr.verify_pin("12345"));
    CHECK_FALSE(mgr.verify_pin(""));
}

TEST_CASE("LockManager: the stored PIN hash is lowercase hex SHA-256", "[lock]") {
    // Hashes saved by existing installs must keep verifying.
    auto& mgr = helix::LockManager::instance();
    LockManagerTestAccess::reset(mgr);

    mgr.set_pin("1234");
    CHECK(LockManagerTestAccess::pin_hash(mgr) ==
          "03ac674216f3e15c761ee1a5e255f067953623c8b388b4459e13f978d7c846f4");
}

TEST_CASE("LockManager: lock and unlock", "[lock]") {
    auto& mgr = helix::LockManager::instance();
    LockManagerTestAccess::reset(mgr);
    mgr.set_pin("5678");

    CHECK_FALSE(mgr.is_locked());

    mgr.lock();
    CHECK(mgr.is_locked());

    CHECK_FALSE(mgr.try_unlock("0000"));
    CHECK(mgr.is_locked());

    CHECK(mgr.try_unlock("5678"));
    CHECK_FALSE(mgr.is_locked());
}

TEST_CASE("LockManager: lock without PIN does nothing", "[lock]") {
    auto& mgr = helix::LockManager::instance();
    LockManagerTestAccess::reset(mgr);

    mgr.lock();
    CHECK_FALSE(mgr.is_locked());
}

TEST_CASE("LockManager: remove PIN unlocks", "[lock]") {
    auto& mgr = helix::LockManager::instance();
    LockManagerTestAccess::reset(mgr);
    mgr.set_pin("1234");
    mgr.lock();
    CHECK(mgr.is_locked());

    mgr.remove_pin();
    CHECK_FALSE(mgr.has_pin());
    CHECK_FALSE(mgr.is_locked());
}

TEST_CASE("LockManager: PIN validation rejects invalid lengths", "[lock]") {
    auto& mgr = helix::LockManager::instance();
    LockManagerTestAccess::reset(mgr);

    CHECK_FALSE(mgr.set_pin("123"));     // too short
    CHECK_FALSE(mgr.set_pin("1234567")); // too long
    CHECK_FALSE(mgr.has_pin());

    CHECK_FALSE(mgr.set_pin("abcd")); // non-digit characters
    CHECK_FALSE(mgr.set_pin("12a4")); // mixed
    CHECK_FALSE(mgr.has_pin());

    CHECK(mgr.set_pin("1234")); // 4 digits OK
    mgr.remove_pin();
    CHECK(mgr.set_pin("12345")); // 5 digits OK
    mgr.remove_pin();
    CHECK(mgr.set_pin("123456")); // 6 digits OK
}

TEST_CASE("LockManager: auto-lock setting", "[lock]") {
    auto& mgr = helix::LockManager::instance();
    LockManagerTestAccess::reset(mgr);

    CHECK_FALSE(mgr.auto_lock_enabled());

    mgr.set_auto_lock(true);
    CHECK(mgr.auto_lock_enabled());

    mgr.set_auto_lock(false);
    CHECK_FALSE(mgr.auto_lock_enabled());
}

TEST_CASE("LockManager: lock is idempotent", "[lock]") {
    auto& mgr = helix::LockManager::instance();
    LockManagerTestAccess::reset(mgr);
    mgr.set_pin("1234");

    mgr.lock();
    mgr.lock(); // second call should be harmless
    CHECK(mgr.is_locked());

    CHECK(mgr.try_unlock("1234"));
    CHECK_FALSE(mgr.is_locked());
}

TEST_CASE("LockManager: set_pin while locked updates PIN", "[lock]") {
    auto& mgr = helix::LockManager::instance();
    LockManagerTestAccess::reset(mgr);
    mgr.set_pin("1234");
    mgr.lock();

    CHECK(mgr.set_pin("5678")); // change PIN while locked
    CHECK(mgr.is_locked());     // stays locked
    CHECK_FALSE(mgr.verify_pin("1234"));
    CHECK(mgr.verify_pin("5678"));
    CHECK(mgr.try_unlock("5678"));
}

TEST_CASE("LockManager: a locked screen stays locked across a restart", "[lock]") {
    auto& mgr = helix::LockManager::instance();
    LockManagerTestAccess::reset(mgr);
    mgr.set_pin("1234");
    mgr.lock();

    LockManagerTestAccess::reload(mgr);
    CHECK(mgr.is_locked());
}

TEST_CASE("LockManager: an unlocked screen restarts unlocked when auto-lock is off", "[lock]") {
    auto& mgr = helix::LockManager::instance();
    LockManagerTestAccess::reset(mgr);
    mgr.set_pin("1234");
    mgr.lock();
    REQUIRE(mgr.try_unlock("1234"));

    LockManagerTestAccess::reload(mgr);
    CHECK_FALSE(mgr.is_locked());
}

TEST_CASE("LockManager: auto-lock locks the screen at startup", "[lock]") {
    auto& mgr = helix::LockManager::instance();
    LockManagerTestAccess::reset(mgr);
    mgr.set_pin("1234");
    mgr.set_auto_lock(true);
    REQUIRE_FALSE(mgr.is_locked());

    LockManagerTestAccess::reload(mgr);
    CHECK(mgr.is_locked());
}

TEST_CASE("LockManager: without a PIN a restart never locks", "[lock]") {
    auto& mgr = helix::LockManager::instance();
    LockManagerTestAccess::reset(mgr);
    mgr.set_pin("1234");
    mgr.set_auto_lock(true);
    mgr.lock();
    mgr.remove_pin();

    LockManagerTestAccess::reload(mgr);
    CHECK_FALSE(mgr.is_locked());
}

namespace {

using LockClock = helix::LockManager::Clock;

std::shared_ptr<LockClock::time_point> frozen_clock(helix::LockManager& mgr) {
    auto now = std::make_shared<LockClock::time_point>(LockClock::now());
    LockManagerTestAccess::use_clock(mgr, now);
    return now;
}

void fail_times(helix::LockManager& mgr, int n) {
    for (int i = 0; i < n; ++i)
        REQUIRE_FALSE(mgr.try_unlock("0000"));
}

} // namespace

TEST_CASE("LockManager: the third wrong PIN in a row starts a 30 s wait", "[lock]") {
    auto& mgr = helix::LockManager::instance();
    LockManagerTestAccess::reset(mgr);
    auto now = frozen_clock(mgr);
    mgr.set_pin("1234");
    mgr.lock();

    fail_times(mgr, 2);
    CHECK(mgr.unlock_wait_seconds() == 0);

    fail_times(mgr, 1);
    CHECK(mgr.unlock_wait_seconds() == 30);

    // The right PIN is refused too while the wait runs.
    CHECK_FALSE(mgr.try_unlock("1234"));
    CHECK(mgr.is_locked());

    *now += std::chrono::seconds(30);
    CHECK(mgr.unlock_wait_seconds() == 0);
    CHECK(mgr.try_unlock("1234"));
    CHECK_FALSE(mgr.is_locked());
}

TEST_CASE("LockManager: a refused attempt during the wait is not counted", "[lock]") {
    auto& mgr = helix::LockManager::instance();
    LockManagerTestAccess::reset(mgr);
    auto now = frozen_clock(mgr);
    mgr.set_pin("1234");
    mgr.lock();

    fail_times(mgr, 3);
    *now += std::chrono::seconds(10);
    CHECK_FALSE(mgr.try_unlock("0000"));
    CHECK(mgr.unlock_wait_seconds() == 20);
}

TEST_CASE("LockManager: each further wrong PIN doubles the wait, up to 15 minutes", "[lock]") {
    auto& mgr = helix::LockManager::instance();
    LockManagerTestAccess::reset(mgr);
    auto now = frozen_clock(mgr);
    mgr.set_pin("1234");
    mgr.lock();

    fail_times(mgr, 3);
    int expected = 30;
    for (int round = 0; round < 8; ++round) {
        CHECK(mgr.unlock_wait_seconds() == expected);
        *now += std::chrono::seconds(mgr.unlock_wait_seconds());
        fail_times(mgr, 1);
        expected = std::min(expected * 2, 15 * 60);
    }
    CHECK(mgr.unlock_wait_seconds() == 15 * 60);
}

TEST_CASE("LockManager: the right PIN clears the wrong-PIN count", "[lock]") {
    auto& mgr = helix::LockManager::instance();
    LockManagerTestAccess::reset(mgr);
    frozen_clock(mgr);
    mgr.set_pin("1234");
    mgr.lock();

    fail_times(mgr, 2);
    REQUIRE(mgr.try_unlock("1234"));
    mgr.lock();
    fail_times(mgr, 2);
    CHECK(mgr.unlock_wait_seconds() == 0);
}

TEST_CASE("LockManager: a restart restarts the wait in full", "[lock]") {
    auto& mgr = helix::LockManager::instance();
    LockManagerTestAccess::reset(mgr);
    auto now = frozen_clock(mgr);
    mgr.set_pin("1234");
    mgr.lock();

    fail_times(mgr, 3);
    *now += std::chrono::seconds(25);
    REQUIRE(mgr.unlock_wait_seconds() == 5);

    LockManagerTestAccess::reload(mgr);
    CHECK(mgr.is_locked());
    CHECK(mgr.unlock_wait_seconds() == 30);
}
