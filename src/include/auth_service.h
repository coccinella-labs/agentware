#pragma once
#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Services {

struct AuthProvider {
  std::string name{};
  std::string display_name{};
  std::string api_key{};
  std::string base_url{};
  std::string model{};
  bool is_active = false;
  bool is_valid = false;
  std::map<std::string, std::string> additional_config{};
};

class AuthService {
private:
  static constexpr size_t key_size = 32;  // 256 bits for AES-256
  static constexpr size_t iv_size = 12;   // 96 bits for GCM
  static constexpr size_t tag_size = 16;  // 128-bit authentication tag
  static constexpr size_t SALT_SIZE = 32; // 256-bit salt for key derivation
  static constexpr size_t ITERATIONS = 100000; // Key derivation iterations

  static std::map<std::string, AuthProvider> providers;
  static std::string active_provider;
  static std::vector<unsigned char> encryption_key;
  static std::string key_file_path;

  static std::string get_auth_config_path();
  static void ensure_auth_directory();
  static std::string encrypt_credential(const std::string &credential);
  static std::string
  decrypt_credential(const std::string &encrypted_credential);
  static void initialize_default_providers();

  // Secure key management
  static bool initialize_encryption_key();
  static bool load_or_generate_key();
  static bool save_encryption_key();
  static bool derive_key(const std::string &password,
                         const std::vector<unsigned char> &salt,
                         std::vector<unsigned char> &key);
  static std::vector<unsigned char> generate_random_bytes(size_t length);
  static std::string bytes_to_hex(const std::vector<unsigned char> &bytes);
  static std::vector<unsigned char> hex_to_bytes(const std::string &hex);

public:
  // Provider management
  static void initialize();
  static bool add_provider(const AuthProvider &provider);
  static bool remove_provider(const std::string &provider_name);
  static bool set_active_provider(const std::string &provider_name);
  static std::string get_active_provider();
  static std::vector<std::string> list_providers();
  static AuthProvider get_provider_info(const std::string &provider_name);

  // Credential management
  static bool set_api_key(const std::string &provider_name,
                          const std::string &api_key);
  static std::string get_api_key(const std::string &provider_name = "");
  static bool validate_credentials(const std::string &provider_name);
  static void clear_credentials(const std::string &provider_name);

  // Configuration management
  static bool save_auth_config();
  static bool load_auth_config();
  static bool
  update_provider_config(const std::string &provider_name,
                         const std::map<std::string, std::string> &config);

  // Health checks
  static bool test_provider_connection(const std::string &provider_name);
  static std::string get_provider_status(const std::string &provider_name);
  static void refresh_all_provider_status();

  // Security utilities
  static bool is_credential_secure();
  static void generate_encryption_key();
  static bool backup_credentials(const std::string &backup_path);
  static bool restore_credentials(const std::string &backup_path);
};
} // namespace Services
