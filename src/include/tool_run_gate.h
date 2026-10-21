#pragma once

#include <atomic>
#include <memory>

namespace Core {

// Gate for the model tool-call loop: at most one tool handler may be in
// flight at a time.
//
// A handler that times out is detached and may still be running, so the
// gate stays closed until the handler itself returns. New tool calls are
// refused while the gate is held.
//
// This is a free-standing class rather than an Agent member so the
// acquire/release semantics can be unit tested directly. agent.h is
// deliberately not included by the test suite (see the note at the top of
// main_test.cpp), so anything tested at this boundary has to live outside
// the Agent class.
class ToolRunGate {
public:
  // Try to acquire the gate. Returns false when another handler holds it.
  bool try_acquire() {
    bool expected = false;
    return flag_->compare_exchange_strong(expected, true);
  }

  // Release the gate. Called by the handler when it finishes.
  void release() { flag_->store(false); }

  // True while a handler holds the gate.
  bool held() const { return flag_->load(); }

  // A copy of the shared flag, safe to hand to a thread that may outlive
  // this object.
  std::shared_ptr<std::atomic<bool>> handle() const { return flag_; }

private:
  std::shared_ptr<std::atomic<bool>> flag_ =
      std::make_shared<std::atomic<bool>>(false);
};

} // namespace Core
