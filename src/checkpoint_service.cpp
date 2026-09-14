#include "checkpoint_service.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <string_view>

namespace Services {

std::string CheckpointService::get_checkpoints_directory() {
  return "data/checkpoints";
}

std::string CheckpointService::generate_checkpoint_id() {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, 15);

  std::string id;
  constexpr std::string_view hex_chars = "0123456789abcdef";
  for (size_t i = 0; i < 8; ++i) {
    id += hex_chars[dis(gen)];
  }
  return id;
}

std::string CheckpointService::get_timestamp() {
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  std::stringstream ss;
  ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
  return ss.str();
}

void CheckpointService::ensure_checkpoints_directory() {
  std::filesystem::create_directories(get_checkpoints_directory());
}

std::vector<std::string>
CheckpointService::get_project_files(const std::string &directory) {
  std::vector<std::string> files;

  try {
    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(directory)) {
      if (entry.is_regular_file()) {
        std::string path = entry.path().string();

        // Skip certain directories and file types
        if (path.find("/.git/") != std::string::npos ||
            path.find("/build/") != std::string::npos ||
            path.find("/node_modules/") != std::string::npos ||
            path.find("/target/") != std::string::npos ||
            path.find("/.cache/") != std::string::npos ||
            path.find("/data/checkpoints/") != std::string::npos) {
          continue;
        }

        // Skip binary files and large files
        if (entry.file_size() > 10 * 1024 * 1024) { // 10MB limit
          continue;
        }

        files.push_back(path);
      }
    }
  } catch (const std::exception &e) {
    std::cerr << "Error scanning project files: " << e.what() << std::endl;
  }

  return files;
}

void CheckpointService::backup_file(const std::string &source_path,
                                    const std::string &backup_path) {
  try {
    std::filesystem::create_directories(
        std::filesystem::path(backup_path).parent_path());
    std::filesystem::copy_file(
        source_path, backup_path,
        std::filesystem::copy_options::overwrite_existing);
  } catch (const std::exception &e) {
    throw std::runtime_error("Failed to backup file " + source_path + ": " +
                             e.what());
  }
}

void CheckpointService::restore_file(const std::string &backup_path,
                                     const std::string &target_path) {
  try {
    std::filesystem::create_directories(
        std::filesystem::path(target_path).parent_path());
    std::filesystem::copy_file(
        backup_path, target_path,
        std::filesystem::copy_options::overwrite_existing);
  } catch (const std::exception &e) {
    throw std::runtime_error("Failed to restore file " + target_path + ": " +
                             e.what());
  }
}

std::string CheckpointService::create_checkpoint(const std::string &name,
                                                 const std::string &description,
                                                 const std::string &directory) {
  try {
    ensure_checkpoints_directory();

    std::string checkpoint_id = generate_checkpoint_id();
    std::string checkpoint_dir =
        get_checkpoints_directory() + "/" + checkpoint_id;
    std::filesystem::create_directories(checkpoint_dir);

    // Get list of files to backup
    std::vector<std::string> project_files = get_project_files(directory);

    // Create checkpoint metadata
    nlohmann::json metadata;
    metadata["id"] = checkpoint_id;
    metadata["name"] = name;
    metadata["description"] = description;
    metadata["timestamp"] = get_timestamp();
    metadata["source_directory"] =
        std::filesystem::absolute(directory).string();
    metadata["files"] = nlohmann::json::array();

    size_t total_size = 0;

    // Backup files
    for (const auto &file_path : project_files) {
      try {
        std::string relative_path =
            std::filesystem::relative(file_path, directory).string();
        std::string backup_path = checkpoint_dir + "/files/" + relative_path;

        backup_file(file_path, backup_path);

        size_t file_size = std::filesystem::file_size(file_path);
        total_size += file_size;

        nlohmann::json file_info;
        file_info["original_path"] = file_path;
        file_info["relative_path"] = relative_path;
        file_info["backup_path"] = backup_path;
        file_info["size"] = file_size;

        metadata["files"].push_back(file_info);

      } catch (const std::exception &e) {
        std::cerr << "Warning: Failed to backup file " << file_path << ": "
                  << e.what() << std::endl;
      }
    }

    metadata["total_size"] = total_size;
    metadata["file_count"] = metadata["files"].size();

    // Also backup memory state if it exists
    std::string memory_file = "data/memory.txt";
    if (std::filesystem::exists(memory_file)) {
      try {
        std::string memory_backup = checkpoint_dir + "/memory.txt";
        backup_file(memory_file, memory_backup);
        metadata["has_memory_backup"] = true;
      } catch (const std::exception &e) {
        std::cerr << "Warning: Failed to backup memory: " << e.what()
                  << std::endl;
        metadata["has_memory_backup"] = false;
      }
    } else {
      metadata["has_memory_backup"] = false;
    }

    // Save metadata
    std::string metadata_file = checkpoint_dir + "/metadata.json";
    std::ofstream meta_stream(metadata_file);
    meta_stream << metadata.dump(2);
    meta_stream.close();

    return checkpoint_id;

  } catch (const std::exception &e) {
    throw std::runtime_error("Failed to create checkpoint: " +
                             std::string(e.what()));
  }
}

std::vector<CheckpointInfo> CheckpointService::list_checkpoints() {
  std::vector<CheckpointInfo> checkpoints;

  try {
    std::string checkpoints_dir = get_checkpoints_directory();
    if (!std::filesystem::exists(checkpoints_dir)) {
      return checkpoints;
    }

    for (const auto &entry :
         std::filesystem::directory_iterator(checkpoints_dir)) {
      if (entry.is_directory()) {
        std::string metadata_file = entry.path().string() + "/metadata.json";
        if (std::filesystem::exists(metadata_file)) {
          try {
            std::ifstream file(metadata_file);
            nlohmann::json metadata;
            file >> metadata;

            CheckpointInfo info;
            info.id = metadata["id"];
            info.name = metadata["name"];
            info.timestamp = metadata["timestamp"];
            info.description = metadata.value("description", "");
            info.total_size = metadata.value("total_size", 0);

            for (const auto &file_info : metadata["files"]) {
              info.backed_up_files.push_back(file_info["relative_path"]);
            }

            checkpoints.push_back(info);

          } catch (const std::exception &e) {
            std::cerr << "Warning: Failed to read checkpoint metadata: "
                      << e.what() << std::endl;
          }
        }
      }
    }

    // Sort by timestamp (newest first)
    std::sort(checkpoints.begin(), checkpoints.end(),
              [](const CheckpointInfo &a, const CheckpointInfo &b) {
                return a.timestamp > b.timestamp;
              });

  } catch (const std::exception &e) {
    std::cerr << "Error listing checkpoints: " << e.what() << std::endl;
  }

  return checkpoints;
}

CheckpointInfo
CheckpointService::get_checkpoint_info(const std::string &checkpoint_id) {
  CheckpointInfo info;

  try {
    std::string metadata_file =
        get_checkpoints_directory() + "/" + checkpoint_id + "/metadata.json";
    if (!std::filesystem::exists(metadata_file)) {
      throw std::runtime_error("Checkpoint not found: " + checkpoint_id);
    }

    std::ifstream file(metadata_file);
    nlohmann::json metadata;
    file >> metadata;

    info.id = metadata["id"];
    info.name = metadata["name"];
    info.timestamp = metadata["timestamp"];
    info.description = metadata.value("description", "");
    info.total_size = metadata.value("total_size", 0);

    for (const auto &file_info : metadata["files"]) {
      info.backed_up_files.push_back(file_info["relative_path"]);
    }

  } catch (const std::exception &e) {
    throw std::runtime_error("Failed to get checkpoint info: " +
                             std::string(e.what()));
  }

  return info;
}

bool CheckpointService::restore_checkpoint(const std::string &checkpoint_id,
                                           const RestoreOptions &options) {
  try {
    std::string checkpoint_dir =
        get_checkpoints_directory() + "/" + checkpoint_id;
    std::string metadata_file = checkpoint_dir + "/metadata.json";

    if (!std::filesystem::exists(metadata_file)) {
      throw std::runtime_error("Checkpoint not found: " + checkpoint_id);
    }

    std::ifstream file(metadata_file);
    nlohmann::json metadata;
    file >> metadata;

    // Create backup before restore if requested
    if (options.create_backup_before_restore) {
      std::string backup_name = "pre-restore-" + checkpoint_id;
      std::string backup_description =
          "Automatic backup before restoring checkpoint " + checkpoint_id;
      create_checkpoint(backup_name, backup_description);
    }

    // Restore files
    if (options.restore_files) {
      for (const auto &file_info : metadata["files"]) {
        std::string relative_path = file_info["relative_path"];
        std::string backup_path = file_info["backup_path"];
        std::string original_path = file_info["original_path"];

        // Check if we should restore this specific file
        if (!options.specific_files.empty()) {
          bool should_restore = false;
          for (const auto &specific_file : options.specific_files) {
            if (relative_path == specific_file ||
                original_path == specific_file) {
              should_restore = true;
              break;
            }
          }
          if (!should_restore)
            continue;
        }

        try {
          restore_file(backup_path, original_path);
        } catch (const std::exception &e) {
          std::cerr << "Warning: Failed to restore file " << relative_path
                    << ": " << e.what() << std::endl;
        }
      }
    }

    // Restore memory if requested and available
    if (options.restore_memory && metadata.value("has_memory_backup", false)) {
      try {
        std::string memory_backup = checkpoint_dir + "/memory.txt";
        std::string memory_file = "data/memory.txt";
        restore_file(memory_backup, memory_file);
      } catch (const std::exception &e) {
        std::cerr << "Warning: Failed to restore memory: " << e.what()
                  << std::endl;
      }
    }

    return true;

  } catch (const std::exception &e) {
    std::cerr << "Error restoring checkpoint: " << e.what() << std::endl;
    return false;
  }
}

bool CheckpointService::delete_checkpoint(const std::string &checkpoint_id) {
  try {
    std::string checkpoint_dir =
        get_checkpoints_directory() + "/" + checkpoint_id;
    if (std::filesystem::exists(checkpoint_dir)) {
      std::filesystem::remove_all(checkpoint_dir);
      return true;
    }
    return false;
  } catch (const std::exception &e) {
    std::cerr << "Error deleting checkpoint: " << e.what() << std::endl;
    return false;
  }
}

size_t
CheckpointService::get_checkpoint_size(const std::string &checkpoint_id) {
  try {
    std::string checkpoint_dir =
        get_checkpoints_directory() + "/" + checkpoint_id;
    size_t total_size = 0;

    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(checkpoint_dir)) {
      if (entry.is_regular_file()) {
        total_size += entry.file_size();
      }
    }

    return total_size;
  } catch ([[maybe_unused]] const std::exception &e) {
    return 0;
  }
}

void CheckpointService::cleanup_old_checkpoints(size_t keep_count) {
  try {
    auto checkpoints = list_checkpoints();
    if (checkpoints.size() <= keep_count) {
      return; // Nothing to cleanup
    }

    // Delete oldest checkpoints
    for (size_t i = keep_count; i < checkpoints.size(); ++i) {
      delete_checkpoint(checkpoints[i].id);
    }

  } catch (const std::exception &e) {
    std::cerr << "Error cleaning up checkpoints: " << e.what() << std::endl;
  }
}

bool CheckpointService::export_checkpoint(const std::string &checkpoint_id,
                                          const std::string &export_path) {
  // For now, just copy the checkpoint directory
  try {
    std::string checkpoint_dir =
        get_checkpoints_directory() + "/" + checkpoint_id;
    if (!std::filesystem::exists(checkpoint_dir)) {
      return false;
    }

    std::filesystem::copy(
        checkpoint_dir, export_path,
        std::filesystem::copy_options::recursive |
            std::filesystem::copy_options::overwrite_existing);
    return true;

  } catch (const std::exception &e) {
    std::cerr << "Error exporting checkpoint: " << e.what() << std::endl;
    return false;
  }
}

std::string
CheckpointService::import_checkpoint(const std::string &archive_path) {
  try {
    if (!std::filesystem::exists(archive_path)) {
      throw std::runtime_error("Archive path does not exist");
    }

    ensure_checkpoints_directory();

    // Generate new checkpoint ID
    std::string new_checkpoint_id = generate_checkpoint_id();
    std::string checkpoint_dir =
        get_checkpoints_directory() + "/" + new_checkpoint_id;

    // Copy archive to checkpoint directory
    std::filesystem::copy(archive_path, checkpoint_dir,
                          std::filesystem::copy_options::recursive);

    // Update metadata with new ID
    std::string metadata_file = checkpoint_dir + "/metadata.json";
    if (std::filesystem::exists(metadata_file)) {
      std::ifstream file(metadata_file);
      nlohmann::json metadata;
      file >> metadata;
      file.close();

      metadata["id"] = new_checkpoint_id;
      metadata["imported_at"] = get_timestamp();

      std::ofstream out_file(metadata_file);
      out_file << metadata.dump(2);
      out_file.close();
    }

    return new_checkpoint_id;

  } catch (const std::exception &e) {
    std::cerr << "Error importing checkpoint: " << e.what() << std::endl;
    return "";
  }
}
} // namespace Services
