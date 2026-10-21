#include "tool_call.h"

#include <algorithm>
#include <cctype>

namespace Core {

namespace {

// Trim ASCII whitespace.
std::string trim_ascii(const std::string &s) {
  size_t b = s.find_first_not_of(" \t\n\r\f\v");
  if (b == std::string::npos)
    return "";
  size_t e = s.find_last_not_of(" \t\n\r\f\v");
  return s.substr(b, e - b + 1);
}

// ASCII lowercase. Deliberately not std::tolower on a signed char, and
// deliberately ASCII-only: a non-ASCII byte is rejected before this runs.
std::string ascii_lower(const std::string &s) {
  std::string out = s;
  for (char &c : out) {
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
  }
  return out;
}

// True when every byte is printable ASCII. This rejects zero-width
// joiners, Unicode lookalikes (Cyrillic 'а' for 'a'), and control
// characters, none of which may be part of a tool name.
bool is_printable_ascii(const std::string &s) {
  for (unsigned char c : s) {
    if (c < 0x20 || c > 0x7E)
      return false;
  }
  return true;
}

// Split "name:rest" into the two parts. "rest" may be empty.
void split_at_colon(const std::string &s, std::string &base,
                    std::string &rest) {
  size_t c = s.find(':');
  if (c == std::string::npos) {
    base = s;
    rest.clear();
    return;
  }
  base = s.substr(0, c);
  rest = s.substr(c + 1);
}

} // namespace

const ToolSpec *find_tool(const std::string &canonical_tool) {
  if (canonical_tool.empty())
    return nullptr;
  return ToolRegistry::instance().find(canonical_tool);
}

bool canonicalize_tool_call(const std::string &raw_name,
                            const nlohmann::json &raw_args, ToolCall &out) {
  out = ToolCall();
  out.raw_name = raw_name;
  out.args = raw_args.is_object() ? raw_args : nlohmann::json::object();

  const std::string trimmed = trim_ascii(raw_name);
  if (trimmed.empty())
    return false;
  // Non-ASCII or control bytes: Unicode lookalikes and zero-width
  // characters are never part of a tool name.
  if (!is_printable_ascii(trimmed))
    return false;
  // '!' is the shell prefix and '/' is the meta prefix. Neither names a
  // tool, so a call using one must not resolve to a tool.
  if (trimmed[0] == '!' || trimmed[0] == '/')
    return false;

  const std::string lowered = ascii_lower(trimmed);
  std::string base;
  std::string rest;
  split_at_colon(lowered, base, rest);
  base = trim_ascii(base);
  rest = trim_ascii(rest);
  if (base.empty())
    return false;

  // The registry holds colon names for the command tools and bare names
  // for the memory verbs.
  std::string canonical = base + ":";
  const ToolSpec *spec = ToolRegistry::instance().find(canonical);
  if (!spec) {
    canonical = base;
    spec = ToolRegistry::instance().find(canonical);
  }
  if (!spec)
    return false;

  out.tool = canonical;
  out.resolved = true;

  // Subcommand: an explicit "tool:sub" form and a "subcommand" argument
  // are two ways to say the same thing. If both are present and they
  // DISAGREE, the call is ambiguous and is refused: silently preferring
  // one of them would let a caller name one subcommand and run another.
  std::string from_colon;
  std::string from_args;
  if (!rest.empty()) {
    from_colon = rest;
  }
  if (canonical == "git:") {
    from_args = out.args.value("subcommand", out.args.value("action",
                             out.args.value("command", std::string(""))));
  } else if (canonical == "github:") {
    // github:params carries "repo:owner/name" style specs.
    std::string params = out.args.value(
        "params", out.args.value("query", out.args.value(
                          "subcommand", std::string(""))));
    size_t c = params.find(':');
    from_args = (c == std::string::npos) ? params : params.substr(0, c);
  }
  from_colon = trim_ascii(ascii_lower(from_colon));
  from_args = trim_ascii(ascii_lower(from_args));

  if (!from_colon.empty() && !from_args.empty() && from_colon != from_args) {
    out.ambiguous = true;
    out.subcommand = from_colon;
    return true;
  }
  out.subcommand = from_colon.empty() ? from_args : from_colon;
  return true;
}

// The path a tool call reads, when it has one. Empty when the tool does
// not take a path.
std::string tool_call_path(const ToolCall &call) {
  if (call.tool == "read:") {
    return call.args.value("path", call.args.value("filename",
                           call.args.value("file", std::string(""))));
  }
  return "";
}

bool is_model_surface_disabled(const ToolCall &call) {
  // git:diff, todos: and analyze: read file contents without filtering
  // secret paths yet, so the model surface must not reach them.
  if (call.tool == "todos:" || call.tool == "analyze:")
    return true;
  if (call.tool == "git:" && call.subcommand == "diff")
    return true;
  return false;
}

bool requires_consent(const ToolCall &call) {
  // fetch: and github: carry data out of the machine, so the model must
  // ask before they run.
  return call.tool == "fetch:" || call.tool == "github:";
}

std::string tool_call_to_input(const ToolCall &call) {
  const nlohmann::json &args = call.args;
  const std::string &tool = call.tool;

  if (tool == "search:") {
    return "search:" + args.value("query", args.value("q", std::string("")));
  }
  if (tool == "cmd:") {
    return "cmd:" + args.value("command", args.value("cmd", std::string("")));
  }
  if (tool == "read:") {
    std::string path = args.value(
        "path", args.value("filename", args.value("file", std::string(""))));
    if (args.contains("start") || args.contains("count") ||
        args.contains("lines")) {
      int start = args.value("start", args.value("start_line", 1));
      int count = args.value("count", args.value("line_count", 0));
      if (count > 0) {
        return "read:" + path + ":" + std::to_string(start) + ":" +
               std::to_string(count);
      }
      if (start > 1) {
        return "read:" + path + ":" + std::to_string(start);
      }
    }
    return "read:" + path;
  }
  if (tool == "write:") {
    std::string path = args.value(
        "path", args.value("filename", args.value("file", std::string(""))));
    std::string content =
        args.value("content", args.value("text", std::string("")));
    return "write:" + path + " " + content;
  }
  if (tool == "replace:") {
    std::string path = args.value(
        "path", args.value("filename", args.value("file", std::string(""))));
    std::string old_text = args.value(
        "old", args.value("old_text", args.value("search", std::string(""))));
    std::string new_text = args.value(
        "new", args.value("new_text", args.value("replacement", std::string(""))));
    int count = args.value("count", args.value("expected_count", 0));
    std::string input = "replace:" + path + ":" + old_text + ":" + new_text;
    if (count > 0)
      input += ":" + std::to_string(count);
    return input;
  }
  if (tool == "grep:") {
    std::string pattern =
        args.value("pattern", args.value("query", std::string("")));
    std::string dir = args.value(
        "directory", args.value("dir", args.value("path", std::string("."))));
    std::string filter =
        args.value("filter", args.value("file_filter", std::string("*")));
    std::string input = "grep:" + pattern + ":" + dir;
    if (filter != "*")
      input += ":" + filter;
    return input;
  }
  if (tool == "remember:") {
    return "remember:" +
           args.value("fact", args.value("text", args.value("content",
                                                            std::string(""))));
  }
  if (tool == "analyze:") {
    return "analyze:" +
           args.value("path", args.value("directory", std::string(".")));
  }
  if (tool == "components:") {
    return "components:" +
           args.value("path", args.value("directory", std::string(".")));
  }
  if (tool == "todos:") {
    return "todos:" +
           args.value("path", args.value("directory", std::string(".")));
  }
  if (tool == "tree:") {
    return "tree:" +
           args.value("path", args.value("directory", std::string(".")));
  }
  if (tool == "git:") {
    std::string sub = call.subcommand.empty() ? std::string("status")
                                               : call.subcommand;
    std::string path =
        args.value("path", args.value("directory", std::string(".")));
    return "git:" + sub + (path != "." ? " " + path : "");
  }
  if (tool == "github:") {
    return "github:" +
           args.value("params", args.value("query", std::string("")));
  }
  if (tool == "list:") {
    std::string path =
        args.value("path", args.value("directory", std::string(".")));
    std::string glob =
        args.value("glob", args.value("pattern", std::string("*")));
    return "list:" + path + ":" + glob;
  }
  if (tool == "fetch:") {
    return "fetch:" +
           args.value("url", args.value("uri", std::string("")));
  }
  // Unknown: pass through as-is (will be caught by dispatch)
  return tool + ":" + args.dump();
}

std::string tool_registry_dump() {
  Core::register_all_tools();
  std::string out;
  for (const auto &t : ToolRegistry::instance().tools()) {
    ToolCall call;
    call.tool = t.name;
    call.resolved = true;
    // The model-surface policy is per (tool, subcommand); the dump
    // reports the tool with no subcommand, which is the conservative
    // answer for a tool whose subcommands differ.
    const bool model_callable =
        t.risk != ToolRiskTier::Mutating && !is_model_surface_disabled(call);
    out += "tool\t";
    out += t.name;
    out += "\t";
    out += (t.risk == ToolRiskTier::Mutating) ? "mutating" : "read-only";
    out += "\t";
    out += model_callable ? "yes" : "no";
    out += "\t";
    out += requires_consent(call) ? "yes" : "no";
    out += "\n";
  }
  // Every (tool, subcommand) the model surface must refuse. The list is
  // derived from the registry plus the policy above, so a newly
  // registered mutating tool is covered without editing a list.
  for (const auto &t : ToolRegistry::instance().tools()) {
    ToolCall call;
    call.tool = t.name;
    call.resolved = true;
    const bool refused = t.risk == ToolRiskTier::Mutating ||
                         requires_consent(call) ||
                         is_model_surface_disabled(call);
    if (refused) {
      out += "refused\t";
      out += t.name;
      out += "\t";
      out += "";
      out += "\t";
      out += requires_consent(call) ? "yes" : "no";
      out += "\n";
    }
  }
  // Subcommand-level refusals (git:diff).
  for (const char *tool : {"git:"}) {
    for (const char *sub : {"diff"}) {
      ToolCall call;
      call.tool = tool;
      call.subcommand = sub;
      call.resolved = true;
      out += "refused\t";
      out += tool;
      out += "\t";
      out += sub;
      out += "\tno\n";
    }
  }
  return out;
}

} // namespace Core
