#include "scheduler.h"

#include "application.h"
#include "esphome/core/defines.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/progmem.h"
#include <algorithm>
#include <cinttypes>
#include <cstring>
<<<<<<< HEAD
=======
#include <limits>
#include <cstdint>
    >>>>>>> 19a82d45b(fix schedule issue wild pointer(not tested))

                namespace esphome {

  static const char *const TAG = "scheduler";

  // Maximum number of logically deleted (cancelled) items before forcing cleanup.
  // Empirically chosen to balance cleanup overhead against tombstone accumulation in items_.
  static constexpr uint32_t MAX_LOGICALLY_DELETED_ITEMS = 5;
  // max delay to start an interval sequence
  static constexpr uint32_t MAX_INTERVAL_DELAY = 5000;

#if defined(ESPHOME_LOG_HAS_VERBOSE) || defined(ESPHOME_DEBUG_SCHEDULER)
  // Helper struct for formatting scheduler item names consistently in logs
  // Uses a stack buffer to avoid heap allocation
  // Uses ESPHOME_snprintf_P/ESPHOME_PSTR for ESP8266 to keep format strings in flash
  struct SchedulerNameLog {
    // Sized for the widest formatted output: "self:0x" + 16 hex digits (64-bit pointer) + nul.
    // Also covers "id:4294967295", "hash:0xFFFFFFFF", "iid:4294967295", "(null)".
    char buffer[28];

    // Format a scheduler item name for logging
    // Returns pointer to formatted string (either static_name or internal buffer)
    const char *format(Scheduler::NameType name_type, const char *static_name, uint32_t hash_or_id) {
      using NameType = Scheduler::NameType;
      if (name_type == NameType::STATIC_STRING) {
        if (static_name)
          return static_name;
        // Copy "(null)" to buffer to keep it in flash on ESP8266
        ESPHOME_strncpy_P(buffer, ESPHOME_PSTR("(null)"), sizeof(buffer));
        return buffer;
      } else if (name_type == NameType::HASHED_STRING) {
        ESPHOME_snprintf_P(buffer, sizeof(buffer), ESPHOME_PSTR("hash:0x%08" PRIX32), hash_or_id);
        return buffer;
      } else if (name_type == NameType::NUMERIC_ID) {
        ESPHOME_snprintf_P(buffer, sizeof(buffer), ESPHOME_PSTR("id:%" PRIu32), hash_or_id);
        return buffer;
      } else if (name_type == NameType::NUMERIC_ID_INTERNAL) {
        ESPHOME_snprintf_P(buffer, sizeof(buffer), ESPHOME_PSTR("iid:%" PRIu32), hash_or_id);
        return buffer;
      } else {  // SELF_POINTER
        // static_name carries the void* key for SELF_POINTER (pointer-width union slot).
        // %p is specified as void* (not const void*), so strip const for the varargs call.
        ESPHOME_snprintf_P(buffer, sizeof(buffer), ESPHOME_PSTR("self:%p"),
                           const_cast<void *>(static_cast<const void *>(static_name)));
        return buffer;
      }
    }
  };
#endif

  // Uncomment to debug scheduler
  // #define ESPHOME_DEBUG_SCHEDULER
  // ====== config ======
  static constexpr size_t MAX_POOL_SIZE = 5;
  static constexpr uint32_t MAX_LOGICALLY_DELETED_ITEMS = 5;
  static constexpr uint32_t HALF_MAX_UINT32 = std::numeric_limits<uint32_t>::max() / 2;
  static constexpr uint32_t MAX_INTERVAL_DELAY = 5000;

  // once this is true, scheduler becomes a no-op for the rest of the boot
  static bool s_scheduler_panic = false;

  // quick sanity check for corrupted / poisoned pointers BEFORE we deref / delete
  static bool is_suspicious_scheduler_ptr_(const void *p) {
    if (p == nullptr)
      return true;

    uintptr_t addr = reinterpret_cast<uintptr_t>(p);

    // must be 4-byte aligned
    if ((addr & 0x3u) != 0)
      return true;

    // common ESP8266 poison pattern: 0xA5A5A5xx
    if ((addr & 0xFFFFFF00u) == 0xA5A5A500u)
      return true;

#if defined(ARDUINO_ARCH_ESP8266) || defined(ESP8266)
    // rough DRAM window, keep it loose
    if (addr < 0x3FFE0000u || addr > 0x40100000u)
      return true;
#endif

    return false;
  }

#ifdef ESPHOME_DEBUG_SCHEDULER
  static void validate_static_string(const char *name) {
    if (name == nullptr)
      return;
    uintptr_t addr = reinterpret_cast<uintptr_t>(name);
    int stack_var;
    uintptr_t stack_addr = reinterpret_cast<uintptr_t>(&stack_var);
    if (addr > (stack_addr - 0x2000) && addr < (stack_addr + 0x2000)) {
      ESP_LOGW(TAG,
               "WARNING: Scheduler name '%s' at %p appears to be on the stack - this is unsafe!\n"
               "         Stack reference at %p",
               name, name, &stack_var);
    }
    static const char *static_str = "test";
    uintptr_t static_addr = reinterpret_cast<uintptr_t>(static_str);
    if (addr > static_addr + 0x100000 || (static_addr > 0x100000 && addr < static_addr - 0x100000)) {
      ESP_LOGW(TAG, "WARNING: Scheduler name '%s' at %p might be on heap (static ref at %p)", name, name, static_str);
    }
  }
#endif

  // ========== public API impl ==========

<<<<<<< HEAD
  // Calculate random offset for interval timers
  // Extracted from set_timer_common_ to reduce code size - only needed for intervals, not timeouts
  uint32_t Scheduler::calculate_interval_offset_(uint32_t delay) {
    uint32_t max_offset = std::min(delay / 2, MAX_INTERVAL_DELAY);
    // Multiply-and-shift: uniform random in [0, max_offset) without floating point
    return static_cast<uint32_t>((static_cast<uint64_t>(random_uint32()) * max_offset) >> 32);
  }
=======
  // Common implementation for both timeout and interval
  void HOT Scheduler::set_timer_common_(Component * component, SchedulerItem::Type type, bool is_static_string,
                                        const void *name_ptr, uint32_t delay, std::function<void()> func, bool is_retry,
                                        bool skip_cancel) {
    if (s_scheduler_panic) {
      // scheduler is quarantined
      return;
    }

    const char *name_cstr = this->get_name_cstr_(is_static_string, name_ptr);
>>>>>>> 19a82d45b (fix schedule issue wild pointer (not tested))

  // Check if a retry was already cancelled in items_ or to_add_
  // Extracted from set_timer_common_ to reduce code size - retry path is cold and deprecated
  // Remove before 2026.8.0 along with all retry code
  bool Scheduler::is_retry_cancelled_locked_(Component * component, NameType name_type, const char *static_name,
                                             uint32_t hash_or_id) {
    for (auto *container : {&this->items_, &this->to_add_}) {
      for (auto *item : *container) {
        if (item != nullptr && this->is_item_removed_locked_(item) &&
            this->matches_item_locked_(item, component, name_type, static_name, hash_or_id, SchedulerItem::TIMEOUT,
                                       /* match_retry= */ true, /* skip_removed= */ false)) {
          return true;
        }
      }
    }
    return false;
  }

  // Common implementation for both timeout and interval
  // name_type determines storage type: STATIC_STRING uses static_name, others use hash_or_id
  void HOT Scheduler::set_timer_common_(Component * component, SchedulerItem::Type type, NameType name_type,
                                        const char *static_name, uint32_t hash_or_id, uint32_t delay,
                                        std::function<void()> &&func, bool is_retry, bool skip_cancel) {
    if (delay == SCHEDULER_DONT_RUN) {
<<<<<<< HEAD
      // Still need to cancel existing timer if we have a name/id
=======
>>>>>>> 19a82d45b (fix schedule issue wild pointer (not tested))
      if (!skip_cancel) {
        LockGuard guard{this->lock_};
        this->cancel_item_locked_(component, name_type, static_name, hash_or_id, type, /* match_retry= */ false,
                                  /* find_first= */ true);
      }
      return;
    }

<<<<<<< HEAD
    // An interval of 0 means "fire every tick forever," which is misuse: the
    // item would always be due, causing Scheduler::call() to spin and starve
    // the main loop (WDT reset in the field). Coerce to 1ms so existing code
    // using update_interval=0ms as a pseudo-loop() continues to work at ~1kHz,
    // and warn so authors can migrate to HighFrequencyLoopRequester which is
    // the intended mechanism for running fast in the main loop. Zero-delay
    // timeouts (defer) remain legitimate one-shots and are not affected.
    if (type == SchedulerItem::INTERVAL && delay == 0) [[unlikely]] {
      ESP_LOGE(TAG, "[%s] set_interval(0) would spin main loop - coercing to 1ms (use HighFrequencyLoopRequester)",
               component ? LOG_STR_ARG(component->get_component_log_str()) : LOG_STR_LITERAL("?"));
      delay = 1;
    }

    // Take lock early to protect scheduler_item_pool_head_ access and retry-cancelled check
    LockGuard guard{this->lock_};

    // For retries, check if there's a cancelled timeout first - before allocating an item.
    // Skip check for anonymous retries (STATIC_STRING with nullptr) - they can't be cancelled by name
    // Skip check for defer (delay=0) - deferred retries bypass the cancellation check
    if (is_retry && delay != 0 && (name_type != NameType::STATIC_STRING || static_name != nullptr) &&
        type == SchedulerItem::TIMEOUT &&
        this->is_retry_cancelled_locked_(component, name_type, static_name, hash_or_id)) {
#ifdef ESPHOME_DEBUG_SCHEDULER
      SchedulerNameLog skip_name_log;
      ESP_LOGD(TAG, "Skipping retry '%s' - found cancelled item",
               skip_name_log.format(name_type, static_name, hash_or_id));
#endif
      return;
    }

    // Create and populate the scheduler item
    SchedulerItem *item = this->get_item_from_pool_locked_();
=======
      const uint64_t now = this->millis_64_(millis());

      LockGuard guard{this->lock_};

      std::unique_ptr<SchedulerItem> item;
      if (!this->scheduler_item_pool_.empty()) {
        item = std::move(this->scheduler_item_pool_.back());
        this->scheduler_item_pool_.pop_back();
      } else {
        item = make_unique<SchedulerItem>();
      }

>>>>>>> 19a82d45b (fix schedule issue wild pointer (not tested))
    item->component = component;
    item->set_name(name_type, static_name, hash_or_id);
    item->type = type;
<<<<<<< HEAD
    // Use destroy + placement-new instead of move-assignment.
    // GCC's std::function::operator=(function&&) does a full swap dance even when the
    // target is empty. Since recycled/new items always have an empty callback, we can
    // destroy the empty one (no-op) and move-construct directly, saving ~40 bytes of
    // swap/destructor code on Xtensa.
    item->callback.~function();
    new (&item->callback) std::function<void()>(std::move(func));
    // Reset remove flag - recycled items may have been cancelled (remove=true) in previous use
    this->set_item_removed_(item, false);
=======
      item->callback = std::move(func);
#ifdef ESPHOME_THREAD_MULTI_ATOMICS
      item->remove.store(false, std::memory_order_relaxed);
#else
      item->remove = false;
#endif
      >>>>>>> 19a82d45b(fix schedule issue wild pointer(not tested)) item->is_retry = is_retry;

      // Determine target container: defer_queue_ for deferred items, to_add_ for everything else.
      // Using a pointer lets both paths share the cancel + push_back epilogue.
      auto *target = &this->to_add_;

#ifndef ESPHOME_THREAD_SINGLE
      if (delay == 0 && type == SchedulerItem::TIMEOUT) {
<<<<<<< HEAD
        // Put in defer queue for guaranteed FIFO execution
        target = &this->defer_queue_;
      } else
#endif /* not ESPHOME_THREAD_SINGLE */
      {
        // Only non-defer items need a timestamp for scheduling
        const uint64_t now_64 = millis_64();

        // Type-specific setup
        if (type == SchedulerItem::INTERVAL) {
          item->interval = delay;
          // first execution happens immediately after a random smallish offset
          uint32_t offset = this->calculate_interval_offset_(delay);
          item->set_next_execution(now_64 + offset);
#ifdef ESPHOME_LOG_HAS_VERBOSE
          SchedulerNameLog name_log;
          ESP_LOGV(TAG, "Scheduler interval for %s is %" PRIu32 "ms, offset %" PRIu32 "ms",
                   name_log.format(name_type, static_name, hash_or_id), delay, offset);
#endif
        } else {
          item->interval = 0;
          item->set_next_execution(now_64 + delay);
        }

#ifdef ESPHOME_DEBUG_SCHEDULER
        this->debug_log_timer_(item, name_type, static_name, hash_or_id, type, delay, now_64);
#endif /* ESPHOME_DEBUG_SCHEDULER */
      }

      // Common epilogue: atomic cancel-and-add (unless skip_cancel is true or anonymous)
      // Anonymous items (STATIC_STRING with nullptr) can never match anything, so skip the scan.
      if (!skip_cancel && (name_type != NameType::STATIC_STRING || static_name != nullptr)) {
        this->cancel_item_locked_(component, name_type, static_name, hash_or_id, type, /* match_retry= */ false,
                                  /* find_first= */ true);
      }
      target->push_back(item);
      if (target == &this->to_add_) {
        this->to_add_count_increment_locked_();
      }
#ifndef ESPHOME_THREAD_SINGLE
      else {
        this->defer_count_increment_locked_();
      }
#endif
      == == == = if (!skip_cancel) { this->cancel_item_locked_(component, name_cstr, type); }
      this->defer_queue_.push_back(std::move(item));
      return;
    }
#endif

    if (type == SchedulerItem::INTERVAL) {
      item->interval = delay;
      uint32_t offset = (uint32_t) (std::min(delay / 2, MAX_INTERVAL_DELAY) * random_float());
      item->set_next_execution(now + offset);
    } else {
      item->interval = 0;
      item->set_next_execution(now + delay);
    }

#ifdef ESPHOME_DEBUG_SCHEDULER
    if (is_static_string && name_cstr != nullptr) {
      validate_static_string(name_cstr);
    }
#endif

    if (is_retry && name_cstr != nullptr && type == SchedulerItem::TIMEOUT &&
        (has_cancelled_timeout_in_container_(this->items_, component, name_cstr, /*match_retry=*/true) ||
         has_cancelled_timeout_in_container_(this->to_add_, component, name_cstr, /*match_retry=*/true))) {
      return;
    }

    if (!skip_cancel) {
      this->cancel_item_locked_(component, name_cstr, type);
    }

    this->to_add_.push_back(std::move(item));
>>>>>>> 19a82d45b (fix schedule issue wild pointer (not tested))
  }

  void HOT Scheduler::set_timeout(Component * component, const char *name, uint32_t timeout,
                                  std::function<void()> &&func) {
    this->set_timer_common_(component, SchedulerItem::TIMEOUT, NameType::STATIC_STRING, name, 0, timeout,
                            std::move(func));
  }
  void HOT Scheduler::set_timeout(Component * component, const std::string &name, uint32_t timeout,
                                  std::function<void()> &&func) {
    this->set_timer_common_(component, SchedulerItem::TIMEOUT, NameType::HASHED_STRING, nullptr, fnv1a_hash(name),
                            timeout, std::move(func));
  }
  void HOT Scheduler::set_timeout(Component * component, uint32_t id, uint32_t timeout, std::function<void()> &&func) {
    this->set_timer_common_(component, SchedulerItem::TIMEOUT, NameType::NUMERIC_ID, nullptr, id, timeout,
                            std::move(func));
  }
  bool HOT Scheduler::cancel_timeout(Component * component, const std::string &name) {
    return this->cancel_item_(component, NameType::HASHED_STRING, nullptr, fnv1a_hash(name), SchedulerItem::TIMEOUT);
  }
  bool HOT Scheduler::cancel_timeout(Component * component, const char *name) {
    return this->cancel_item_(component, NameType::STATIC_STRING, name, 0, SchedulerItem::TIMEOUT);
  }
  bool HOT Scheduler::cancel_timeout(Component * component, uint32_t id) {
    return this->cancel_item_(component, NameType::NUMERIC_ID, nullptr, id, SchedulerItem::TIMEOUT);
  }
  void HOT Scheduler::set_interval(Component * component, const std::string &name, uint32_t interval,
                                   std::function<void()> &&func) {
    this->set_timer_common_(component, SchedulerItem::INTERVAL, NameType::HASHED_STRING, nullptr, fnv1a_hash(name),
                            interval, std::move(func));
  }
  void HOT Scheduler::set_interval(Component * component, const char *name, uint32_t interval,
                                   std::function<void()> &&func) {
    this->set_timer_common_(component, SchedulerItem::INTERVAL, NameType::STATIC_STRING, name, 0, interval,
                            std::move(func));
  }
  void HOT Scheduler::set_interval(Component * component, uint32_t id, uint32_t interval,
                                   std::function<void()> &&func) {
    this->set_timer_common_(component, SchedulerItem::INTERVAL, NameType::NUMERIC_ID, nullptr, id, interval,
                            std::move(func));
  }
  bool HOT Scheduler::cancel_interval(Component * component, const std::string &name) {
    return this->cancel_item_(component, NameType::HASHED_STRING, nullptr, fnv1a_hash(name), SchedulerItem::INTERVAL);
  }
  bool HOT Scheduler::cancel_interval(Component * component, const char *name) {
    return this->cancel_item_(component, NameType::STATIC_STRING, name, 0, SchedulerItem::INTERVAL);
  }
  bool HOT Scheduler::cancel_interval(Component * component, uint32_t id) {
    return this->cancel_item_(component, NameType::NUMERIC_ID, nullptr, id, SchedulerItem::INTERVAL);
  }

<<<<<<< HEAD
  // Self-keyed scheduler API. The cancellation key is `self` (typically the caller's `this`),
  // passed through the existing static_name pointer slot. Matching is by raw pointer equality
  // (see matches_item_locked_'s SELF_POINTER branch). No Component pointer is stored, so
  // is_failed() skip and component-based log attribution don't apply.
  void HOT Scheduler::set_timeout(const void *self, uint32_t timeout, std::function<void()> &&func) {
    this->set_timer_common_(nullptr, SchedulerItem::TIMEOUT, NameType::SELF_POINTER, static_cast<const char *>(self), 0,
                            timeout, std::move(func));
  }
  void HOT Scheduler::set_interval(const void *self, uint32_t interval, std::function<void()> &&func) {
    this->set_timer_common_(nullptr, SchedulerItem::INTERVAL, NameType::SELF_POINTER, static_cast<const char *>(self),
                            0, interval, std::move(func));
  }
  bool HOT Scheduler::cancel_timeout(const void *self) {
    return this->cancel_item_(nullptr, NameType::SELF_POINTER, static_cast<const char *>(self), 0,
                              SchedulerItem::TIMEOUT);
  }
  bool HOT Scheduler::cancel_interval(const void *self) {
    return this->cancel_item_(nullptr, NameType::SELF_POINTER, static_cast<const char *>(self), 0,
                              SchedulerItem::INTERVAL);
  }

// Suppress deprecation warnings for RetryResult usage in the still-present (but deprecated) retry implementation.
// Remove before 2026.8.0 along with all retry code.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  == == == =
               // ===== retry support =====
>>>>>>> 19a82d45b (fix schedule issue wild pointer (not tested))

      struct RetryArgs {
    // Ordered to minimize padding on 32-bit systems
    std::function<RetryResult(uint8_t)> func;
    Component *component;
<<<<<<< HEAD
=======
    std::string name;
    float backoff_increase_factor;
>>>>>>> 19a82d45b (fix schedule issue wild pointer (not tested))
    Scheduler *scheduler;
    // Union for name storage - only one is used based on name_type
    union {
      const char *static_name;  // For STATIC_STRING
      uint32_t hash_or_id;      // For HASHED_STRING or NUMERIC_ID
    } name_;
    uint32_t current_interval;
    float backoff_increase_factor;
    Scheduler::NameType name_type;  // Discriminator for name_ union
    uint8_t retry_countdown;
  };

  // must stay non-static because scheduler.h friends it
  void retry_handler(const std::shared_ptr<RetryArgs> &args) {
    RetryResult const retry_result = args->func(--args->retry_countdown);
    if (retry_result == RetryResult::DONE || args->retry_countdown <= 0)
      return;
<<<<<<< HEAD
    // second execution of `func` happens after `initial_wait_time`
    // args->name_ is owned by the shared_ptr<RetryArgs>
    // which is captured in the lambda and outlives the SchedulerItem
    const char *static_name =
        (args->name_type == Scheduler::NameType::STATIC_STRING) ? args->name_.static_name : nullptr;
    uint32_t hash_or_id = (args->name_type != Scheduler::NameType::STATIC_STRING) ? args->name_.hash_or_id : 0;
    args->scheduler->set_timer_common_(
        args->component, Scheduler::SchedulerItem::TIMEOUT, args->name_type, static_name, hash_or_id,
        args->current_interval, [args]() { retry_handler(args); },
        /* is_retry= */ true);
    // backoff_increase_factor applied to third & later executions
=======
    args->scheduler->set_timer_common_(
        args->component, Scheduler::SchedulerItem::TIMEOUT, false, &args->name, args->current_interval,
        [args]() { retry_handler(args); }, /*is_retry=*/true);
>>>>>>> 19a82d45b (fix schedule issue wild pointer (not tested))
    args->current_interval *= args->backoff_increase_factor;
  }

  void HOT Scheduler::set_retry_common_(Component * component, NameType name_type, const char *static_name,
                                        uint32_t hash_or_id, uint32_t initial_wait_time, uint8_t max_attempts,
                                        std::function<RetryResult(uint8_t)> func, float backoff_increase_factor) {
<<<<<<< HEAD
    this->cancel_retry_(component, name_type, static_name, hash_or_id);
=======
    if (s_scheduler_panic)
      return;

    const char *name_cstr = this->get_name_cstr_(is_static_string, name_ptr);

    if (name_cstr != nullptr)
      this->cancel_retry(component, name_cstr);
>>>>>>> 19a82d45b (fix schedule issue wild pointer (not tested))

    if (initial_wait_time == SCHEDULER_DONT_RUN)
      return;

<<<<<<< HEAD
#ifdef ESPHOME_LOG_HAS_VERY_VERBOSE
    {
      SchedulerNameLog name_log;
      ESP_LOGVV(TAG, "set_retry(name='%s', initial_wait_time=%" PRIu32 ", max_attempts=%u, backoff_factor=%0.1f)",
                name_log.format(name_type, static_name, hash_or_id), initial_wait_time, max_attempts,
                backoff_increase_factor);
    }
#endif

    if (backoff_increase_factor < 0.0001) {
      ESP_LOGE(TAG, "set_retry: backoff_factor %0.1f too small, using 1.0: %s", backoff_increase_factor,
               (name_type == NameType::STATIC_STRING && static_name) ? static_name : "");
      backoff_increase_factor = 1;
=======
    if (backoff_increase_factor < 0.0001f) {
      backoff_increase_factor = 1.0f;
>>>>>>> 19a82d45b (fix schedule issue wild pointer (not tested))
    }

    auto args = std::make_shared<RetryArgs>();
    args->func = std::move(func);
    args->component = component;
<<<<<<< HEAD
=======
    args->name = name_cstr ? name_cstr : "";
    args->backoff_increase_factor = backoff_increase_factor;
>>>>>>> 19a82d45b (fix schedule issue wild pointer (not tested))
    args->scheduler = this;
    args->name_type = name_type;
    if (name_type == NameType::STATIC_STRING) {
      args->name_.static_name = static_name;
    } else {
      args->name_.hash_or_id = hash_or_id;
    }
    args->current_interval = initial_wait_time;
    args->backoff_increase_factor = backoff_increase_factor;
    args->retry_countdown = max_attempts;

    this->set_timer_common_(
<<<<<<< HEAD
        component, SchedulerItem::TIMEOUT, name_type, static_name, hash_or_id, 0, [args]() { retry_handler(args); },
        /* is_retry= */ true);
=======
        component, SchedulerItem::TIMEOUT, false, &args->name, 0, [args]() { retry_handler(args); }, /*is_retry=*/true);
>>>>>>> 19a82d45b (fix schedule issue wild pointer (not tested))
  }

  void HOT Scheduler::set_retry(Component * component, const char *name, uint32_t initial_wait_time,
                                uint8_t max_attempts, std::function<RetryResult(uint8_t)> func,
                                float backoff_increase_factor) {
    this->set_retry_common_(component, NameType::STATIC_STRING, name, 0, initial_wait_time, max_attempts,
                            std::move(func), backoff_increase_factor);
  }

  bool HOT Scheduler::cancel_retry_(Component * component, NameType name_type, const char *static_name,
                                    uint32_t hash_or_id) {
    return this->cancel_item_(component, name_type, static_name, hash_or_id, SchedulerItem::TIMEOUT,
                              /* match_retry= */ true);
  }
  bool HOT Scheduler::cancel_retry(Component * component, const char *name) {
    return this->cancel_retry_(component, NameType::STATIC_STRING, name, 0);
  }

  void HOT Scheduler::set_retry(Component * component, const std::string &name, uint32_t initial_wait_time,
                                uint8_t max_attempts, std::function<RetryResult(uint8_t)> func,
                                float backoff_increase_factor) {
    this->set_retry_common_(component, NameType::HASHED_STRING, nullptr, fnv1a_hash(name), initial_wait_time,
                            max_attempts, std::move(func), backoff_increase_factor);
  }
<<<<<<< HEAD

=======
  void HOT Scheduler::set_retry(Component * component, const char *name, uint32_t initial_wait_time,
                                uint8_t max_attempts, std::function<RetryResult(uint8_t)> func,
                                float backoff_increase_factor) {
    this->set_retry_common_(component, true, name, initial_wait_time, max_attempts, std::move(func),
                            backoff_increase_factor);
  }
>>>>>>> 19a82d45b (fix schedule issue wild pointer (not tested))
  bool HOT Scheduler::cancel_retry(Component * component, const std::string &name) {
    return this->cancel_retry_(component, NameType::HASHED_STRING, nullptr, fnv1a_hash(name));
  }
<<<<<<< HEAD

  void HOT Scheduler::set_retry(Component * component, uint32_t id, uint32_t initial_wait_time, uint8_t max_attempts,
                                std::function<RetryResult(uint8_t)> func, float backoff_increase_factor) {
    this->set_retry_common_(component, NameType::NUMERIC_ID, nullptr, id, initial_wait_time, max_attempts,
                            std::move(func), backoff_increase_factor);
  }

  bool HOT Scheduler::cancel_retry(Component * component, uint32_t id) {
    return this->cancel_retry_(component, NameType::NUMERIC_ID, nullptr, id);
  }

#pragma GCC diagnostic pop  // End suppression of deprecated RetryResult warnings

  optional<uint32_t> HOT Scheduler::next_schedule_in(uint32_t now) {
    // IMPORTANT: This method should only be called from the main thread (loop task).
    // Accesses items_[0] and the fast-path empty checks without holding a lock, which
    // is only safe from the main thread. Other threads must not call this method.
    //
    // Note: cleanup_() is only invoked on the items_[0] path below. The early returns
    // skip it because they don't read items_[0], and Scheduler::call() at the top of
    // every loop iteration already performs its own cleanup before the next sleep-
    // duration computation happens.

#ifndef ESPHOME_THREAD_SINGLE
    // defer() items live in a separate queue that is drained at the top of every
    // loop tick via process_defer_queue_(). If any are pending, the next loop
    // iteration has work to do right now -- don't let the caller sleep.
    if (!this->defer_empty_())
      return 0;
#else
    // On single-threaded builds, defer() routes through set_timeout(..., 0) which
    // stages in to_add_. process_to_add() runs at the top of every scheduler.call(),
    // so anything in to_add_ becomes runnable on the next iteration; don't sleep.
    if (!this->to_add_empty_())
      return 0;
#endif

    // If no items, return empty optional
    if (!this->cleanup_())
      return {};

    SchedulerItem *item = this->items_[0];
    const auto now_64 = this->millis_64_from_(now);
=======
  bool HOT Scheduler::cancel_retry(Component * component, const char *name) {
    LockGuard guard{this->lock_};
    return this->cancel_item_locked_(component, name, SchedulerItem::TIMEOUT, /*match_retry=*/true);
  }

  // ===== scheduling =====

  optional<uint32_t> HOT Scheduler::next_schedule_in(uint32_t now) {
    if (s_scheduler_panic)
      return {};

    if (this->cleanup_() == 0)
      return {};

    auto &item = this->items_[0];
    auto *raw = item.get();
    if (is_suspicious_scheduler_ptr_(raw)) {
      ESP_LOGE(TAG, "Scheduler: ignoring invalid/poisoned item pointer=%p (next_schedule)", raw);
      // hard panic
      s_scheduler_panic = true;
      this->items_.clear();
      this->to_add_.clear();
#ifndef ESPHOME_THREAD_SINGLE
      this->defer_queue_.clear();
      this->defer_queue_front_ = 0;
#endif
      this->scheduler_item_pool_.clear();
      this->to_remove_ = 0;
      return {};
    }

    const auto now_64 = this->millis_64_(now);
>>>>>>> 19a82d45b (fix schedule issue wild pointer (not tested))
    const uint64_t next_exec = item->get_next_execution();
    if (next_exec < now_64)
      return 0u;
    return next_exec - now_64;
  }

<<<<<<< HEAD
  void Scheduler::full_cleanup_removed_items_() {
    // We hold the lock for the entire cleanup operation because:
    // 1. We're rebuilding the entire items_ list, so we need exclusive access throughout
    // 2. Other threads must see either the old state or the new state, not intermediate states
    // 3. The operation is already expensive (O(n)), so lock overhead is negligible
    // 4. No operations inside can block or take other locks, so no deadlock risk
    LockGuard guard{this->lock_};

    // Compact in-place: move valid items forward, recycle removed ones
    size_t write = 0;
    for (size_t read = 0; read < this->items_.size(); ++read) {
      if (!is_item_removed_locked_(this->items_[read])) {
        if (write != read) {
          this->items_[write] = this->items_[read];
        }
        ++write;
      } else {
        this->recycle_item_main_loop_(this->items_[read]);
      }
    }
    this->items_.erase(this->items_.begin() + write, this->items_.end());
    // Rebuild the heap structure since items are no longer in heap order
    std::make_heap(this->items_.begin(), this->items_.end(), SchedulerItem::cmp);
    this->to_remove_clear_locked_();
  }

#ifndef ESPHOME_THREAD_SINGLE
  void Scheduler::compact_defer_queue_locked_() {
    // Rare case: new items were added during processing - compact the vector
    // This only happens when:
    // 1. A deferred callback calls defer() again, or
    // 2. Another thread calls defer() while we're processing
    //
    // Move unprocessed items (added during this loop) to the front for next iteration
    //
    // SAFETY: Compacted items may include cancelled items (marked for removal via
    // cancel_item_locked_() during execution). This is safe because should_skip_item_()
    // checks is_item_removed_() before executing, so cancelled items will be skipped
    // and recycled on the next loop iteration.
    size_t remaining = this->defer_queue_.size() - this->defer_queue_front_;
    for (size_t i = 0; i < remaining; i++) {
      this->defer_queue_[i] = this->defer_queue_[this->defer_queue_front_ + i];
    }
    // Use erase() instead of resize() to avoid instantiating _M_default_append
    // (saves ~156 bytes flash). Erasing from the end is O(1) - no shifting needed.
    this->defer_queue_.erase(this->defer_queue_.begin() + remaining, this->defer_queue_.end());
  }
  void HOT Scheduler::process_defer_queue_slow_path_(uint32_t &now) {
    // Process defer queue to guarantee FIFO execution order for deferred items.
    // Previously, defer() used the heap which gave undefined order for equal timestamps,
    // causing race conditions on multi-core systems (ESP32, BK7200).
    // With the defer queue:
    // - Deferred items (delay=0) go directly to defer_queue_ in set_timer_common_
    // - Items execute in exact order they were deferred (FIFO guarantee)
    // - No deferred items exist in to_add_, so processing order doesn't affect correctness
    // Single-core platforms don't use this queue and fall back to the heap-based approach.
    //
    // Note: Items cancelled via cancel_item_locked_() are marked with remove=true but still
    // processed here. They are skipped during execution by should_skip_item_().
    // This is intentional - no memory leak occurs.
    //
    // We use an index (defer_queue_front_) to track the read position instead of calling
    // erase() on every pop, which would be O(n). The queue is processed once per loop -
    // any items added during processing are left for the next loop iteration.

    // Merge lock acquisitions: instead of separate locks for move-out and recycle (2N+1 total),
    // recycle each item after re-acquiring the lock for the next iteration (N+1 total).
    // The lock is held across: recycle → loop condition → move-out, then released for execution.
    SchedulerItem *item;

    this->lock_.lock();
    // Reset counter and snapshot queue end under lock
    this->defer_count_clear_locked_();
    size_t defer_queue_end = this->defer_queue_.size();
    if (this->defer_queue_front_ >= defer_queue_end) {
      this->lock_.unlock();
      return;
    }
    while (this->defer_queue_front_ < defer_queue_end) {
      // Take ownership of the item, leaving nullptr in the vector slot.
      // This is safe because:
      // 1. The vector is only cleaned up by cleanup_defer_queue_locked_() at the end of this function
      // 2. Any code iterating defer_queue_ MUST check for nullptr items (see mark_matching_items_removed_locked_)
      // 3. The lock protects concurrent access, but the nullptr remains until cleanup
      item = this->defer_queue_[this->defer_queue_front_];
      this->defer_queue_[this->defer_queue_front_] = nullptr;
      this->defer_queue_front_++;
      this->lock_.unlock();

      // Execute callback without holding lock to prevent deadlocks
      // if the callback tries to call defer() again
      if (!this->should_skip_item_(item)) {
        now = this->execute_item_(item, now);
      }

      this->lock_.lock();
      this->recycle_item_main_loop_(item);
    }
    // Clean up the queue (lock already held from last recycle or initial acquisition)
    this->cleanup_defer_queue_locked_();
    this->lock_.unlock();
  }
#endif /* not ESPHOME_THREAD_SINGLE */

  uint32_t HOT Scheduler::call(uint32_t now) {
#ifndef ESPHOME_THREAD_SINGLE
    this->process_defer_queue_(now);
#endif /* not ESPHOME_THREAD_SINGLE */

    // Extend the caller's 32-bit timestamp to 64-bit for scheduler operations
    const auto now_64 = this->millis_64_from_(now);
=======
  void HOT Scheduler::call(uint32_t now) {
    if (s_scheduler_panic) {
      // scheduler quarantined, let the rest of the app run
      return;
    }

#ifndef ESPHOME_THREAD_SINGLE
    // process defer queue
    size_t defer_queue_end = this->defer_queue_.size();
    while (this->defer_queue_front_ < defer_queue_end) {
      std::unique_ptr<SchedulerItem> item;
      {
        LockGuard lock(this->lock_);
        item = std::move(this->defer_queue_[this->defer_queue_front_]);
        this->defer_queue_front_++;
      }
      if (!this->should_skip_item_(item.get())) {
        now = this->execute_item_(item.get(), now);
      }
      this->recycle_item_(std::move(item));
      if (s_scheduler_panic)  // panic may have been set during recycle
        return;
    }
    if (this->defer_queue_front_ >= defer_queue_end) {
      LockGuard lock(this->lock_);
      this->cleanup_defer_queue_locked_();
    }
#endif

    const auto now_64 = this->millis_64_(now);
>>>>>>> 19a82d45b (fix schedule issue wild pointer (not tested))
    this->process_to_add();
    if (s_scheduler_panic)
      return;

<<<<<<< HEAD
    // Track if any items were added to to_add_ during callbacks
    bool has_added_items = false;

#ifdef ESPHOME_DEBUG_SCHEDULER
    static uint64_t last_print = 0;

    if (now_64 - last_print > 2000) {
      last_print = now_64;
      std::vector<SchedulerItem *> old_items;
      ESP_LOGD(TAG, "Items: count=%zu, pool=%zu, now=%" PRIu64, this->items_.size(), this->scheduler_item_pool_size_,
               now_64);
      // Cleanup before debug output
      this->cleanup_();
      while (!this->items_.empty()) {
        SchedulerItem *item;
        {
          LockGuard guard{this->lock_};
          item = this->pop_raw_locked_();
        }

        SchedulerNameLog name_log;
        bool is_cancelled = is_item_removed_(item);
        ESP_LOGD(TAG, "  %s '%s/%s' interval=%" PRIu32 " next_execution in %" PRIu64 "ms at %" PRIu64 "%s",
                 item->get_type_str(), LOG_STR_ARG(item->get_source()),
                 name_log.format(item->get_name_type(), item->get_name(), item->get_name_hash_or_id()), item->interval,
                 item->get_next_execution() - now_64, item->get_next_execution(), is_cancelled ? " [CANCELLED]" : "");

        old_items.push_back(item);
      }
      ESP_LOGD(TAG, "\n");

      {
        LockGuard guard{this->lock_};
        this->items_ = std::move(old_items);
        // Rebuild heap after moving items back
        std::make_heap(this->items_.begin(), this->items_.end(), SchedulerItem::cmp);
      }
    }
#endif /* ESPHOME_DEBUG_SCHEDULER */

    // Cleanup removed items before processing
    // First try to clean items from the top of the heap (fast path)
    this->cleanup_();

    // If we still have too many cancelled items, do a full cleanup
    // This only happens if cancelled items are stuck in the middle/bottom of the heap
    if (this->to_remove_count_() >= MAX_LOGICALLY_DELETED_ITEMS) {
      this->full_cleanup_removed_items_();
    }
    // IMPORTANT: This loop uses index-based access (items_[0]), NOT iterators.
    // This is intentional — fired intervals are pushed back into items_ via
    // push_back() + push_heap() below, which may reallocate the vector's storage.
    // Index-based access is safe across reallocations because we re-read items_[0]
    // at the top of each iteration. Do NOT convert this to a range-based for loop
    // or iterator-based loop, as that would break when items are added.
    while (!this->items_.empty()) {
      // Don't copy-by value yet
      SchedulerItem *item = this->items_[0];
=======
    // fast cleanup
    this->cleanup_();

    if (this->to_remove_ >= MAX_LOGICALLY_DELETED_ITEMS) {
      LockGuard guard{this->lock_};
      std::vector<std::unique_ptr<SchedulerItem> > valid_items;
      for (auto &item : this->items_) {
        if (!is_item_removed_(item.get())) {
          valid_items.push_back(std::move(item));
        } else {
          this->recycle_item_(std::move(item));
          if (s_scheduler_panic)
            return;
        }
      }
      this->items_ = std::move(valid_items);
      std::make_heap(this->items_.begin(), this->items_.end(), SchedulerItem::cmp);
      this->to_remove_ = 0;
    }

    bool has_added_items = false;

    while (!this->items_.empty()) {
      auto &item = this->items_[0];
      SchedulerItem *raw = item.get();

      // NEW: detect poisoned / freed entry in heap
      if (is_suspicious_scheduler_ptr_(raw)) {
        ESP_LOGE(TAG, "Scheduler: ignoring invalid/poisoned item pointer=%p (will not delete)", raw);
        ESP_LOGE(TAG, "Scheduler: removing bad item from heap: 0 – dropping all scheduled work");
        // hard quarantine
        s_scheduler_panic = true;
        // drop everything so we don't keep hitting this every loop
        this->items_.clear();
        this->to_add_.clear();
#ifndef ESPHOME_THREAD_SINGLE
        this->defer_queue_.clear();
        this->defer_queue_front_ = 0;
#endif
        this->scheduler_item_pool_.clear();
        this->to_remove_ = 0;
        return;
      }

>>>>>>> 19a82d45b (fix schedule issue wild pointer (not tested))
      if (item->get_next_execution() > now_64) {
        break;
      }

      if (item->component != nullptr && item->component->is_failed()) {
        LockGuard guard{this->lock_};
        this->recycle_item_main_loop_(this->pop_raw_locked_());
        continue;
      }

#ifdef ESPHOME_THREAD_MULTI_NO_ATOMICS
      {
        LockGuard guard{this->lock_};
        if (is_item_removed_locked_(item)) {
          this->recycle_item_main_loop_(this->pop_raw_locked_());
          this->to_remove_decrement_locked_();
          continue;
        }
      }
#else
      < < < < < < < HEAD
          // Single-threaded or multi-threaded with atomics: can check without lock
          if (is_item_removed_(item)) {
=======
if (is_item_removed_(item.get())) {
>>>>>>> 19a82d45b (fix schedule issue wild pointer (not tested))
      LockGuard guard{this->lock_};
      this->recycle_item_main_loop_(this->pop_raw_locked_());
      this->to_remove_decrement_locked_();
      continue;
    }
#endif

    < < < < < < < HEAD
#ifdef ESPHOME_DEBUG_SCHEDULER
    {
      SchedulerNameLog name_log;
      ESP_LOGV(TAG, "Running %s '%s/%s' with interval=%" PRIu32 " next_execution=%" PRIu64 " (now=%" PRIu64 ")",
               item->get_type_str(), LOG_STR_ARG(item->get_source()),
               name_log.format(item->get_name_type(), item->get_name(), item->get_name_hash_or_id()), item->interval,
               item->get_next_execution(), now_64);
    }
#endif /* ESPHOME_DEBUG_SCHEDULER */

    // Warning: During callback(), a lot of stuff can happen, including:
    //  - timeouts/intervals get added, potentially invalidating vector pointers
    //  - timeouts/intervals get cancelled
    now = this->execute_item_(item, now);

    LockGuard guard{this->lock_};

    // Only pop after function call, this ensures we were reachable
    // during the function call and know if we were cancelled.
    SchedulerItem *executed_item = this->pop_raw_locked_();

    if (this->is_item_removed_locked_(executed_item)) {
      // We were removed/cancelled in the function call, recycle and continue
      this->to_remove_decrement_locked_();
      this->recycle_item_main_loop_(executed_item);
=======
      now = this->execute_item_(item.get(), now);

      LockGuard guard{this->lock_};

      auto executed_item = std::move(this->items_[0]);
      this->pop_raw_();

      if (executed_item && executed_item->remove) {
        this->to_remove_--;
>>>>>>> 19a82d45b (fix schedule issue wild pointer (not tested))
        continue;
      }

      if (executed_item && executed_item->type == SchedulerItem::INTERVAL) {
        executed_item->set_next_execution(now_64 + executed_item->interval);
<<<<<<< HEAD
        // Push directly back into the heap instead of routing through to_add_.
        // This is safe because:
        // 1. We're on the main loop and already hold the lock
        // 2. The item was already popped from items_ via pop_raw_locked_() above
        // 3. The while loop uses index-based access (items_[0]), not iterators,
        //    so push_back() reallocation cannot invalidate our iteration
        // 4. push_heap() restores the heap invariant before the next iteration
        //    peeks at items_[0]
        // This avoids the to_add_ detour and the overhead of
        // process_to_add_slow_path_() (lock acquisition, vector iteration, clear).
        this->items_.push_back(executed_item);
        std::push_heap(this->items_.begin(), this->items_.end(), SchedulerItem::cmp);
      } else {
        // Timeout completed - recycle it
        this->recycle_item_main_loop_(executed_item);
=======
          this->to_add_.push_back(std::move(executed_item));
        } else {
          this->recycle_item_(std::move(executed_item));
          if (s_scheduler_panic)
            return;
>>>>>>> 19a82d45b (fix schedule issue wild pointer (not tested))
      }

      has_added_items |= !this->to_add_.empty();
    }

    if (has_added_items) {
      this->process_to_add();
    }

#ifdef ESPHOME_DEBUG_SCHEDULER
    // Verify no items were leaked during this call() cycle.
    // All items must be in items_, to_add_, defer_queue_, or the pool.
    // Safe to check here because:
    // - process_defer_queue_ has already run its cleanup_defer_queue_locked_(),
    //   so defer_queue_ contains no nullptr slots inflating the count.
    // - The while loop above has finished, so no items are held in local variables;
    //   every item has been returned to a container (items_, to_add_, or pool).
    // Lock needed to get a consistent snapshot of all containers.
    {
      LockGuard guard{this->lock_};
      this->debug_verify_no_leak_();
    }
#endif
    // execute_item_() advances `now` as items fire; return it so the caller
    // stays monotonic with last_wdt_feed_.
    return now;
  }
<<<<<<< HEAD
  void HOT Scheduler::process_to_add_slow_path_() {
    LockGuard guard{this->lock_};
    for (auto *&it : this->to_add_) {
      if (is_item_removed_locked_(it)) {
        // Recycle cancelled items
        this->recycle_item_main_loop_(it);
        it = nullptr;
        continue;
      }

      this->items_.push_back(it);
=======

    void HOT Scheduler::process_to_add() {
      if (s_scheduler_panic) {
        // drop stuff that late joiners tried to schedule
        this->to_add_.clear();
        return;
      }

      LockGuard guard{this->lock_};
      for (auto &it : this->to_add_) {
        if (is_item_removed_(it.get())) {
          this->recycle_item_(std::move(it));
          if (s_scheduler_panic)
            return;
          continue;
        }
        this->items_.push_back(std::move(it));
>>>>>>> 19a82d45b (fix schedule issue wild pointer (not tested))
      std::push_heap(this->items_.begin(), this->items_.end(), SchedulerItem::cmp);
    }
    this->to_add_.clear();
    this->to_add_count_clear_locked_();
  }
<<<<<<< HEAD
  bool HOT Scheduler::cleanup_slow_path_() {
    // We must hold the lock for the entire cleanup operation because:
    // 1. We're modifying items_ (via pop_raw_locked_) which requires exclusive access
    // 2. We're decrementing to_remove_ which is also modified by other threads
    //    (though all modifications are already under lock)
    // 3. Other threads read items_ when searching for items to cancel in cancel_item_locked_()
    // 4. We need a consistent view of items_ and to_remove_ throughout the operation
    // Without the lock, we could access items_ while another thread is reading it,
    // leading to race conditions
=======

    size_t HOT Scheduler::cleanup_() {
      if (s_scheduler_panic)
        return 0;

      if (this->to_remove_ == 0)
        return this->items_.size();

>>>>>>> 19a82d45b (fix schedule issue wild pointer (not tested))
    LockGuard guard{this->lock_};
    while (!this->items_.empty()) {
      SchedulerItem *item = this->items_[0];
      if (!this->is_item_removed_locked_(item))
        break;
<<<<<<< HEAD
      this->to_remove_decrement_locked_();
      this->recycle_item_main_loop_(this->pop_raw_locked_());
=======
        this->to_remove_--;
        this->pop_raw_();
        if (s_scheduler_panic)
          return 0;
>>>>>>> 19a82d45b (fix schedule issue wild pointer (not tested))
    }
    return !this->items_.empty();
  }
<<<<<<< HEAD
  Scheduler::SchedulerItem *HOT Scheduler::pop_raw_locked_() {
    std::pop_heap(this->items_.begin(), this->items_.end(), SchedulerItem::cmp);

    SchedulerItem *item = this->items_.back();
=======

    void HOT Scheduler::pop_raw_() {
      std::pop_heap(this->items_.begin(), this->items_.end(), SchedulerItem::cmp);
      this->recycle_item_(std::move(this->items_.back()));
>>>>>>> 19a82d45b (fix schedule issue wild pointer (not tested))
    this->items_.pop_back();
    return item;
  }

  // Helper to execute a scheduler item
  uint32_t HOT Scheduler::execute_item_(SchedulerItem * item, uint32_t now) {
    App.set_current_component(item->component);
    // Freshen so callbacks reading App.get_loop_component_start_time() see this item's dispatch time.
    App.set_loop_component_start_time_(now);
    WarnIfComponentBlockingGuard guard{item->component, now};
    item->callback();
    uint32_t end = guard.finish();
    // Feed the watchdog after each scheduled item (both main heap and defer
    // queue paths go through here). A run of back-to-back callbacks cannot
    // starve the wdt. The inline fast path is a load + sub + branch — nearly
    // free when the 3 ms rate limit hasn't elapsed.
    App.feed_wdt_with_time(end);
    return end;
  }

<<<<<<< HEAD
  // Common implementation for cancel operations - handles locking
  bool HOT Scheduler::cancel_item_(Component * component, NameType name_type, const char *static_name,
                                   uint32_t hash_or_id, SchedulerItem::Type type, bool match_retry) {
=======
    // Common implementation for cancel operations
    bool HOT Scheduler::cancel_item_(Component * component, bool is_static_string, const void *name_ptr,
                                     SchedulerItem::Type type) {
      if (s_scheduler_panic)
        return false;
      const char *name_cstr = this->get_name_cstr_(is_static_string, name_ptr);
>>>>>>> 19a82d45b (fix schedule issue wild pointer (not tested))
    LockGuard guard{this->lock_};
    // Public cancel path uses default find_first=false to cancel ALL matches because
    // DelayAction parallel mode (skip_cancel=true) can create multiple items with the same key.
    return this->cancel_item_locked_(component, name_type, static_name, hash_or_id, type, match_retry);
  }

<<<<<<< HEAD
  // Helper to cancel matching items - must be called with lock held.
  // When find_first=true, stops after the first match and exits across containers
  // (used by set_timer_common_ where cancel-before-add guarantees at most one match).
  // When find_first=false, cancels ALL matches across all containers (needed for
  // public cancel path where DelayAction parallel mode can create duplicates).
  // name_type determines matching: STATIC_STRING uses static_name, others use hash_or_id
  size_t Scheduler::mark_matching_items_removed_slow_locked_(
      std::vector<SchedulerItem *> & container, Component * component, NameType name_type, const char *static_name,
      uint32_t hash_or_id, SchedulerItem::Type type, bool match_retry, bool find_first) {
    size_t count = 0;
    for (auto *item : container) {
      if (this->matches_item_locked_(item, component, name_type, static_name, hash_or_id, type, match_retry)) {
        this->set_item_removed_(item, true);
        if (find_first)
          return 1;
        count++;
      }
    }
    return count;
  }

  bool HOT Scheduler::cancel_item_locked_(Component * component, NameType name_type, const char *static_name,
                                          uint32_t hash_or_id, SchedulerItem::Type type, bool match_retry,
                                          bool find_first) {
    // Early return if static string name is invalid
    if (name_type == NameType::STATIC_STRING && static_name == nullptr) {
=======
    bool HOT Scheduler::cancel_item_locked_(Component * component, const char *name_cstr, SchedulerItem::Type type,
                                            bool match_retry) {
      if (name_cstr == nullptr)
>>>>>>> 19a82d45b (fix schedule issue wild pointer (not tested))
      return false;

      size_t total_cancelled = 0;

#ifndef ESPHOME_THREAD_SINGLE
      if (type == SchedulerItem::TIMEOUT) {
        total_cancelled += this->mark_matching_items_removed_locked_(
            this->defer_queue_, component, name_type, static_name, hash_or_id, type, match_retry, find_first);
        if (find_first && total_cancelled > 0)
          return true;
      }
#endif

      < < < < < < < HEAD
      // Cancel items in the main heap
      // We only mark items for removal here - never recycle directly.
      // The main loop may be executing an item's callback right now, and recycling
      // would destroy the callback while it's running (use-after-free).
      // Only the main loop in call() should recycle items after execution completes.
      {
        size_t heap_cancelled = this->mark_matching_items_removed_locked_(
            this->items_, component, name_type, static_name, hash_or_id, type, match_retry, find_first);
        total_cancelled += heap_cancelled;
        this->to_remove_add_locked_(heap_cancelled);
        if (find_first && total_cancelled > 0)
          return true;
      }

      // Cancel items in to_add_
      total_cancelled += this->mark_matching_items_removed_locked_(this->to_add_, component, name_type, static_name,
                                                                   hash_or_id, type, match_retry, find_first);
=======
      if (!this->items_.empty()) {
        auto &last_item = this->items_.back();
        if (this->matches_item_(last_item, component, name_cstr, type, match_retry)) {
          this->recycle_item_(std::move(this->items_.back()));
          this->items_.pop_back();
          total_cancelled++;
          if (s_scheduler_panic)
            return true;
        }
        size_t heap_cancelled =
            this->mark_matching_items_removed_(this->items_, component, name_cstr, type, match_retry);
        total_cancelled += heap_cancelled;
        this->to_remove_ += heap_cancelled;
      }

      total_cancelled += this->mark_matching_items_removed_(this->to_add_, component, name_cstr, type, match_retry);
>>>>>>> 19a82d45b (fix schedule issue wild pointer (not tested))

      return total_cancelled > 0;
    }

<<<<<<< HEAD
    bool HOT Scheduler::SchedulerItem::cmp(SchedulerItem * a, SchedulerItem * b) {
      // High bits are almost always equal (change only on 32-bit rollover ~49 days)
      // Optimize for common case: check low bits first when high bits are equal
=======
    // ===== time conversion =====

    uint64_t Scheduler::millis_64_(uint32_t now) {
#ifdef ESPHOME_THREAD_SINGLE
      uint16_t major = this->millis_major_;
      uint32_t last = this->last_millis_;

      if (now < last && (last - now) > HALF_MAX_UINT32) {
        this->millis_major_++;
        major++;
      }

      if (now > last) {
        this->last_millis_ = now;
      }

      return now + (static_cast<uint64_t>(major) << 32);

#elif defined(ESPHOME_THREAD_MULTI_NO_ATOMICS)
        uint16_t major = this->millis_major_;
        uint32_t last = this->last_millis_;

        static const uint32_t ROLLOVER_WINDOW = 10000;

        bool near_rollover =
            (last > (std::numeric_limits<uint32_t>::max() - ROLLOVER_WINDOW)) || (now < ROLLOVER_WINDOW);

        if (near_rollover || (now < last && (last - now) > HALF_MAX_UINT32)) {
          LockGuard guard{this->lock_};
          last = this->last_millis_;
          if (now < last && (last - now) > HALF_MAX_UINT32) {
            this->millis_major_++;
            major++;
          }
          this->last_millis_ = now;
        } else if (now > last) {
          this->last_millis_ = now;
        }

        return now + (static_cast<uint64_t>(major) << 32);

#elif defined(ESPHOME_THREAD_MULTI_ATOMICS)
        for (;;) {
          uint16_t major = this->millis_major_.load(std::memory_order_acquire);
          uint32_t last = this->last_millis_.load(std::memory_order_acquire);

          if (now < last && (last - now) > HALF_MAX_UINT32) {
            LockGuard guard{this->lock_};
            last = this->last_millis_.load(std::memory_order_relaxed);
            if (now < last && (last - now) > HALF_MAX_UINT32) {
              this->millis_major_.fetch_add(1, std::memory_order_relaxed);
              major++;
            }
            this->last_millis_.store(now, std::memory_order_release);
          } else {
            while (now > last && (now - last) < HALF_MAX_UINT32) {
              if (this->last_millis_.compare_exchange_weak(last, now, std::memory_order_release,
                                                           std::memory_order_relaxed)) {
                break;
              }
            }
          }
          uint16_t major_end = this->millis_major_.load(std::memory_order_relaxed);
          if (major_end == major)
            return now + (static_cast<uint64_t>(major) << 32);
        }
        __builtin_unreachable();
#else
#error "No platform threading model defined."
#endif
    }

    // ===== item helpers =====

    bool HOT Scheduler::SchedulerItem::cmp(const std::unique_ptr<SchedulerItem> &a,
                                           const std::unique_ptr<SchedulerItem> &b) {
>>>>>>> 19a82d45b (fix schedule issue wild pointer (not tested))
      return (a->next_execution_high_ == b->next_execution_high_) ? (a->next_execution_low_ > b->next_execution_low_)
                                                                  : (a->next_execution_high_ > b->next_execution_high_);
    }

    // Recycle a SchedulerItem back to the freelist for reuse.
    // IMPORTANT: Caller must hold the scheduler lock.
    void Scheduler::recycle_item_main_loop_(SchedulerItem * item) {
      if (item == nullptr)
        return;

<<<<<<< HEAD
      item->callback = nullptr;  // release captured resources
      item->next_free = this->scheduler_item_pool_head_;
      this->scheduler_item_pool_head_ = item;
      this->scheduler_item_pool_size_++;
#ifdef ESPHOME_DEBUG_SCHEDULER
      ESP_LOGD(TAG, "Recycled item to pool (pool size now: %zu)", this->scheduler_item_pool_size_);
#endif
      == == == = SchedulerItem *raw = item.get();
      if (is_suspicious_scheduler_ptr_(raw)) {
        ESP_LOGE(TAG, "Scheduler: ignoring invalid/poisoned item pointer=%p (will not delete)", raw);
        // hard quarantine here too – we just saw memory poison in a place we own
        s_scheduler_panic = true;
        (void) item.release();  // leak instead of crashing
        return;
      }

      if (this->scheduler_item_pool_.size() < MAX_POOL_SIZE) {
        item->callback = nullptr;
        item->clear_dynamic_name();
        this->scheduler_item_pool_.push_back(std::move(item));
      } else {
        // let unique_ptr dtor free it
      }
>>>>>>> 19a82d45b (fix schedule issue wild pointer (not tested))
    }

    // Shrink a SchedulerItem* vector's capacity to its current size.
    // std::vector::shrink_to_fit() is non-binding and our toolchain ignores it; the classic
    // swap-with-copy idiom (std::vector<T>(other).swap(other)) instantiates the iterator-range
    // constructor which pulls in std::__throw_bad_array_new_length and ~120 B of related
    // stdlib RTTI/typeinfo. Build into a temp via reserve + push_back instead, then move-assign:
    // reserve uses operator new (throws bad_alloc, already linked) and push_back without growth
    // is the noexcept tail path. Move-assign just swaps pointers.
    // Out-of-line + noinline so the callers in trim_freelist() share one body.
    void __attribute__((noinline)) Scheduler::shrink_scheduler_vector_(std::vector<SchedulerItem *> * v) {
      if (v->capacity() == v->size())
        return;  // already exact, common after a quiet period
      std::vector<SchedulerItem *> tmp;
      tmp.reserve(v->size());
      for (SchedulerItem *p : *v)
        tmp.push_back(p);
      *v = std::move(tmp);
    }

    void Scheduler::trim_freelist() {
      LockGuard guard{this->lock_};
      SchedulerItem *item = this->scheduler_item_pool_head_;
      size_t freed = 0;
      while (item != nullptr) {
        SchedulerItem *next = item->next_free;
        delete item;
#ifdef ESPHOME_DEBUG_SCHEDULER
        this->debug_live_items_--;
#endif
        item = next;
        freed++;
      }
      this->scheduler_item_pool_head_ = nullptr;
      this->scheduler_item_pool_size_ = 0;

      // items_/to_add_/defer_queue_ retain their boot-peak vector capacity (vector grows
      // by doubling and otherwise keeps the peak). Reclaim that slack as well.
      shrink_scheduler_vector_(&this->items_);
      shrink_scheduler_vector_(&this->to_add_);
#ifndef ESPHOME_THREAD_SINGLE
      shrink_scheduler_vector_(&this->defer_queue_);
#endif

#ifdef ESPHOME_DEBUG_SCHEDULER
      ESP_LOGD(TAG, "Freelist trimmed (%zu items freed)", freed);
#else
      (void) freed;
#endif
    }

#ifdef ESPHOME_DEBUG_SCHEDULER
    void Scheduler::debug_log_timer_(const SchedulerItem *item, NameType name_type, const char *static_name,
                                     uint32_t hash_or_id, SchedulerItem::Type type, uint32_t delay, uint64_t now) {
      // Validate static strings in debug mode
      if (name_type == NameType::STATIC_STRING && static_name != nullptr) {
        validate_static_string(static_name);
      }

      // Debug logging
      SchedulerNameLog name_log;
      const char *type_str = (type == SchedulerItem::TIMEOUT) ? "timeout" : "interval";
      if (type == SchedulerItem::TIMEOUT) {
        ESP_LOGD(TAG, "set_%s(name='%s/%s', %s=%" PRIu32 ")", type_str, LOG_STR_ARG(item->get_source()),
                 name_log.format(name_type, static_name, hash_or_id), type_str, delay);
      } else {
        ESP_LOGD(TAG, "set_%s(name='%s/%s', %s=%" PRIu32 ", offset=%" PRIu32 ")", type_str,
                 LOG_STR_ARG(item->get_source()), name_log.format(name_type, static_name, hash_or_id), type_str, delay,
                 static_cast<uint32_t>(item->get_next_execution() - now));
      }
    }
#endif /* ESPHOME_DEBUG_SCHEDULER */

    // Pop from freelist or allocate. IMPORTANT: caller must hold the lock and must overwrite
    // `item->component` before releasing it -- the popped slot still holds the freelist link.
    Scheduler::SchedulerItem *Scheduler::get_item_from_pool_locked_() {
      if (this->scheduler_item_pool_head_ != nullptr) {
        SchedulerItem *item = this->scheduler_item_pool_head_;
        this->scheduler_item_pool_head_ = item->next_free;
        this->scheduler_item_pool_size_--;
#ifdef ESPHOME_DEBUG_SCHEDULER
        ESP_LOGD(TAG, "Reused item from pool (pool size now: %zu)", this->scheduler_item_pool_size_);
#endif
        return item;
      }
#ifdef ESPHOME_DEBUG_SCHEDULER
      ESP_LOGD(TAG, "Allocated new item (pool empty)");
#endif
      auto *item = new SchedulerItem();
#ifdef ESPHOME_DEBUG_SCHEDULER
      this->debug_live_items_++;
#endif
      return item;
    }

#ifdef ESPHOME_DEBUG_SCHEDULER
    bool Scheduler::debug_verify_no_leak_() const {
      // Invariant: every live SchedulerItem must be in exactly one container.
      // debug_live_items_ tracks allocations minus deletions.
      size_t accounted = this->items_.size() + this->to_add_.size() + this->scheduler_item_pool_size_;
#ifndef ESPHOME_THREAD_SINGLE
      accounted += this->defer_queue_.size();
#endif
      if (accounted != this->debug_live_items_) {
        ESP_LOGE(TAG,
                 "SCHEDULER LEAK DETECTED: live=%" PRIu32 " but accounted=%" PRIu32 " (items=%" PRIu32
                 " to_add=%" PRIu32 " pool=%" PRIu32
#ifndef ESPHOME_THREAD_SINGLE
                 " defer=%" PRIu32
#endif
                 ")",
                 static_cast<uint32_t>(this->debug_live_items_), static_cast<uint32_t>(accounted),
                 static_cast<uint32_t>(this->items_.size()), static_cast<uint32_t>(this->to_add_.size()),
                 static_cast<uint32_t>(this->scheduler_item_pool_size_)
#ifndef ESPHOME_THREAD_SINGLE
                     ,
                 static_cast<uint32_t>(this->defer_queue_.size())
#endif
        );
        assert(false);
        return false;
      }
      return true;
    }
#endif

  }  // namespace esphome
