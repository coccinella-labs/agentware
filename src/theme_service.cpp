#include "theme_service.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace Services {

std::string ThemeService::current_theme_name_ = "default";
std::map<std::string, Theme> ThemeService::available_themes_;

std::string ThemeService::get_theme_config_path() {
  return "data/theme_config.json";
}

void ThemeService::ensure_theme_directory() {
  std::filesystem::create_directories("data");
}

void ThemeService::initialize_default_themes() {
  // Default theme (light)
  Theme default_theme;
  default_theme.name = "default";
  default_theme.description = "Default light theme";
  default_theme.is_dark_theme = false;
  default_theme.colors[ThemeColor::PRIMARY] = "#0066CC";
  default_theme.colors[ThemeColor::SECONDARY] = "#6C757D";
  default_theme.colors[ThemeColor::SUCCESS] = "#28A745";
  default_theme.colors[ThemeColor::WARNING] = "#FFC107";
  default_theme.colors[ThemeColor::ERROR] = "#DC3545";
  default_theme.colors[ThemeColor::INFO] = "#17A2B8";
  default_theme.colors[ThemeColor::TEXT] = "#212529";
  default_theme.colors[ThemeColor::BACKGROUND] = "#FFFFFF";
  default_theme.colors[ThemeColor::ACCENT] = "#007BFF";
  default_theme.colors[ThemeColor::MUTED] = "#6C757D";
  available_themes_["default"] = default_theme;

  // Dark theme
  Theme dark_theme;
  dark_theme.name = "dark";
  dark_theme.description = "Dark theme for low-light environments";
  dark_theme.is_dark_theme = true;
  dark_theme.colors[ThemeColor::PRIMARY] = "#4A9EFF";
  dark_theme.colors[ThemeColor::SECONDARY] = "#ADB5BD";
  dark_theme.colors[ThemeColor::SUCCESS] = "#40E0D0";
  dark_theme.colors[ThemeColor::WARNING] = "#FFD700";
  dark_theme.colors[ThemeColor::ERROR] = "#FF6B6B";
  dark_theme.colors[ThemeColor::INFO] = "#64B5F6";
  dark_theme.colors[ThemeColor::TEXT] = "#E9ECEF";
  dark_theme.colors[ThemeColor::BACKGROUND] = "#1A1A1A";
  dark_theme.colors[ThemeColor::ACCENT] = "#BB86FC";
  dark_theme.colors[ThemeColor::MUTED] = "#6C757D";
  available_themes_["dark"] = dark_theme;

  // Cyberpunk theme
  Theme cyberpunk_theme;
  cyberpunk_theme.name = "cyberpunk";
  cyberpunk_theme.description = "Neon cyberpunk theme";
  cyberpunk_theme.is_dark_theme = true;
  cyberpunk_theme.colors[ThemeColor::PRIMARY] = "#00FFFF";
  cyberpunk_theme.colors[ThemeColor::SECONDARY] = "#FF00FF";
  cyberpunk_theme.colors[ThemeColor::SUCCESS] = "#00FF00";
  cyberpunk_theme.colors[ThemeColor::WARNING] = "#FFFF00";
  cyberpunk_theme.colors[ThemeColor::ERROR] = "#FF0080";
  cyberpunk_theme.colors[ThemeColor::INFO] = "#80FF80";
  cyberpunk_theme.colors[ThemeColor::TEXT] = "#00FFFF";
  cyberpunk_theme.colors[ThemeColor::BACKGROUND] = "#000020";
  cyberpunk_theme.colors[ThemeColor::ACCENT] = "#FF00FF";
  cyberpunk_theme.colors[ThemeColor::MUTED] = "#808080";
  available_themes_["cyberpunk"] = cyberpunk_theme;

  // Ocean theme
  Theme ocean_theme;
  ocean_theme.name = "ocean";
  ocean_theme.description = "Calm ocean-inspired theme";
  ocean_theme.is_dark_theme = false;
  ocean_theme.colors[ThemeColor::PRIMARY] = "#006994";
  ocean_theme.colors[ThemeColor::SECONDARY] = "#4A90A4";
  ocean_theme.colors[ThemeColor::SUCCESS] = "#2E8B57";
  ocean_theme.colors[ThemeColor::WARNING] = "#DAA520";
  ocean_theme.colors[ThemeColor::ERROR] = "#CD5C5C";
  ocean_theme.colors[ThemeColor::INFO] = "#4682B4";
  ocean_theme.colors[ThemeColor::TEXT] = "#2F4F4F";
  ocean_theme.colors[ThemeColor::BACKGROUND] = "#F0F8FF";
  ocean_theme.colors[ThemeColor::ACCENT] = "#20B2AA";
  ocean_theme.colors[ThemeColor::MUTED] = "#708090";
  available_themes_["ocean"] = ocean_theme;
}

std::string ThemeService::get_ansi_color_code(const std::string &color) {
  // Convert hex colors to ANSI escape codes
  if (color == "#00FFFF") {
    return "\033[96m"; // Bright cyan
  }
  if (color == "#FF00FF") {
    return "\033[95m"; // Bright magenta
  }
  if (color == "#00FF00") {
    return "\033[92m"; // Bright green
  }
  if (color == "#FFFF00") {
    return "\033[93m"; // Bright yellow
  }
  if (color == "#FF0080")
    return "\033[91m"; // Bright red
  if (color == "#4A9EFF")
    return "\033[94m"; // Bright blue
  if (color == "#40E0D0")
    return "\033[96m"; // Turquoise
  if (color == "#FFD700")
    return "\033[93m"; // Gold
  if (color == "#FF6B6B")
    return "\033[91m"; // Light red
  if (color == "#64B5F6")
    return "\033[94m"; // Light blue
  if (color == "#BB86FC")
    return "\033[95m"; // Light purple
  if (color == "#28A745")
    return "\033[32m"; // Green
  if (color == "#DC3545")
    return "\033[31m"; // Red
  if (color == "#FFC107")
    return "\033[33m"; // Yellow
  if (color == "#17A2B8")
    return "\033[36m"; // Cyan
  if (color == "#007BFF")
    return "\033[34m"; // Blue
  if (color == "#0066CC")
    return "\033[34m"; // Blue
  if (color == "#006994")
    return "\033[34m"; // Dark blue
  if (color == "#2E8B57")
    return "\033[32m"; // Sea green
  if (color == "#20B2AA")
    return "\033[36m"; // Light sea green

  // Default colors
  return "\033[37m"; // White
}

void ThemeService::initialize() {
  initialize_default_themes();
  load_theme_config();
}

bool ThemeService::set_theme(const std::string &theme_name) {
  auto it = available_themes_.find(theme_name);
  if (it == available_themes_.end()) {
    return false;
  }

  current_theme_name_ = theme_name;
  save_theme_config();
  return true;
}

std::string ThemeService::get_current_theme() { return current_theme_name_; }

std::vector<std::string> ThemeService::list_available_themes() {
  std::vector<std::string> theme_names;
  for (const auto &pair : available_themes_) {
    theme_names.push_back(pair.first);
  }
  return theme_names;
}

Theme ThemeService::get_theme_info(const std::string &theme_name) {
  auto it = available_themes_.find(theme_name);
  if (it != available_themes_.end()) {
    return it->second;
  }
  return Theme{}; // Return empty theme if not found
}

std::string ThemeService::colorize(const std::string &text, ThemeColor color) {
  if (!is_color_supported()) {
    return text;
  }

  auto it = available_themes_.find(current_theme_name_);
  if (it == available_themes_.end()) {
    return text;
  }

  auto color_it = it->second.colors.find(color);
  if (color_it == it->second.colors.end()) {
    return text;
  }

  std::string ansi_code = get_ansi_color_code(color_it->second);
  return ansi_code + text + "\033[0m"; // Reset color after text
}

std::string ThemeService::colorize_command(const std::string &text) {
  return colorize(text, ThemeColor::PRIMARY);
}

std::string ThemeService::colorize_success(const std::string &text) {
  return colorize(text, ThemeColor::SUCCESS);
}

std::string ThemeService::colorize_error(const std::string &text) {
  return colorize(text, ThemeColor::ERROR);
}

std::string ThemeService::colorize_warning(const std::string &text) {
  return colorize(text, ThemeColor::WARNING);
}

std::string ThemeService::colorize_info(const std::string &text) {
  return colorize(text, ThemeColor::INFO);
}

std::string ThemeService::colorize_accent(const std::string &text) {
  return colorize(text, ThemeColor::ACCENT);
}

bool ThemeService::save_theme_config() {
  try {
    ensure_theme_directory();

    nlohmann::json config;
    config["current_theme"] = current_theme_name_;
    config["themes"] = nlohmann::json::object();

    for (const auto &[name, theme] : available_themes_) {
      nlohmann::json theme_json;
      theme_json["name"] = theme.name;
      theme_json["description"] = theme.description;
      theme_json["is_dark_theme"] = theme.is_dark_theme;
      theme_json["colors"] = nlohmann::json::object();

      for (const auto &[color_type, color_value] : theme.colors) {
        std::string color_key;
        switch (color_type) {
        case ThemeColor::PRIMARY:
          color_key = "primary";
          break;
        case ThemeColor::SECONDARY:
          color_key = "secondary";
          break;
        case ThemeColor::SUCCESS:
          color_key = "success";
          break;
        case ThemeColor::WARNING:
          color_key = "warning";
          break;
        case ThemeColor::ERROR:
          color_key = "error";
          break;
        case ThemeColor::INFO:
          color_key = "info";
          break;
        case ThemeColor::TEXT:
          color_key = "text";
          break;
        case ThemeColor::BACKGROUND:
          color_key = "background";
          break;
        case ThemeColor::ACCENT:
          color_key = "accent";
          break;
        case ThemeColor::MUTED:
          color_key = "muted";
          break;
        }
        theme_json["colors"][color_key] = color_value;
      }

      config["themes"][name] = theme_json;
    }

    std::ofstream file(get_theme_config_path());
    file << config.dump(2);

    return true;

  } catch (const std::exception &e) {
    std::cerr << "Failed to save theme config: " << e.what() << std::endl;
    return false;
  }
}

bool ThemeService::load_theme_config() {
  try {
    std::string config_path = get_theme_config_path();
    if (!std::filesystem::exists(config_path)) {
      return true; // Use defaults
    }

    std::ifstream file(config_path);
    nlohmann::json config;
    file >> config;

    if (config.contains("current_theme")) {
      current_theme_name_ = config["current_theme"];
    }

    // Load custom themes (built-in themes are already initialized)
    if (config.contains("themes")) {
      for (const auto &[name, theme_json] : config["themes"].items()) {
        // Skip built-in themes
        if (name == "default" || name == "dark" || name == "cyberpunk" ||
            name == "ocean") {
          continue;
        }

        Theme theme;
        theme.name = theme_json.value("name", name);
        theme.description = theme_json.value("description", "");
        theme.is_dark_theme = theme_json.value("is_dark_theme", false);

        if (theme_json.contains("colors")) {
          for (const auto &[color_key, color_value] :
               theme_json["colors"].items()) {
            ThemeColor color_type;
            if (color_key == "primary")
              color_type = ThemeColor::PRIMARY;
            else if (color_key == "secondary")
              color_type = ThemeColor::SECONDARY;
            else if (color_key == "success")
              color_type = ThemeColor::SUCCESS;
            else if (color_key == "warning")
              color_type = ThemeColor::WARNING;
            else if (color_key == "error")
              color_type = ThemeColor::ERROR;
            else if (color_key == "info")
              color_type = ThemeColor::INFO;
            else if (color_key == "text")
              color_type = ThemeColor::TEXT;
            else if (color_key == "background")
              color_type = ThemeColor::BACKGROUND;
            else if (color_key == "accent")
              color_type = ThemeColor::ACCENT;
            else if (color_key == "muted")
              color_type = ThemeColor::MUTED;
            else
              continue;

            theme.colors[color_type] = color_value;
          }
        }

        available_themes_[name] = theme;
      }
    }

    return true;

  } catch (const std::exception &e) {
    std::cerr << "Failed to load theme config: " << e.what() << std::endl;
    return false;
  }
}

bool ThemeService::create_custom_theme(const std::string &name,
                                       const Theme &theme) {
  available_themes_[name] = theme;
  return save_theme_config();
}

bool ThemeService::delete_custom_theme(const std::string &name) {
  // Don't allow deletion of built-in themes
  if (name == "default" || name == "dark" || name == "cyberpunk" ||
      name == "ocean") {
    return false;
  }

  auto it = available_themes_.find(name);
  if (it == available_themes_.end()) {
    return false;
  }

  available_themes_.erase(it);

  // If we're deleting the current theme, switch to default
  if (current_theme_name_ == name) {
    current_theme_name_ = "default";
  }

  return save_theme_config();
}

void ThemeService::print_theme_preview(const std::string &theme_name) {
  auto it = available_themes_.find(theme_name);
  if (it == available_themes_.end()) {
    std::cout << "Theme '" << theme_name << "' not found." << std::endl;
    return;
  }

  const Theme &theme = it->second;
  std::cout << "Theme: " << colorize_accent(theme.name) << std::endl;
  std::cout << "Description: " << theme.description << std::endl;
  std::cout << "Type: " << (theme.is_dark_theme ? "Dark" : "Light")
            << std::endl;
  std::cout << std::endl;

  std::cout << "Color Preview:" << std::endl;
  std::cout << "  " << colorize("Primary", ThemeColor::PRIMARY)
            << " - Commands and headers" << std::endl;
  std::cout << "  " << colorize("Success", ThemeColor::SUCCESS)
            << " - Success messages" << std::endl;
  std::cout << "  " << colorize("Warning", ThemeColor::WARNING)
            << " - Warning messages" << std::endl;
  std::cout << "  " << colorize("Error", ThemeColor::ERROR)
            << " - Error messages" << std::endl;
  std::cout << "  " << colorize("Info", ThemeColor::INFO)
            << " - Information messages" << std::endl;
  std::cout << "  " << colorize("Accent", ThemeColor::ACCENT)
            << " - Highlights and accents" << std::endl;
}

void ThemeService::reset_colors() { std::cout << "\033[0m" << std::flush; }

bool ThemeService::is_color_supported() {
  // Check if terminal supports colors
  const char *term = std::getenv("TERM");
  if (term == nullptr) {
    return false;
  }

  std::string term_str(term);
  return term_str.find("color") != std::string::npos ||
         term_str.find("xterm") != std::string::npos ||
         term_str.find("screen") != std::string::npos || term_str == "vt100" ||
         term_str == "vt220";
}
} // namespace Services
