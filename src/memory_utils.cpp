#include "memory_utils.h"
#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>
#include <string>

namespace Utils {
namespace Memory {

size_t MemoryTracker::current_usage_ = 0;
size_t MemoryTracker::peak_usage_ = 0;

void MemoryTracker::add_allocation(size_t size) {
  current_usage_ += size;
  peak_usage_ = std::max(peak_usage_, current_usage_);
}

void MemoryTracker::remove_allocation(size_t size) {
  if (current_usage_ >= size) {
    current_usage_ -= size;
  }
}

std::string MemoryTracker::format_bytes(size_t bytes) {
  constexpr std::array<std::string_view, 4> units = {"B", "KB", "MB", "GB"};
  constexpr double KB = 1024.0;
  double size = static_cast<double>(bytes);
  size_t unit_index = 0;

  while (size >= KB && unit_index < 3) {
    size /= KB;
    unit_index++;
  }

  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2) << size << " " << units[unit_index];
  return oss.str();
}

std::vector<std::string_view> split_view(std::string_view input,
                                         char delimiter) {
  std::vector<std::string_view> result;
  result.reserve(8); // Reserve for common case

  size_t start = 0;
  size_t pos = 0;

  while ((pos = input.find(delimiter, start)) != std::string_view::npos) {
    if (pos > start) { // Skip empty tokens
      result.emplace_back(input.substr(start, pos - start));
    }
    start = pos + 1;
  }

  // Add the last token if it exists
  if (start < input.length()) {
    result.emplace_back(input.substr(start));
  }

  return result;
}

std::vector<std::string_view> split_view(std::string_view input,
                                         std::string_view delimiter) {
  std::vector<std::string_view> result;
  result.reserve(8); // Reserve for common case

  if (delimiter.empty()) {
    result.emplace_back(input);
    return result;
  }

  size_t start = 0;
  size_t pos = 0;

  while ((pos = input.find(delimiter, start)) != std::string_view::npos) {
    if (pos > start) { // Skip empty tokens
      result.emplace_back(input.substr(start, pos - start));
    }
    start = pos + delimiter.length();
  }

  // Add the last token if it exists
  if (start < input.length()) {
    result.emplace_back(input.substr(start));
  }

  return result;
}

} // namespace Memory
} // namespace Utils
