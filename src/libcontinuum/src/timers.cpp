#include <cassert>
#include <chrono>
#include <queue>
#include <thread>
#include <utility>

#include <continuum/timers.hpp>
#include <continuum/util.hpp>

using std::pair;
// using std::chrono::high_resolution_clock;

namespace continuum {

Timer::Timer(
    std::chrono::time_point<std::chrono::high_resolution_clock> deadline,
    folly::Promise<folly::Unit> completion_promise)
    : deadline_(deadline), completion_promise_(std::move(completion_promise)) {}

bool Timer::operator<(const Timer &rhs) const {
  return deadline_ < rhs.deadline_;
}

bool Timer::operator>(const Timer &rhs) const {
  return deadline_ > rhs.deadline_;
}

bool Timer::operator<=(const Timer &rhs) const {
  return deadline_ <= rhs.deadline_;
}

bool Timer::operator>=(const Timer &rhs) const {
  return deadline_ >= rhs.deadline_;
}

void Timer::expire() { completion_promise_.setValue(); }

}  // namespace continuum
