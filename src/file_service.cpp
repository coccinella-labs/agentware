#include "file_service.h"
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <regex>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace Services {
static const std::size_t MAX_FILE_SIZE = 5 * 1024 * 1024; // 5 MB limit

namespace {

// Describes the file type an opened descriptor actually names. The read path
// verifies the descriptor, not the path, so a path swapped for a FIFO or a
// device between the caller's check and the open is still refused.
std::string non_regular_description(
#ifdef _WIN32
    const struct _stat64 &st
#else
    const struct stat &st
#endif
) {
#ifdef _WIN32
  if ((st.st_mode & _S_IFDIR) != 0)
    return "a directory";
  return "a non-regular file";
#else
  if (S_ISDIR(st.st_mode))
    return "a directory";
  if (S_ISFIFO(st.st_mode))
    return "a FIFO";
  if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode))
    return "a device";
  if (S_ISSOCK(st.st_mode))
    return "a socket";
  return "a non-regular file";
#endif
}

void close_fd(int fd) {
#ifdef _WIN32
  _close(fd);
#else
  ::close(fd);
#endif
}

// Opens a regular file for reading and verifies its type on the opened
// descriptor. O_NONBLOCK keeps a FIFO from blocking the open, and O_NOFOLLOW
// refuses a final-component symlink on POSIX. Returns an empty string and
// fills `content` on success; returns a user-facing error otherwise.
std::string read_regular_file(const std::string &filename,
                              std::string &content) {
#ifndef _WIN32
  const int fd =
      ::open(filename.c_str(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
#else
  const int fd = _open(filename.c_str(), _O_RDONLY | _O_BINARY | _O_NOINHERIT);
#endif
  if (fd < 0) {
#ifndef _WIN32
    if (errno == ENOENT)
      return "Error: File '" + filename + "' does not exist";
    if (errno == ELOOP)
      return "Refusing to read a symbolic link: " + filename;
#endif
    return "Error: Could not open file '" + filename + "'";
  }

  std::uintmax_t reported_size = 0;
#ifdef _WIN32
  struct _stat64 st{};
  const bool stat_ok = _fstat64(fd, &st) == 0;
#else
  struct stat st{};
  const bool stat_ok = fstat(fd, &st) == 0;
#endif
  if (!stat_ok) {
    close_fd(fd);
    return "Error: Could not inspect file '" + filename + "'";
  }
#ifdef _WIN32
  const bool is_regular = (st.st_mode & _S_IFREG) != 0;
#else
  const bool is_regular = S_ISREG(st.st_mode);
#endif
  if (!is_regular) {
    const std::string what = non_regular_description(st);
    close_fd(fd);
    return "Refusing to read " + what + ": " + filename;
  }
  if (st.st_size > 0)
    reported_size = static_cast<std::uintmax_t>(st.st_size);
  if (reported_size > MAX_FILE_SIZE) {
    close_fd(fd);
    return "Error: File too large to read (" + std::to_string(reported_size) +
           " bytes)";
  }

  content.clear();
  char buffer[65536];
  while (content.size() <= MAX_FILE_SIZE) {
    const std::size_t want =
        std::min(sizeof(buffer), MAX_FILE_SIZE + 1 - content.size());
#ifdef _WIN32
    const int n = _read(fd, buffer, static_cast<unsigned int>(want));
#else
    const ssize_t n = ::read(fd, buffer, want);
#endif
    if (n < 0) {
#ifndef _WIN32
      if (errno == EINTR)
        continue;
#endif
      close_fd(fd);
      return "Error reading file '" + filename + "'";
    }
    if (n == 0)
      break;
    content.append(buffer, static_cast<std::size_t>(n));
  }
  close_fd(fd);
  if (content.size() > MAX_FILE_SIZE)
    return "Error: File too large to read";
  return "";
}

std::vector<std::string> split_lines(const std::string &content) {
  std::vector<std::string> lines;
  std::istringstream in(content);
  std::string line;
  while (std::getline(in, line)) {
    // The read is binary so the byte count is exact; a range read still
    // strips the CR of a CRLF file, matching the old text-mode line range.
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    lines.push_back(line);
  }
  return lines;
}

} // namespace

std::string FileService::read_file(const std::string &filename) {
  try {
    std::string content;
    const std::string error = read_regular_file(filename, content);
    return error.empty() ? content : error;
  } catch ([[maybe_unused]] const std::exception &e) {
    return std::string("Error reading file: ") + e.what();
  }
}

std::string FileService::read_file_range(const std::string &filename,
                                         int start_line, int line_count) {
  try {
    std::string content;
    const std::string error = read_regular_file(filename, content);
    if (!error.empty())
      return error;

    const std::vector<std::string> lines = split_lines(content);
    if (start_line < 0 || start_line >= static_cast<int>(lines.size())) {
      return "Error: Start line out of range";
    }

    int end_line =
        (line_count == -1)
            ? static_cast<int>(lines.size())
            : std::min(start_line + line_count, static_cast<int>(lines.size()));

    std::ostringstream result;
    for (int i = start_line; i < end_line; ++i) {
      result << lines[i];
      if (i < end_line - 1)
        result << "\n";
    }

    return result.str();
  } catch ([[maybe_unused]] const std::exception &e) {
    return std::string("Error reading file range: ") + e.what();
  }
}

std::string FileService::write_file(const std::string &filename,
                                    const std::string &content) {
  try {
    if (content.size() > MAX_FILE_SIZE) {
      return "Error: Content too large to write";
    }

    std::filesystem::path file_path(filename);
    if (file_path.has_parent_path()) {
      std::filesystem::create_directories(file_path.parent_path());
    }

    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
      return "Error: Could not create/write file '" + filename + "'";
    }

    file << content;
    if (!file) {
      return "Error: Write operation failed";
    }
    return "File '" + filename + "' written successfully (" +
           std::to_string(content.length()) + " bytes)";
  } catch ([[maybe_unused]] const std::exception &e) {
    return std::string("Error writing file: ") + e.what();
  }
}

FileEditResult FileService::replace_text_in_file(const std::string &filename,
                                                 const std::string &old_text,
                                                 const std::string &new_text,
                                                 int expected_replacements) {
  FileEditResult result = {false, "", 0};

  try {
    // An empty pattern matches at every position without advancing and
    // hangs the loops below; reject it before touching the file.
    if (old_text.empty()) {
      result.message = "Error: old text cannot be empty";
      return result;
    }

    if (!file_exists(filename)) {
      result.message = "Error: File '" + filename + "' does not exist";
      return result;
    }

    // Read file content
    std::ifstream file(filename);
    if (!file.is_open()) {
      result.message = "Error: Could not open file '" + filename + "'";
      return result;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();

    // Count occurrences
    size_t pos = 0;
    int count = 0;
    while ((pos = content.find(old_text, pos)) != std::string::npos) {
      count++;
      pos += old_text.length();
    }

    if (count == 0) {
      result.message = "Error: Text to replace not found in file";
      return result;
    }

    if (expected_replacements > 0 && count != expected_replacements) {
      result.message = "Error: Expected " +
                       std::to_string(expected_replacements) +
                       " replacements but found " + std::to_string(count);
      return result;
    }

    // Perform replacements
    pos = 0;
    while ((pos = content.find(old_text, pos)) != std::string::npos) {
      content.replace(pos, old_text.length(), new_text);
      pos += new_text.length();
    }

    // Write back to file
    std::ofstream out_file(filename);
    if (!out_file.is_open()) {
      result.message = "Error: Could not write to file '" + filename + "'";
      return result;
    }

    out_file << content;
    if (!out_file) {
      result.message = "Error: Write operation failed";
      return result;
    }

    result.success = true;
    result.replacements_made = count;
    result.message =
        "Successfully replaced " + std::to_string(count) + " occurrence(s)";
    return result;

  } catch ([[maybe_unused]] const std::exception &e) {
    result.message = std::string("Error during text replacement: ") + e.what();
    return result;
  }
}

std::vector<FileSearchResult>
FileService::search_in_file(const std::string &filename,
                            const std::string &pattern, bool case_sensitive) {
  std::vector<FileSearchResult> results;

  try {
    if (!file_exists(filename) || !is_text_file(filename)) {
      return results;
    }

    std::ifstream file(filename);
    if (!file.is_open()) {
      return results;
    }

    std::regex regex_pattern;
    if (case_sensitive) {
      regex_pattern = std::regex(pattern);
    } else {
      regex_pattern = std::regex(pattern, std::regex_constants::icase);
    }

    std::string line;
    int line_number = 1;

    while (std::getline(file, line)) {
      std::smatch match;
      if (std::regex_search(line, match, regex_pattern)) {
        FileSearchResult result;
        result.file_path = filename;
        result.line_number = line_number;
        result.line_content = line;
        result.match_context = line; // For now, context is the same as line
        results.push_back(result);
      }
      line_number++;
    }

  } catch ([[maybe_unused]] const std::exception &e) {
    // Log error but return partial results
  }

  return results;
}

std::vector<FileSearchResult> FileService::search_in_directory(
    const std::string &directory, const std::string &pattern,
    const std::string &file_filter, bool case_sensitive) {
  std::vector<FileSearchResult> all_results;

  try {
    if (!std::filesystem::exists(directory) ||
        !std::filesystem::is_directory(directory)) {
      return all_results;
    }

    // Simple file filter matching (supports * wildcard)
    auto matches_filter = [&file_filter](const std::string &filename) {
      if (file_filter == "*")
        return true;

      // Simple wildcard matching
      if (file_filter.find('*') != std::string::npos) {
        std::string pattern_regex = file_filter;
        std::replace(pattern_regex.begin(), pattern_regex.end(), '*', '.');
        pattern_regex = ".*" + pattern_regex + ".*";
        try {
          std::regex filter_regex(pattern_regex, std::regex_constants::icase);
          return std::regex_match(filename, filter_regex);
        } catch (...) {
          return false;
        }
      }

      return filename.find(file_filter) != std::string::npos;
    };

    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(directory)) {
      if (entry.is_regular_file()) {
        std::string filepath = entry.path().string();
        std::string filename = entry.path().filename().string();

        if (matches_filter(filename) && is_text_file(filepath)) {
          auto file_results = search_in_file(filepath, pattern, case_sensitive);
          all_results.insert(all_results.end(), file_results.begin(),
                             file_results.end());
        }
      }
    }

  } catch ([[maybe_unused]] const std::exception &e) {
    // Log error but return partial results
  }

  return all_results;
}

bool FileService::file_exists(const std::string &filename) {
  try {
    return std::filesystem::exists(filename);
  } catch (...) {
    return false;
  }
}

std::string FileService::get_file_extension(const std::string &filename) {
  std::filesystem::path file_path(filename);
  return file_path.extension().string();
}

bool FileService::is_text_file(const std::string &filename) {
  std::string ext = get_file_extension(filename);
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  // Common text file extensions
  static const std::vector<std::string> text_extensions = {
      ".txt", ".md",   ".cpp", ".h",   ".hpp",  ".c",    ".cc",
      ".cxx", ".py",   ".js",  ".ts",  ".html", ".css",  ".json",
      ".xml", ".yaml", ".yml", ".ini", ".cfg",  ".conf", ".log",
      ".sh",  ".bat",  ".ps1", ".sql", ".csv",  ".tsv"};

  return std::find(text_extensions.begin(), text_extensions.end(), ext) !=
         text_extensions.end();
}

std::string FileService::get_relative_path(const std::string &filepath,
                                           const std::string &base_path) {
  try {
    std::filesystem::path file_path = std::filesystem::absolute(filepath);
    std::filesystem::path base = std::filesystem::absolute(base_path);
    return std::filesystem::relative(file_path, base).string();
  } catch (...) {
    return filepath;
  }
}

bool FileService::is_within_directory(const std::string &filepath,
                                      const std::string &base_directory) {
  try {
    std::filesystem::path file_path = std::filesystem::absolute(filepath);
    std::filesystem::path base = std::filesystem::absolute(base_directory);

    auto file_it = file_path.begin();
    auto base_it = base.begin();

    while (base_it != base.end() && file_it != file_path.end()) {
      if (*base_it != *file_it) {
        return false;
      }
      ++base_it;
      ++file_it;
    }

    return base_it == base.end();
  } catch (...) {
    return false;
  }
}

std::string FileService::normalize_path(const std::string &path) {
  try {
    return std::filesystem::absolute(path).string();
  } catch (...) {
    return path;
  }
}

} // namespace Services
