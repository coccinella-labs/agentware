#include "memory_manager.h"
#include "file_service.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>
#include <unordered_set>

namespace Data {

MemoryManager::MemoryManager(const std::string &filename)
    : memory_file_(filename),
      last_file_check_(std::chrono::steady_clock::now()), cached_file_size_(0) {
  ensure_memory_directory();
  global_memory_file_ = get_global_memory_path();

  // Initialize cache
  entry_cache_.reserve(lru_cache_size);
}

void MemoryManager::ensure_memory_directory() const {
  try {
    // Ensure directory exists if a path is provided
    std::filesystem::path file_path(memory_file_);
    if (file_path.has_parent_path()) {
      std::filesystem::create_directories(file_path.parent_path());
    }

    // Ensure global memory directory exists
    std::filesystem::path global_path(get_global_memory_path());
    if (global_path.has_parent_path()) {
      std::filesystem::create_directories(global_path.parent_path());
    }
  } catch (const std::filesystem::filesystem_error &e) {
    throw std::runtime_error("Failed to create memory directory: " +
                             std::string(e.what()));
  }
}

std::string MemoryManager::get_timestamp() const {
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  std::stringstream ss;
  ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
  return ss.str();
}

std::string MemoryManager::get_global_memory_path() const {
  // Get home directory
  const char *home = std::getenv("HOME");
  if (!home) {
    home = std::getenv("USERPROFILE"); // Windows
  }
  if (!home) {
    return "global_memory.md"; // Fallback to current directory
  }

  std::filesystem::path global_path =
      std::filesystem::path(home) / ".agentware" / "AGENTWARE.md";
  return global_path.string();
}

std::vector<std::string> MemoryManager::load_memory() const {
  // Legacy method - convert from optimized load_recent_entries_only()
  std::vector<MemoryEntry> entries = load_recent_entries_only();
  std::vector<std::string> memory;
  memory.reserve(entries.size());

  for (const auto &entry : entries) {
    memory.push_back(entry.content);
  }

  return memory;
}

void MemoryManager::save_interaction(const std::string &user_input,
                                     const std::string &response) {
  std::ofstream file(memory_file_, std::ios::out | std::ios::app);
  if (!file.is_open()) {
    throw std::runtime_error("Unable to open memory file for writing: " +
                             memory_file_);
  }

  file << "User: " << user_input << "\n";
  file << "Assistant: " << response << "\n";
  file << "---\n"; // Separator for better readability
}

void MemoryManager::clear_memory() {
  std::ofstream file(memory_file_, std::ios::out | std::ios::trunc);
  if (!file.is_open()) {
    throw std::runtime_error("Unable to open memory file for clearing: " +
                             memory_file_);
  }
  // File is now empty
}

std::string MemoryManager::get_context_string() const {
  // Use optimized recent entries loading with StringBuilder
  std::vector<MemoryEntry> recent_entries = load_recent_entries_only();

  // Use StringBuilder for efficient string concatenation
  Utils::Memory::StringBuilder context_builder(4096);

  // Limit context to last 20 interactions
  constexpr size_t max_interactions = 20;
  size_t interaction_count = 0;

  // Process entries in reverse order to get most recent first
  for (auto it = recent_entries.rbegin();
       it != recent_entries.rend() && interaction_count < max_interactions;
       ++it) {
    if (it->type == "interaction") {
      context_builder.append(it->content).append("\n");
      interaction_count++;
    }
  }

  // Add global context efficiently
  std::string global_context = get_global_context();
  if (!global_context.empty()) {
    std::string context_content = std::move(context_builder).build();
    Utils::Memory::StringBuilder final_builder(global_context.size() +
                                               context_content.size() + 50);
    final_builder.append(global_context)
        .append("\n\nRecent interactions:\n")
        .append(context_content);
    return std::move(final_builder).build();
  }

  return std::move(context_builder).build();
}

size_t MemoryManager::get_memory_size() const {
  // Use cached file size instead of loading entire file
  if (!Services::FileService::file_exists(memory_file_)) {
    return 0;
  }

  try {
    // Update cached size if needed
    auto now = std::chrono::steady_clock::now();
    if (now - last_file_check_ > std::chrono::seconds(5)) {
      cached_file_size_ = std::filesystem::file_size(memory_file_);
      last_file_check_ = now;
    }

    // Approximate line count based on average line size (80 chars)
    return cached_file_size_ / 80;
  } catch (const std::exception &) {
    return load_recent_entries_only().size(); // Fallback
  }
}

void MemoryManager::save_fact(const std::string &fact) {
  std::ofstream file(memory_file_, std::ios::out | std::ios::app);
  if (!file.is_open()) {
    throw std::runtime_error("Unable to open memory file for writing: " +
                             memory_file_);
  }

  file << "Fact [" << get_timestamp() << "]: " << fact << "\n";
}

void MemoryManager::save_preference(const std::string &preference) {
  std::ofstream file(memory_file_, std::ios::out | std::ios::app);
  if (!file.is_open()) {
    throw std::runtime_error("Unable to open memory file for writing: " +
                             memory_file_);
  }

  file << "Preference [" << get_timestamp() << "]: " << preference << "\n";
}

std::vector<MemoryEntry> MemoryManager::load_structured_memory() const {
  // Use optimized recent entries loading - already parsed
  return load_recent_entries_only();
}

std::string MemoryManager::get_facts_context() const {
  std::vector<MemoryEntry> entries = load_structured_memory();
  std::string facts_context;

  for (const auto &entry : entries) {
    if (entry.type == "fact") {
      facts_context += "- " + entry.content + "\n";
    }
  }

  return facts_context.empty() ? "" : "Known facts:\n" + facts_context;
}

std::string MemoryManager::get_preferences_context() const {
  std::vector<MemoryEntry> entries = load_structured_memory();
  std::string prefs_context;

  for (const auto &entry : entries) {
    if (entry.type == "preference") {
      prefs_context += "- " + entry.content + "\n";
    }
  }

  return prefs_context.empty() ? "" : "User preferences:\n" + prefs_context;
}

void MemoryManager::save_global_fact(const std::string &fact) {
  try {
    std::string content;

    // Read existing content
    if (Services::FileService::file_exists(global_memory_file_)) {
      std::ifstream file(global_memory_file_);
      if (file.is_open()) {
        std::ostringstream buffer;
        buffer << file.rdbuf();
        content = buffer.str();
      }
    }

    // Find or create the memories section
    const std::string header = "## Agentware Added Memories";
    size_t header_pos = content.find(header);

    if (header_pos == std::string::npos) {
      // Add header and fact
      if (!content.empty() && content.back() != '\n') {
        content += "\n";
      }
      content += "\n" + header + "\n- " + fact + "\n";
    } else {
      // Find insertion point after header
      size_t insert_pos = header_pos + header.length();
      size_t next_header = content.find("\n## ", insert_pos);

      if (next_header == std::string::npos) {
        // Insert at end
        content += "\n- " + fact;
      } else {
        // Insert before next header
        content.insert(next_header, "\n- " + fact);
      }
    }

    // Write back to file
    std::ofstream file(global_memory_file_);
    if (file.is_open()) {
      file << content;
    }

  } catch (const std::exception &) {
    throw std::runtime_error("Failed to save global fact");
  }
}

std::string MemoryManager::get_global_context() const {
  if (!Services::FileService::file_exists(global_memory_file_)) {
    return "";
  }

  try {
    std::ifstream file(global_memory_file_);
    if (!file.is_open()) {
      return "";
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    // Extract memories section
    const std::string header = "## Agentware Added Memories";
    size_t header_pos = content.find(header);

    if (header_pos == std::string::npos) {
      return "";
    }

    size_t start_pos = header_pos + header.length();
    size_t end_pos = content.find("\n## ", start_pos);

    if (end_pos == std::string::npos) {
      end_pos = content.length();
    }

    std::string memories = content.substr(start_pos, end_pos - start_pos);
    return memories.empty() ? "" : "Global memories:\n" + memories;

  } catch (const std::exception &) {
    return "";
  }
}

void MemoryManager::clear_global_memory() {
  try {
    if (Services::FileService::file_exists(global_memory_file_)) {
      std::filesystem::remove(global_memory_file_);
    }
  } catch (const std::exception &) {
    throw std::runtime_error("Failed to clear global memory");
  }
}

std::vector<MemoryEntry>
MemoryManager::search_memory(const std::string &query) const {
  rebuild_index();

  std::vector<MemoryEntry> results;
  std::unordered_set<size_t> result_indices;

  // Convert query to lowercase for case-insensitive search
  std::string lower_query(query);
  std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(),
                 ::tolower);

  // Tokenize query and search index (O(1) lookup per word)
  std::istringstream iss(lower_query);
  std::string word;
  while (iss >> word) {
    // Remove punctuation
    word.erase(std::remove_if(word.begin(), word.end(), ::ispunct), word.end());

    if (word.length() > 2) {
      auto it = content_index_.find(word);
      if (it != content_index_.end()) {
        for (size_t idx : it->second) {
          result_indices.insert(idx);
        }
      }
    }
  }

  // If no indexed results, fall back to linear search on recent entries
  if (result_indices.empty()) {
    std::vector<MemoryEntry> recent_entries = load_recent_entries_only();
    for (const auto &entry : recent_entries) {
      std::string lower_content = entry.content;
      std::transform(lower_content.begin(), lower_content.end(),
                     lower_content.begin(), ::tolower);

      if (lower_content.find(lower_query) != std::string::npos) {
        results.push_back(entry);
      }
    }
  } else {
    // Convert indices to entries
    std::vector<MemoryEntry> recent_entries = load_recent_entries_only();
    results.reserve(result_indices.size());

    for (size_t idx : result_indices) {
      if (idx < recent_entries.size()) {
        results.push_back(recent_entries[idx]);
      }
    }
  }

  return results;
}

void MemoryManager::export_memory(const std::string &filename) const {
  std::vector<MemoryEntry> entries = load_structured_memory();

  std::ofstream file(filename);
  if (!file.is_open()) {
    throw std::runtime_error("Unable to open export file: " + filename);
  }

  file << "# Agentware Memory Export\n\n";
  file << "Export Date: " << get_timestamp() << "\n\n";

  for (const auto &entry : entries) {
    file << "## " << entry.type << " [" << entry.timestamp << "]\n";
    file << entry.content << "\n\n";
  }
}

bool MemoryManager::import_memory(const std::string &filename) {
  if (!Services::FileService::file_exists(filename)) {
    return false;
  }

  try {
    std::ifstream file(filename);
    if (!file.is_open()) {
      return false;
    }

    std::string line;
    while (std::getline(file, line)) {
      if (!line.empty() && line != "# Agentware Memory Export" &&
          line.find("Export Date:") == std::string::npos) {

        std::ofstream memory_file(memory_file_, std::ios::out | std::ios::app);
        if (memory_file.is_open()) {
          memory_file << line << "\n";
        }
      }
    }

    return true;
  } catch (const std::exception &) {
    return false;
  }
}

void MemoryManager::save_conversation_state(const std::string &tag) {
  try {
    // Create conversations directory
    std::filesystem::path conv_dir =
        std::filesystem::path(memory_file_).parent_path() / "conversations";
    std::filesystem::create_directories(conv_dir);

    // Save current memory to tagged file
    std::filesystem::path conv_file = conv_dir / (tag + ".txt");
    std::filesystem::copy_file(
        memory_file_, conv_file,
        std::filesystem::copy_options::overwrite_existing);

  } catch (const std::exception &) {
    throw std::runtime_error("Failed to save conversation state");
  }
}

bool MemoryManager::resume_conversation_state(const std::string &tag) {
  try {
    std::filesystem::path conv_dir =
        std::filesystem::path(memory_file_).parent_path() / "conversations";
    std::filesystem::path conv_file = conv_dir / (tag + ".txt");

    if (!std::filesystem::exists(conv_file)) {
      return false;
    }

    // Replace current memory with saved state
    std::filesystem::copy_file(
        conv_file, memory_file_,
        std::filesystem::copy_options::overwrite_existing);
    return true;

  } catch (const std::exception &) {
    return false;
  }
}

std::vector<std::string> MemoryManager::list_conversation_states() const {
  std::vector<std::string> conversations;

  try {
    std::filesystem::path conv_dir =
        std::filesystem::path(memory_file_).parent_path() / "conversations";

    if (!std::filesystem::exists(conv_dir)) {
      return conversations;
    }

    for (const auto &entry : std::filesystem::directory_iterator(conv_dir)) {
      if (entry.is_regular_file() && entry.path().extension() == ".txt") {
        conversations.push_back(entry.path().stem().string());
      }
    }

    std::sort(conversations.begin(), conversations.end());

  } catch (const std::exception &) {
    // Return empty vector on error
  }

  return conversations;
}

void MemoryManager::delete_conversation_state(const std::string &tag) {
  try {
    std::filesystem::path conv_dir =
        std::filesystem::path(memory_file_).parent_path() / "conversations";
    std::filesystem::path conv_file = conv_dir / (tag + ".txt");

    if (std::filesystem::exists(conv_file)) {
      std::filesystem::remove(conv_file);
    }

  } catch (const std::exception &) {
    throw std::runtime_error("Failed to delete conversation state");
  }
}

std::string MemoryManager::get_compressible_context() const {
  std::vector<MemoryEntry> entries = load_structured_memory();

  // Only compress interaction entries, preserve facts and preferences
  std::string compressible_content;
  int interaction_count = 0;

  for (const auto &entry : entries) {
    if (entry.type == "interaction") {
      compressible_content +=
          "[" + entry.timestamp + "] " + entry.content + "\n";
      interaction_count++;
    }
  }

  if (interaction_count < 5) {
    return ""; // Not enough content to compress
  }

  return compressible_content;
}

void MemoryManager::compress_memory(const std::string &compressed_summary) {
  try {
    // Load current memory entries
    std::vector<MemoryEntry> entries = load_structured_memory();

    // Create backup before compression
    std::string backup_file = memory_file_ + ".backup." + get_timestamp();
    std::filesystem::copy_file(memory_file_, backup_file);

    // Clear current memory file
    std::ofstream file(memory_file_, std::ios::trunc);
    if (!file.is_open()) {
      throw std::runtime_error("Could not open memory file for compression");
    }

    // Write compressed summary as a single entry
    file << "[" << get_timestamp()
         << "] COMPRESSED_SUMMARY: " << compressed_summary << std::endl;

    // Preserve recent interactions (last 3), facts, and preferences
    int recent_interactions = 0;
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
      const auto &entry = *it;

      if (entry.type == "interaction" && recent_interactions < 3) {
        file << "[" << entry.timestamp << "] " << entry.content << std::endl;
        recent_interactions++;
      } else if (entry.type == "fact" || entry.type == "preference") {
        file << "[" << entry.timestamp << "] " << entry.type << ": "
             << entry.content << std::endl;
      }
    }

    file.close();

    // Clear cache after compression
    entry_cache_.clear();
    lru_order_.clear();
    content_index_.clear();
    index_dirty_ = true;
    current_memory_usage_ = 0;

  } catch (const std::exception &) {
    throw std::runtime_error("Failed to compress memory");
  }
}

// ============================================================================
// MEMORY OPTIMIZATION IMPLEMENTATIONS
// ============================================================================

bool MemoryManager::is_cache_valid() const {
  if (!Services::FileService::file_exists(memory_file_)) {
    return entry_cache_.empty();
  }

  // Check if file has been modified since last cache
  auto now = std::chrono::steady_clock::now();
  if (now - last_file_check_ >
      std::chrono::seconds(5)) { // Check every 5 seconds
    try {
      size_t current_size = std::filesystem::file_size(memory_file_);
      last_file_check_ = now;

      if (current_size != cached_file_size_) {
        cached_file_size_ = current_size;
        return false; // Cache invalid
      }
    } catch (const std::exception &) {
      return false;
    }
  }

  return true;
}

void MemoryManager::update_lru_access(size_t entry_id) const {
  // Remove from current position
  auto it = std::find(lru_order_.begin(), lru_order_.end(), entry_id);
  if (it != lru_order_.end()) {
    lru_order_.erase(it);
  }

  // Add to front (most recently used)
  lru_order_.push_front(entry_id);
}

void MemoryManager::evict_lru_entries() const {
  while (entry_cache_.size() > lru_cache_size && !lru_order_.empty()) {
    size_t lru_id = lru_order_.back();
    lru_order_.pop_back();

    auto it = entry_cache_.find(lru_id);
    if (it != entry_cache_.end()) {
      current_memory_usage_ -= calculate_entry_size(it->second);
      entry_cache_.erase(it);
    }
  }
}

MemoryEntry
MemoryManager::parse_entry_from_line(std::string_view line,
                                     size_t /* line_number */) const {
  MemoryEntry entry;
  entry.content =
      std::string(line); // Convert string_view to string only when needed
  entry.timestamp = get_timestamp(); // Default timestamp
  entry.type = "interaction";        // Default type

  // Use string_view for efficient parsing - avoid substr() copies
  if (line.starts_with("Fact [")) {
    entry.type = "fact";
    size_t bracket_end = line.find("]: ");
    if (bracket_end != std::string_view::npos) {
      entry.content = std::string(line.substr(bracket_end + 3));
      // Extract timestamp using string_view
      size_t ts_start = 5; // After "Fact ["
      size_t ts_end = line.find(']');
      if (ts_end != std::string_view::npos && ts_end > ts_start) {
        entry.timestamp = std::string(line.substr(ts_start, ts_end - ts_start));
      }
    }
  } else if (line.starts_with("Preference [")) {
    entry.type = "preference";
    size_t bracket_end = line.find("]: ");
    if (bracket_end != std::string_view::npos) {
      entry.content = std::string(line.substr(bracket_end + 3));
      // Extract timestamp using string_view
      size_t ts_start = 11; // After "Preference ["
      size_t ts_end = line.find(']');
      if (ts_end != std::string_view::npos && ts_end > ts_start) {
        entry.timestamp = std::string(line.substr(ts_start, ts_end - ts_start));
      }
    }
  }

  return entry;
}

std::vector<MemoryEntry> MemoryManager::load_recent_entries_only() const {
  std::vector<MemoryEntry> entries;

  if (!Services::FileService::file_exists(memory_file_)) {
    return entries;
  }

  // Check cache validity first
  if (!is_cache_valid()) {
    entry_cache_.clear();
    lru_order_.clear();
    index_dirty_ = true;
    current_memory_usage_ = 0;
  }

  std::ifstream file(memory_file_, std::ios::in);
  if (!file.is_open()) {
    return entries;
  }

  std::string line;
  size_t line_number = 0;
  size_t total_lines = 0;

  // First pass: count total lines (for recent entries calculation)
  while (std::getline(file, line)) {
    if (!line.empty()) {
      total_lines++;
    }
  }

  // Reset file stream
  file.clear();
  file.seekg(0, std::ios::beg);

  // Calculate start position for recent entries
  size_t start_line = total_lines > recent_entries_limit
                          ? total_lines - recent_entries_limit
                          : 0;

  entries.reserve(std::min(total_lines, recent_entries_limit));

  // Second pass: load only recent entries
  line_number = 0;
  while (std::getline(file, line)) {
    if (line.empty())
      continue;

    if (line_number >= start_line) {
      // Check cache first
      auto cache_it = entry_cache_.find(line_number);
      if (cache_it != entry_cache_.end()) {
        entries.push_back(cache_it->second);
        update_lru_access(line_number);
      } else {
        // Parse new entry using string_view for efficiency
        MemoryEntry entry = parse_entry_from_line(line, line_number);
        entries.push_back(entry);

        // Add to cache if within limits
        if (entry_cache_.size() < lru_cache_size) {
          entry_cache_[line_number] = entry;
          update_lru_access(line_number);
          current_memory_usage_ += calculate_entry_size(entry);
        }
      }
    }
    line_number++;
  }

  // Evict old entries if cache is full
  evict_lru_entries();

  return entries;
}

void MemoryManager::rebuild_index() const {
  if (!index_dirty_)
    return;

  content_index_.clear();

  // Load recent entries and build index
  std::vector<MemoryEntry> entries = load_recent_entries_only();

  for (size_t i = 0; i < entries.size(); ++i) {
    const auto &entry = entries[i];

    // Tokenize content for indexing (simple word-based)
    std::istringstream iss(entry.content);
    std::string word;
    while (iss >> word) {
      // Convert to lowercase for case-insensitive search
      std::transform(word.begin(), word.end(), word.begin(), ::tolower);
      // Remove punctuation
      word.erase(std::remove_if(word.begin(), word.end(), ::ispunct),
                 word.end());

      if (word.length() > 2) { // Index words longer than 2 characters
        content_index_[word].push_back(i);
      }
    }

    // Also index by type
    content_index_[entry.type].push_back(i);
  }

  index_dirty_ = false;
}

std::vector<MemoryEntry>
MemoryManager::search_memory_by_type(const std::string &type) const {
  rebuild_index();

  std::vector<MemoryEntry> results;
  auto it = content_index_.find(type);
  if (it != content_index_.end()) {
    std::vector<MemoryEntry> recent_entries = load_recent_entries_only();
    results.reserve(it->second.size());

    for (size_t idx : it->second) {
      if (idx < recent_entries.size()) {
        results.push_back(recent_entries[idx]);
      }
    }
  }

  return results;
}

size_t MemoryManager::get_cache_hit_ratio() const {
  if (lru_order_.empty())
    return 0;

  // Simple approximation: cache hits vs total accesses
  return (entry_cache_.size() * 100) / std::max(size_t(1), lru_order_.size());
}

void MemoryManager::print_memory_stats() const {
  std::cout << "=== Memory Manager Statistics ===" << std::endl;
  std::cout << "Cache entries: " << entry_cache_.size() << "/" << lru_cache_size
            << std::endl;
  std::cout << "Memory usage: " << (current_memory_usage_ / 1024) << " KB"
            << std::endl;
  std::cout << "Cache hit ratio: " << get_cache_hit_ratio() << "%" << std::endl;
  std::cout << "Index entries: " << content_index_.size() << std::endl;
  std::cout << "File size: " << (cached_file_size_ / 1024) << " KB"
            << std::endl;
}

// Helper methods implementation
void MemoryManager::evict_old_entries() const {
  // This method can be used for automatic memory management
  // Currently handled by evict_lru_entries()
}

size_t MemoryManager::calculate_entry_size(const MemoryEntry &entry) const {
  return entry.content.size() + entry.timestamp.size() + entry.type.size() +
         sizeof(MemoryEntry); // Approximate size including object overhead
}

} // namespace Data
