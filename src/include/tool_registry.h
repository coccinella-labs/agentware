#pragma once

#include <functional>
#include <string>
#include <vector>

#include "config.h" // For AGENTWARE_API

namespace Core {

// Risk tier for a tool. Read-only tools may auto-run from the model-call
// surface; mutating tools go through the existing consent gate.
enum class ToolRiskTier { ReadOnly, Mutating };

// A single tool in the registry. The registry is the single source of truth
// for both the dispatch surface (user prefix routing) and the tool section of
// the system prompt (src/ai_service.cpp), so the two cannot drift.
struct ToolSpec {
  // Colon-prefixed command name as typed by the user, e.g. "search:".
  // Must include the trailing colon.
  std::string name;
  // One-line human description, used in the system prompt and in the
  // "Unknown command" recovery message.
  std::string description;
  // Compact parameter shape, e.g. "query" or "filename:old:new[:count]".
  // Informational; the model-call seam (PR-3) will tighten this into a
  // JSON schema.
  std::string param_shape;
  ToolRiskTier risk = ToolRiskTier::ReadOnly;
  // Handler. Receives the already-trimmed full input line and returns the
  // tool's textual result. Mutating handlers are expected to obtain consent
  // internally (or be gated by the caller) before side effects.
  std::function<std::string(const std::string &input)> handler;
  // Optional explicit prompt lines. When non-empty, these are emitted
  // verbatim (in order) instead of the synthesized "name + shape - desc"
  // line, so the generated prompt stays byte-identical to the hand-written
  // one for tools with multiple documented shapes (read:, git:).
  std::vector<std::string> prompt_lines;
};

// The registry. A process-wide singleton populated by register_all_tools().
class ToolRegistry {
public:
  static ToolRegistry &instance();

  void register_tool(ToolSpec spec);
  const ToolSpec *find(const std::string &name) const;
  // Lookup by prefix: matches a tool whose `name` is a case-insensitive
  // prefix of `input`. Returns nullptr when nothing matches.
  const ToolSpec *match_prefix(const std::string &input) const;
  const std::vector<ToolSpec> &tools() const { return tools_; }
  // Render the system-prompt tool section from the registry, so ai_service
  // never hand-maintains a parallel list.
  std::string render_prompt_section() const;
  // Names joined for the "Unknown command" recovery message.
  std::string known_names() const;

private:
  ToolRegistry() = default;
  std::vector<ToolSpec> tools_;
};

// Populates the registry with every colon tool the agent knows. Idempotent.
void register_all_tools();

} // namespace Core