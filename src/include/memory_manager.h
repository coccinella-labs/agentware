#pragma once
#include "memory_utils.h"
#include <chrono>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Data {
struct MemoryEntry {
  std::string content{};
  std::string timestamp{};
  std::string type{}; // "interaction", "fact", "preference", etc.
};

class MemoryManager {
private:
  std::string memory_file_;
  std::string global_memory_file_;

  // Memory optimization constants
  static constexpr size_t max_memory_size = 50 * 1024 * 1024; // 50MB
  static constexpr size_t max_entries = 10000; // Maximum number of entries
  static constexpr size_t recent_entries_limit =
      100;                                      // Lazy load recent entries only
  static constexpr size_t lru_cache_size = 500; // LRU cache for parsed data

  // LRU Cache for parsed entries
  mutable std::unordered_map<size_t, MemoryEntry> entry_cache_;
  mutable std::deque<size_t> lru_order_; // Track access order for LRU
  mutable size_t current_memory_usage_ = 0;
  mutable std::chrono::steady_clock::time_point last_file_check_;
  mutable size_t cached_file_size_ = 0;

  // Index for efficient searching (O(1) instead of O(n))
  mutable std::unordered_map<std::string, std::vector<size_t>> content_index_;
  mutable bool index_dirty_ = true;

  void ensure_memory_directory() const;
  std::string get_timestamp() const;
  std::string get_global_memory_path() const;
  void evict_old_entries() const;
  size_t calculate_entry_size(const MemoryEntry &entry) const;

  // LRU Cache management
  void update_lru_access(size_t entry_id) const;
  void evict_lru_entries() const;
  bool is_cache_valid() const;
  void rebuild_index() const;

  // Lazy loading helpers
  std::vector<MemoryEntry> load_recent_entries_only() const;
  MemoryEntry parse_entry_from_line(std::string_view line,
                                    size_t line_number) const;

public:
  MemoryManager(const std::string &filename = "data/memory.txt");

  // Basic memory operations (optimized)
  std::vector<std::string>
  load_memory() const; // Legacy - use load_recent_entries_only()
  void save_interaction(const std::string &user_input,
                        const std::string &response);
  void clear_memory();
  std::string get_context_string() const; // Uses lazy loading
  size_t get_memory_size() const;         // Cached file size check

  // Enhanced memory operations
  void save_fact(const std::string &fact);
  void save_preference(const std::string &preference);
  std::vector<MemoryEntry> load_structured_memory() const;
  std::string get_facts_context() const;
  std::string get_preferences_context() const;

  // Global memory (persistent across sessions)
  void save_global_fact(const std::string &fact);
  std::string get_global_context() const;
  void clear_global_memory();

  // Memory search and management (optimized with indexing)
  std::vector<MemoryEntry>
  search_memory(const std::string &query) const; // O(1) indexed search
  std::vector<MemoryEntry> search_memory_by_type(const std::string &type) const;
  void export_memory(const std::string &filename) const;
  bool import_memory(const std::string &filename);

  // Memory statistics and monitoring
  size_t get_cache_hit_ratio() const;
  void print_memory_stats() const;

  // Conversation state management
  void save_conversation_state(const std::string &tag);
  bool resume_conversation_state(const std::string &tag);
  std::vector<std::string> list_conversation_states() const;
  void delete_conversation_state(const std::string &tag);

  // Context compression
  void compress_memory(const std::string &compressed_summary);
  std::string get_compressible_context() const;
};
} // namespace Data
