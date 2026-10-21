#pragma once

#include "tool_registry.h" // For ToolSpec

#include <nlohmann/json.hpp>
#include <string>

// Canonical model tool-call form.
//
// The model may name a tool in several ways ("list", "list:", "LIST",
// " list: ", "git" with a "subcommand" argument, "git:diff", ...). Every
// one of those forms must resolve to exactly ONE canonical representation,
// and every check (risk tier, model-surface policy, dispatch) must read
// that representation. Nothing downstream may re-parse the raw string the
// model wrote: that is how a case variant or a colon variant slipped past a
// refusal check.
namespace Core {

// The canonical form of a model tool call.
struct ToolCall {
  // Registry name of the tool, exactly as registered ("list:", "git:",
  // "write:", or the bare "memory" / "clear" / "forget").
  std::string tool;
  // Subcommand for tools that take one ("diff" for git:, "repo" for
  // github:). Empty when the tool takes no subcommand.
  std::string subcommand;
  // Arguments as the model sent them.
  nlohmann::json args;
  // True when the name resolved to a registered tool.
  bool resolved = false;
  // True when the colon form and the subcommand argument disagree.
  bool ambiguous = false;
  // The name exactly as the model wrote it, for error messages only.
  std::string raw_name;
};

// Canonicalize a model tool call.
//
// Returns false when the name does not resolve to a registered tool.
// Rejected, in order:
//   - empty or whitespace-only names
//   - names with any non-ASCII byte (Unicode lookalikes, zero-width
//     joiners) or control characters
//   - names starting with '!' or '/' (shell and meta prefixes, never
//     tool names)
//   - names whose base does not resolve to a registered tool
bool canonicalize_tool_call(const std::string &raw_name,
                            const nlohmann::json &raw_args, ToolCall &out);

// The registry entry for a canonical tool name, or nullptr.
const ToolSpec *find_tool(const std::string &canonical_tool);

// The path a tool call reads, when it has one. Empty when the tool does
// not take a path.
std::string tool_call_path(const ToolCall &call);

// True when the model may not call this tool at all in v1, even though a
// user can. These tools read file contents without filtering secret
// paths, so the model surface must not reach them yet.
bool is_model_surface_disabled(const ToolCall &call);

// True when the model must obtain consent before the tool runs.
bool requires_consent(const ToolCall &call);

// Build the dispatch input (the "name:args" string the existing dispatch
// understands) from a canonical call.
std::string tool_call_to_input(const ToolCall &call);

} // namespace Core
