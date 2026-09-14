#pragma once
#include <cstdint>

namespace Core {
enum class AgentMode : std::uint8_t {
  MODE_UNSET = 0,
  MODE_TOGETHER = 1,
  MODE_LLAMA_3B = 2,
  MODE_CEREBRAS = 3,
  MODE_LLAMA_LATEST = 4,
  MODE_LLAMA_31 = 5,
  MODE_FIREWORKS = 6,
  MODE_GROQ = 7,
  MODE_DEEPSEEK = 8,
  MODE_OPENAI = 9
};
} // namespace Core
