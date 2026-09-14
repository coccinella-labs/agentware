#pragma once
#include <map>
#include <string>
#include <vector>

namespace Services {
struct CheckpointInfo {
  std::string id{};
  std::string name{};
  std::string timestamp{};
  std::string description{};
  std::vector<std::string> backed_up_files{};
  size_t total_size{0};
};

struct RestoreOptions {
  bool create_backup_before_restore = true;
  bool restore_memory = true;
  bool restore_files = true;
  std::vector<std::string> specific_files; // If empty, restore all
};

class CheckpointService {
private:
  static std::string get_checkpoints_directory();
  static std::string generate_checkpoint_id();
  static std::string get_timestamp();
  static void ensure_checkpoints_directory();
  static std::vector<std::string>
  get_project_files(const std::string &directory);
  static void backup_file(const std::string &source_path,
                          const std::string &backup_path);
  static void restore_file(const std::string &backup_path,
                           const std::string &target_path);

public:
  // Create checkpoint
  static std::string create_checkpoint(const std::string &name,
                                       const std::string &description = "",
                                       const std::string &directory = ".");

  // List checkpoints
  static std::vector<CheckpointInfo> list_checkpoints();

  // Get checkpoint info
  static CheckpointInfo get_checkpoint_info(const std::string &checkpoint_id);

  // Restore from checkpoint
  static bool
  restore_checkpoint(const std::string &checkpoint_id,
                     const RestoreOptions &options = RestoreOptions{});

  // Delete checkpoint
  static bool delete_checkpoint(const std::string &checkpoint_id);

  // Get checkpoint size
  static size_t get_checkpoint_size(const std::string &checkpoint_id);

  // Cleanup old checkpoints (keep only N most recent)
  static void cleanup_old_checkpoints(size_t keep_count = 10);

  // Export checkpoint to archive
  static bool export_checkpoint(const std::string &checkpoint_id,
                                const std::string &export_path);

  // Import checkpoint from archive
  static std::string import_checkpoint(const std::string &archive_path);
};
} // namespace Services
