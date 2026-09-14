#pragma once
#include <map>
#include <string>
#include <vector>

namespace Services {

enum class ThemeColor {
  PRIMARY,
  SECONDARY,
  SUCCESS,
  WARNING,
  ERROR,
  INFO,
  TEXT,
  BACKGROUND,
  ACCENT,
  MUTED
};

struct Theme {
  std::string name;
  std::string description;
  std::map<ThemeColor, std::string> colors;
  bool is_dark_theme = false;
};

class ThemeService {
private:
  static std::string current_theme_name_;
  static std::map<std::string, Theme> available_themes_;
  static std::string get_theme_config_path();
  static void ensure_theme_directory();
  static void initialize_default_themes();
  static std::string get_ansi_color_code(const std::string &color);

public:
  // Theme management
  static void initialize();
  static bool set_theme(const std::string &theme_name);
  static std::string get_current_theme();
  static std::vector<std::string> list_available_themes();
  static Theme get_theme_info(const std::string &theme_name);

  // Color utilities
  static std::string colorize(const std::string &text, ThemeColor color);
  static std::string colorize_command(const std::string &text);
  static std::string colorize_success(const std::string &text);
  static std::string colorize_error(const std::string &text);
  static std::string colorize_warning(const std::string &text);
  static std::string colorize_info(const std::string &text);
  static std::string colorize_accent(const std::string &text);

  // Theme persistence
  static bool save_theme_config();
  static bool load_theme_config();

  // Theme creation
  static bool create_custom_theme(const std::string &name, const Theme &theme);
  static bool delete_custom_theme(const std::string &name);

  // Utility functions
  static void print_theme_preview(const std::string &theme_name);
  static void reset_colors();
  static bool is_color_supported();
};
} // namespace Services
