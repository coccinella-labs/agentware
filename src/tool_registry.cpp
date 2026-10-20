#include "tool_registry.h"

#include "command_router.h"

#include <algorithm>
#include <sstream>

namespace Core {

ToolRegistry &ToolRegistry::instance() {
  static ToolRegistry r;
  return r;
}

void ToolRegistry::register_tool(ToolSpec spec) {
  // Idempotent: re-registering replaces the existing entry with the same name.
  for (auto &t : tools_) {
    if (t.name == spec.name) {
      t = std::move(spec);
      return;
    }
  }
  tools_.push_back(std::move(spec));
}

const ToolSpec *ToolRegistry::find(const std::string &name) const {
  for (const auto &t : tools_) {
    if (t.name == name)
      return &t;
  }
  return nullptr;
}

const ToolSpec *ToolRegistry::match_prefix(const std::string &input) const {
  for (const auto &t : tools_) {
    if (has_prefix_ci(input, t.name))
      return &t;
  }
  return nullptr;
}

std::string ToolRegistry::known_names() const {
  std::string out;
  for (size_t i = 0; i < tools_.size(); ++i) {
    if (i)
      out += ", ";
    out += tools_[i].name;
  }
  return out;
}

std::string ToolRegistry::render_prompt_section() const {
  auto category_of = [](const std::string &n) -> const char * {
    if (n == "search:" || n == "cmd:" || n == "read:" || n == "write:" ||
        n == "list:")
      return "BASIC COMMANDS";
    if (n == "replace:" || n == "grep:")
      return "ADVANCED FILE OPERATIONS";
    if (n == "analyze:" || n == "components:" || n == "todos:" || n == "tree:")
      return "CODEBASE ANALYSIS";
    if (n == "git:" || n == "github:")
      return "GIT INTEGRATION";
    if (n == "remember:" || n == "memory" || n == "clear" || n == "forget")
      return "MEMORY MANAGEMENT";
    return nullptr;
  };

  std::ostringstream oss;
  const char *current = nullptr;
  for (const auto &t : tools_) {
    const char *cat = category_of(t.name);
    if (cat && cat != current) {
      if (current)
        oss << "\n";
      oss << cat << ":\n";
      current = cat;
    }
    if (!t.prompt_lines.empty()) {
      for (const auto &pl : t.prompt_lines) {
        oss << pl << "\n";
      }
    } else {
      oss << "\xe2\x80\xa2 " << t.name; // bullet
      if (!t.param_shape.empty())
        oss << t.param_shape;
      if (!t.description.empty())
        oss << " - " << t.description;
      oss << "\n";
    }
  }
  return oss.str();
}

void register_all_tools() {
  auto &R = ToolRegistry::instance();
  // BASIC COMMANDS
  R.register_tool({"search:",
                   "Search the web for information",
                   "query",
                   ToolRiskTier::ReadOnly,
                   nullptr,
                   {"• search:query - Search the web for information"}});
  R.register_tool({"cmd:",
                   "Execute shell commands safely (sandbox by default)",
                   "command",
                   ToolRiskTier::Mutating,
                   nullptr,
                   {"• cmd:command - Execute shell commands safely"}});
  R.register_tool(
      {"read:",
       "Read file contents (optionally a line range)",
       "filename[:start:count]",
       ToolRiskTier::ReadOnly,
       nullptr,
       {"• read:filename - Read file contents",
        "• read:filename:start:count - Read specific line ranges from files"}});
  R.register_tool({"write:",
                   "Write content to files",
                   "filename content",
                   ToolRiskTier::Mutating,
                   nullptr,
                   {"• write:filename content - Write content to files"}});
  R.register_tool({"list:",
                   "List files and directories (glob support)",
                   "path[:glob]",
                   ToolRiskTier::ReadOnly,
                   nullptr,
                   {"• list:path[:glob] - List files and directories"}});
  R.register_tool({"replace:",
                   "Replace text in files with precision",
                   "filename:old_text:new_text[:expected_count]",
                   ToolRiskTier::Mutating,
                   nullptr,
                   {"• replace:filename:old_text:new_text[:expected_count] - "
                    "Replace text in "
                    "files with precision"}});
  R.register_tool({"grep:",
                   "Search for patterns in files and directories",
                   "pattern[:directory[:file_filter]]",
                   ToolRiskTier::ReadOnly,
                   nullptr,
                   {"• grep:pattern[:directory[:file_filter]] - Search for "
                    "patterns in files "
                    "and directories"}});
  // CODEBASE ANALYSIS
  R.register_tool(
      {"analyze:",
       "Analyze codebase structure, file types, and "
       "project configuration",
       "path",
       ToolRiskTier::ReadOnly,
       nullptr,
       {"• analyze:path - Analyze codebase structure, file types, and project "
        "configuration"}});
  R.register_tool(
      {"components:",
       "Find main components and their relationships",
       "path",
       ToolRiskTier::ReadOnly,
       nullptr,
       {"• components:path - Find main components and their relationships"}});
  R.register_tool({"todos:",
                   "Find all task comments in codebase",
                   "path",
                   ToolRiskTier::ReadOnly,
                   nullptr,
                   {"• todos:path - Find all task comments in codebase"}});
  R.register_tool({"tree:",
                   "Display directory tree structure",
                   "path",
                   ToolRiskTier::ReadOnly,
                   nullptr,
                   {"• tree:path - Display directory tree structure"}});
  // GIT INTEGRATION
  R.register_tool({"git:",
                   "Git integration (log/status/diff/analyze)",
                   "log|status|diff|analyze [path]",
                   ToolRiskTier::ReadOnly,
                   nullptr,
                   {"• git:log - Show recent git commit history",
                    "• git:status - Show git working directory status",
                    "• git:diff - Show unstaged working-tree diff",
                    "• git:analyze - Comprehensive git repository analysis"}});
  R.register_tool({"github:",
                   "GitHub integration (repo/issues/health/threads)",
                   "params",
                   ToolRiskTier::ReadOnly,
                   nullptr,
                   {"• github:params - GitHub integration (repo, issues, "
                    "health, threads)"}});
  // MEMORY MANAGEMENT
  R.register_tool(
      {"remember:",
       "Save important facts to persistent global memory",
       "fact",
       ToolRiskTier::Mutating,
       nullptr,
       {"• remember:fact - Save important facts to persistent global memory"}});
  R.register_tool({"memory",
                   "View stored global memories",
                   "",
                   ToolRiskTier::ReadOnly,
                   nullptr,
                   {"• memory - View stored global memories"}});
  R.register_tool({"clear",
                   "Clear current session memory",
                   "",
                   ToolRiskTier::Mutating,
                   nullptr,
                   {"• clear - Clear current session memory"}});
  R.register_tool({"forget",
                   "Clear all global memories",
                   "",
                   ToolRiskTier::Mutating,
                   nullptr,
                   {"• forget - Clear all global memories"}});
}

} // namespace Core
