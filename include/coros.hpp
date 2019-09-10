#pragma once

#include <uv.h>

#include <cstddef>
#include <functional>
#include <string>
#include <mutex>
#include <vector>

#include "uvpp.hpp"

#if defined(_WIN32)
#define BAD_SOCKET (uintptr_t)(~0)
#else
#define BAD_SOCKET (-1)
#endif

namespace coros {
namespace context {

typedef void* fcontext_t;

typedef struct transfer_s {
  fcontext_t fctx;
  void* data;
} transfer_t;

extern "C" transfer_t jump_fcontext(fcontext_t const to, void* vp);
extern "C" fcontext_t make_fcontext(void* sp, size_t size, void (*fn)(transfer_t));
extern "C" transfer_t ontop_fcontext(fcontext_t const to, void* vp, transfer_t(*fn)(transfer_t));

}

struct Stack {
  std::size_t size{ 0 };
  void* sp{ nullptr };
};

struct Unwind {};

enum State {
  STATE_READY = 0,
  STATE_RUNNING = 1,
  STATE_WAITING = 2,
  STATE_COMPUTE = 3,
  STATE_DONE = 4
};

enum Event {
  EVENT_CANCEL = 0,
  EVENT_READABLE = 1,
  EVENT_WRITABLE = 2,
  EVENT_TIMEOUT = 3,
  EVENT_JOIN = 4,
  EVENT_COMPUTE = 5,
  EVENT_COMPUTE_DONE = 6,
  EVENT_HUP = 7,
  EVENT_RWABLE = 8,
  EVENT_CONT = 9,
  EVENT_COND = 10,
};

enum {
  WAIT_READABLE = 0x1,
  WAIT_WRITABLE = 0x2
};

class Scheduler;
class Coroutine;

class Socket {
  friend class Scheduler;

public:
  Socket();
  Socket(uv_os_sock_t s);

  void Close();

  void SetDeadline(int timeout_secs);
  int GetDeadline();

  bool ListenByHost(const std::string& host, int port, int backlog = 1024);
  bool ListenByIp(const std::string& ip, int port, int backlog = 1024);
  uv_os_sock_t Accept();

  bool ConnectHost(const std::string& host, int port);
  bool ConnectIp(const std::string& ip, int port);

  int ReadSome(char* buf, int len);
  int WriteSome(const char* buf, int len);

  Event Wait(int flags);

protected:
  uv_os_sock_t s_;
  uv_poll_t poll_;
  int timeout_secs_{ 0 };
  Coroutine* coro_{ nullptr };
};

class Coroutine {
public:
  static Coroutine* Self();

  bool Create(Scheduler* sched,
              const std::function<void()>& fn,
              const std::function<void()>& exit_fn);
  void Destroy();

  void Resume();
  void Yield(State new_state);

  void Join(Coroutine* coro);
  void Cancel();
  void SetEvent(Event new_event);

  State GetState() const;
  Event GetEvent() const;
  Scheduler* GetScheduler() const;
  std::size_t GetId() const;

  void Nice();
  void Wait(long millisecs);
  void BeginCompute();
  void EndCompute();

  void SetTimeout(int seconds);
  void CheckTimeout();

private:
  context::fcontext_t ctx_{ nullptr };
  context::fcontext_t caller_{ nullptr };
  Stack stack_;
  std::function<void()> fn_;
  std::function<void()> exit_fn_;
  Scheduler* sched_{ nullptr };
  State state_{ STATE_READY };
  Event event_;
  int timeout_secs_{ 0 };
  Coroutine* joined_{ nullptr };
  std::size_t id_{ 0 };
};

typedef std::vector<Coroutine* > CoroutineList;

class Condition {
public:
  void Wait(Coroutine* coro) {
    waiting_.push_back(coro);
    coro->Yield(STATE_WAITING);
  }

  void NotifyOne() {
    if (waiting_.size() > 0) {
      Coroutine* coro = waiting_.back();
      waiting_.pop_back();
      coro->SetEvent(EVENT_COND);
    }
  }

  void NotifyAll() {
    for (auto it = waiting_.begin(); it != waiting_.end(); it++) {
      (*it)->SetEvent(EVENT_COND);
    }
    waiting_.clear();
  }

protected:
  CoroutineList waiting_;
};

class Scheduler {
public:
  static Scheduler* Get();

  Scheduler(bool is_default);
  ~Scheduler();

  std::size_t NextId();
  Stack AllocateStack();
  void DeallocateStack(Stack& stack);

  void AddCoroutine(Coroutine* coro); // for current thread
  void PostCoroutine(Coroutine* coro, bool is_compute = false); // for different thread
  void Wait(Coroutine* coro, long millisecs);
  void Wait(Coroutine* coro, Socket& s, int flags);
  void BeginCompute(Coroutine* coro);
  void Run();

  Coroutine* GetCurrent() const;
  uv_loop_t* GetLoop();

protected:
  void Pre();
  void Check();
  void Async();
  void Sweep();
  void RunCoros();
  void Cleanup(CoroutineList& cl);

protected:
  static std::size_t page_size_;
  static std::size_t min_stack_size_;
  static std::size_t max_stack_size_;
  static std::size_t default_stack_size_;
  bool is_default_;
  uvpp::Loop loop_;
  uvpp::Prepare pre_;
  uvpp::Check check_;
  uvpp::Async async_;
  uvpp::Timer sweep_timer_;
  Coroutine* current_{ nullptr };
  CoroutineList ready_;
  CoroutineList waiting_;
  std::mutex lock_;
  int outstanding_{ 0 };
  CoroutineList posted_;
  CoroutineList compute_done_;
};

#include "coros-inl.hpp"

}
