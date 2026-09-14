#pragma once

#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace Services {

class CodebaseService {
public:
  CodebaseService();

  // Analyze overall codebase structure
  static std::string analyze_structure(const std::string &path);

  // Find main components and their relationships
  static std::string find_main_components(const std::string &path);

  // Find TODO/FIXME/HACK comments
  static std::vector<std::string> find_todos(const std::string &path);

  // Get directory tree structure
  static std::string get_directory_tree(const std::string &path,
                                        int max_depth = 3);

private:
  // Helper methods
  static void build_tree_recursive(const std::string &path,
                                   std::ostringstream &tree,
                                   const std::string &prefix, int depth,
                                   int max_depth);
  static std::map<std::string, int> analyze_file_types(const std::string &path);
  static int count_files_in_directory(const std::string &path);
  static std::vector<std::string> find_key_files(const std::string &path);
};

} // namespace Services
