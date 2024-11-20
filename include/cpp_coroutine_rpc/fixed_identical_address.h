/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <boost/interprocess/creation_tags.hpp>
#include <boost/interprocess/interprocess_fwd.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/interprocess_semaphore.hpp>
#include <coroutine>
#include <cstddef>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <memory_resource>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace cpp_coroutine_rpc::fixed_identical_address {

namespace bip = boost::interprocess;

class rpc_context;

class segment_memory_resource : public std::pmr::memory_resource {
  bip::fixed_managed_shared_memory::segment_manager *m_segment_manager;

public:
  explicit segment_memory_resource(rpc_context *context);

private:
  auto do_allocate(size_t bytes, size_t alignment) -> void * override {
    return m_segment_manager->allocate_aligned(bytes, alignment);
  }
  void do_deallocate(void *ptr, size_t /*bytes*/,
                     size_t /*alignment*/) override {
    m_segment_manager->deallocate(ptr);
  }
  [[nodiscard]] auto
  do_is_equal(const std::pmr::memory_resource &other) const noexcept
      -> bool override {
    try {
      const auto *other_p =
          dynamic_cast<const segment_memory_resource *>(&other);
      return this->m_segment_manager == other_p->m_segment_manager;
    } catch (const std::bad_cast &e) {
      return false;
    }
  }
};

class task {
  friend class rpc_context;

  bool m_started{false};

public:
  class promise_type {
    friend class task;
    std::exception_ptr m_exception;
    static auto next_aligned_size(size_t origin_size) -> size_t {
      constexpr size_t align =
          alignof(bip::fixed_managed_shared_memory::segment_manager *);
      size_t next_aligned_size = (origin_size + align - 1) & ~(align - 1);
      return next_aligned_size;
    }
    rpc_context *m_current_context;
    std::pmr::string m_initial_process_full_name;
    bip::interprocess_semaphore m_finished{0};

  public:
    auto get_return_object() -> task {
      return task{handle_type::from_promise(*this)};
    }
    static auto initial_suspend() -> std::suspend_always { return {}; }
    auto final_suspend() noexcept -> std::suspend_always {
      m_finished.post();
      return {};
    }
    void return_void() {}
    void unhandled_exception() { m_exception = std::current_exception(); }

    promise_type() = delete;

    template <class... Args>
    explicit promise_type(rpc_context *local_context,
                          const Args &.../*unused*/);

    void set_current_context(rpc_context *context) {
      m_current_context = context;
    }

    auto operator new(const std::size_t size) -> void * = delete;

    template <class... Args>
    auto operator new(const std::size_t size, rpc_context *local_context,
                      const Args &.../*unused*/) -> void *;

    void operator delete(void *ptr) = delete;

    // NOLINTNEXTLINE(cert-dcl54-cpp,*-new-delete-*)
    void operator delete(void *ptr, size_t size);
  };

private:
  using handle_type = std::coroutine_handle<promise_type>;
  handle_type m_coroutine_handle;

public:
  explicit task(handle_type promise) : m_coroutine_handle{promise} {};
  task(const task &) = delete;
  task(task &&other) noexcept
      : m_coroutine_handle(other.m_coroutine_handle),
        m_started(other.m_started) {
    other.m_coroutine_handle = nullptr;
  };
  auto operator=(const task &) -> task & = delete;
  auto operator=(task &&other) noexcept -> task & {
    m_coroutine_handle = other.m_coroutine_handle;
    m_started = other.m_started;
    other.m_coroutine_handle = nullptr;
    return *this;
  };
  ~task() {
    if (m_coroutine_handle != nullptr) {
      if (!m_coroutine_handle.done() && m_started) {
        std::cerr << "Error: try to destruct a task which has started but not "
                     "finished is not allowed. You must wait() the task, or "
                     "get true from try_wait() or timed_wait() before destruct "
                     "task class!\n";
        std::terminate();
      }
      m_coroutine_handle.destroy();
    }
  }

  void start() {
    if (m_started) {
      std::cerr << "Error: task has already started\n";
      std::terminate();
    }
    m_started = true;
    m_coroutine_handle.resume();
  }

  void wait() {
    m_coroutine_handle.promise().m_finished.wait();
    if (m_coroutine_handle.promise().m_exception) {
      std::rethrow_exception(m_coroutine_handle.promise().m_exception);
    }
  }
  auto try_wait() -> bool {
    auto finished = m_coroutine_handle.promise().m_finished.try_wait();
    if (finished) {
      if (m_coroutine_handle.promise().m_exception) {
        std::rethrow_exception(m_coroutine_handle.promise().m_exception);
      }
    }
    return finished;
  }
  template <typename TimePoint = boost::posix_time::ptime>
  auto timed_wait(const TimePoint &abs_time) -> bool {
    auto finished =
        m_coroutine_handle.promise().m_finished.timed_wait(abs_time);
    if (finished) {
      if (m_coroutine_handle.promise().m_exception) {
        std::rethrow_exception(m_coroutine_handle.promise().m_exception);
      }
    }
    return finished;
  };
};

class rpc_context {
  friend class segment_memory_resource;
  friend class task::promise_type;

  class process_info {
    rpc_context *m_context;

  public:
    explicit process_info(rpc_context *context) : m_context(context) {}
    auto get_context() -> rpc_context * { return m_context; }
  };

  class other_process_t {
    rpc_context *m_local_context;
    bool m_cache_info;
    struct string_hash {
      using hash_type = std::hash<std::string_view>;
      using is_transparent = void;

      auto operator()(const char *str) const -> std::size_t {
        return hash_type{}(str);
      }
      auto operator()(std::string_view str) const -> std::size_t {
        return hash_type{}(str);
      }
      auto operator()(std::string const &str) const -> std::size_t {
        return hash_type{}(str);
      }
    };
    struct cache_info_t {
      bip::message_queue m_queue;
      rpc_context *m_context;
      std::string m_full_name;

      explicit cache_info_t(const char *queue_name, rpc_context *context,
                            std::string_view full_name)
          : m_queue(bip::open_only, queue_name), m_context(context),
            m_full_name(full_name) {};
    };
    mutable std::unordered_map<std::string, std::shared_ptr<cache_info_t>,
                               string_hash, std::equal_to<>>
        m_cache;

  public:
    explicit other_process_t(bool cache_info, rpc_context *current_context)
        : m_cache_info(cache_info), m_local_context(current_context) {}

    auto get_target_info(std::string_view target_name) const
        -> std::shared_ptr<cache_info_t> {
      if (m_cache_info) {
        auto find_result = m_cache.find(target_name);
        if (find_result != m_cache.end()) {
          return find_result->second;
        }
      }
      std::string target_full_name =
          get_my_full_name(m_local_context->m_global_context_name, target_name);
      std::string m_target_queue_name = get_full_queue_name(target_full_name);
      auto target_find_result =
          m_local_context->m_shm_segment->find<process_info>(
              target_full_name.c_str());
      if (target_find_result.first == nullptr) {
        throw std::runtime_error{
            "target process required to resume the coroutine is not found"};
      }
      rpc_context *target_context = target_find_result.first->get_context();

      auto final_result = std::make_shared<cache_info_t>(
          m_target_queue_name.c_str(), target_context, target_full_name);

      if (m_cache_info) {
        std::pair<decltype(m_cache)::iterator, bool> return_value;
        return_value = m_cache.emplace(target_name, final_result);
        if (!return_value.second) {
          throw std::runtime_error("failed to emplace cache entry");
        }
      }

      return final_result;
    }
  };

  std::string m_global_context_name;
  std::string m_my_full_name;
  std::string m_my_queue_name;
  std::string m_my_raw_name;

  bip::fixed_managed_shared_memory *m_shm_segment;
  process_info *m_my_process_info;
  bip::message_queue m_my_queue;

  other_process_t m_other_process;
  void *m_local_context;

  static auto get_my_full_name(std::string_view global_context_name,
                               std::string_view my_raw_name) -> std::string {
    return std::string{global_context_name} + "_" + my_raw_name.data();
  }
  static auto get_full_queue_name(std::string_view my_full_name)
      -> std::string {
    return std::string{my_full_name} + "_queue";
  }

public:
  static void remove_named_objects(std::string_view global_context_name,
                                   const std::string_view local_name) {
    bip::message_queue::remove(
        get_full_queue_name(get_my_full_name(global_context_name, local_name))
            .c_str());
  }
  // no move, no copy
  rpc_context(const rpc_context &) noexcept = delete;
  rpc_context(rpc_context &&) noexcept = delete;
  auto operator=(const rpc_context &) noexcept -> rpc_context & = delete;
  auto operator=(rpc_context &&) noexcept -> rpc_context & = delete;

  explicit rpc_context(bip::fixed_managed_shared_memory *segment,
                       const std::string_view global_context_name,
                       const std::string_view my_name, void *local_context,
                       size_t queue_size, const bool cache_info)
      : m_shm_segment(segment), m_global_context_name(global_context_name),
        m_my_raw_name(my_name),
        m_my_full_name(get_my_full_name(global_context_name, my_name)),
        m_my_queue_name(get_full_queue_name(m_my_full_name)),
        m_local_context(local_context),
        m_my_process_info(m_shm_segment->construct<process_info>(
            m_my_full_name.c_str())(this)),
        m_my_queue(bip::create_only, m_my_queue_name.c_str(), queue_size,
                   sizeof(void *)),
        m_other_process(cache_info, this) {}
  ~rpc_context() {
    m_shm_segment->destroy<process_info>(m_my_full_name.c_str());
    bip::message_queue::remove(m_my_queue_name.c_str());
  };

  [[nodiscard]] auto get_segment() const -> bip::fixed_managed_shared_memory * {
    return m_shm_segment;
  }

  [[nodiscard]] auto get_raw_name() const -> std::string_view {
    return m_my_raw_name;
  }

  [[nodiscard]] auto get_local_context() const -> void * {
    return m_local_context;
  }

private:
  class process_switch_awaitable {
    rpc_context *m_local_process_context;
    std::string_view m_target_name;
    rpc_context *m_target_process_context{};

  public:
    explicit process_switch_awaitable(rpc_context *context,
                                      std::string_view target_process_name)
        : m_local_process_context(context), m_target_name(target_process_name) {
    }

    auto await_ready() -> bool {
      auto find_result =
          m_local_process_context->m_other_process.get_target_info(
              m_target_name);
      if (m_local_process_context->m_my_full_name == find_result->m_full_name) {
        m_target_process_context = m_local_process_context;
        return true;
      } else {
        m_target_process_context = find_result->m_context;
        return false;
      };
    }
    void
    await_suspend(std::coroutine_handle<task::promise_type> coroutine_handle) {
      auto find_result =
          m_local_process_context->m_other_process.get_target_info(
              m_target_name);
      coroutine_handle.promise().set_current_context(find_result->m_context);
      find_result->m_queue.send(&coroutine_handle, sizeof(coroutine_handle), 0);
    };
    auto await_resume() -> rpc_context * { return m_target_process_context; };
  };

public:
  auto resume_on(std::string_view target_name) -> process_switch_awaitable {
    return process_switch_awaitable{this, target_name};
  }
  void listen_once() {
    void *handle = nullptr;
    size_t msg_size{};
    unsigned int priority{};
    while (m_my_queue.try_receive(static_cast<void *>(std::addressof(handle)),
                                  sizeof(void *), msg_size, priority)) {
      auto coroutine_handle =
          std::coroutine_handle<task::promise_type>::from_address(handle);
      coroutine_handle.resume();
    }
  }
  template <typename TimePoint = boost::posix_time::ptime>
  void listen_timeout(const TimePoint &abs_time) {
    void *handle = nullptr;
    size_t msg_size{};
    unsigned int priority{};
    while (m_my_queue.timed_receive(static_cast<void *>(std::addressof(handle)),
                                    sizeof(void *), msg_size, priority,
                                    abs_time)) {
      auto coroutine_handle =
          std::coroutine_handle<task::promise_type>::from_address(handle);
      coroutine_handle.resume();
    }
  }
};

inline segment_memory_resource::segment_memory_resource(rpc_context *context)
    : m_segment_manager(context->get_segment()->get_segment_manager()) {}

template <class... Args>
task::promise_type::promise_type(rpc_context *local_context,
                                 const Args &.../*unused*/)
    : m_current_context(local_context),
      m_initial_process_full_name(local_context->m_my_full_name) {}

template <class... Args>
auto task::promise_type::operator new(const std::size_t size,
                                      rpc_context *local_context,
                                      const Args &.../*unused*/) -> void * {
  const size_t ceiled_alloc_size = next_aligned_size(size);
  void *ptr = local_context->get_segment()->allocate(
      ceiled_alloc_size + sizeof(bip::fixed_managed_shared_memory *));

  // NOLINTBEGIN(*-reinterpret-cast,*-pointer-arithmetic)
  *reinterpret_cast<bip::fixed_managed_shared_memory::segment_manager **>(
      static_cast<char *>(ptr) + ceiled_alloc_size) =
      local_context->m_shm_segment->get_segment_manager();
  // NOLINTEND(*-reinterpret-cast,*-pointer-arithmetic)

  return ptr;
}

// NOLINTNEXTLINE(cert-dcl54-cpp,*-new-delete-*)
inline void task::promise_type::operator delete(void *ptr, size_t size) {
  const size_t ceiled_alloc_size = next_aligned_size(size);

  // NOLINTBEGIN(*-reinterpret-cast,*-pointer-arithmetic)
  bip::fixed_managed_shared_memory::segment_manager *segment_manager =
      *reinterpret_cast<bip::fixed_managed_shared_memory::segment_manager **>(
          static_cast<char *>(ptr) + ceiled_alloc_size);
  // NOLINTEND(*-reinterpret-cast,*-pointer-arithmetic)

  segment_manager->deallocate(ptr);
};

} // namespace cpp_coroutine_rpc::fixed_identical_address