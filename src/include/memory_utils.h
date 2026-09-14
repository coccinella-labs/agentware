#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace Utils {
namespace Memory {

// String optimization utilities
class StringBuilder {
private:
  std::string buffer_;

public:
  explicit StringBuilder(size_t reserve_size = 1024) {
    buffer_.reserve(reserve_size);
  }

  StringBuilder &append(const std::string &str) {
    buffer_.append(str);
    return *this;
  }

  StringBuilder &append(char c) {
    buffer_.push_back(c);
    return *this;
  }

  std::string build() { return std::move(buffer_); }

  size_t size() const { return buffer_.size(); }

  const std::string &str() const { return buffer_; }
  void clear() { buffer_.clear(); }
  void reserve(size_t size) { buffer_.reserve(size); }
};

// Efficient string splitting using string_view
std::vector<std::string_view> split_view(std::string_view input,
                                         char delimiter);
std::vector<std::string_view> split_view(std::string_view input,
                                         std::string_view delimiter);

// Memory usage tracking
class MemoryTracker {
private:
  static size_t current_usage_;
  static size_t peak_usage_;

public:
  static void add_allocation(size_t size);
  static void remove_allocation(size_t size);
  static size_t get_current_usage() { return current_usage_; }
  static size_t get_peak_usage() { return peak_usage_; }
  static void reset_peak() { peak_usage_ = current_usage_; }
  static std::string format_bytes(size_t bytes);
};

// RAII memory tracker for automatic tracking
class ScopedMemoryTracker {
private:
  size_t size_;

public:
  explicit ScopedMemoryTracker(size_t size) : size_(size) {
    MemoryTracker::add_allocation(size_);
  }

  ~ScopedMemoryTracker() { MemoryTracker::remove_allocation(size_); }

  // Non-copyable, movable
  ScopedMemoryTracker(const ScopedMemoryTracker &) = delete;
  ScopedMemoryTracker &operator=(const ScopedMemoryTracker &) = delete;
  ScopedMemoryTracker(ScopedMemoryTracker &&) = default;
  ScopedMemoryTracker &operator=(ScopedMemoryTracker &&) = default;
};

} // namespace Memory
} // namespace Utils
