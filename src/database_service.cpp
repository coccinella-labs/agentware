#include "database_service.h"
#include <iostream>
#include <vector>

namespace agentware {

DatabaseService::DatabaseService()
#ifdef HAVE_PQXX
    : connection_(nullptr)
#endif
{
}

DatabaseService::~DatabaseService() { disconnect(); }

bool DatabaseService::connect(const std::string &host, int port,
                              const std::string &dbname,
                              const std::string &user,
                              const std::string &password) {
#ifdef HAVE_PQXX
  try {
    std::string connection_string =
        "host=" + host + " port=" + std::to_string(port) + " dbname=" + dbname +
        " user=" + user + " password=" + password;

    connection_ = std::make_unique<pqxx::connection>(connection_string);

    // The pqxx::connection constructor throws on failure, so if we get here,
    // the connection is open.
    std::cout << "Connected to database: " << connection_->dbname()
              << std::endl;
    return true;
  } catch (const std::exception &e) {
    std::cerr << "Database connection error: " << e.what() << std::endl;
    return false;
  }
#else
  (void)host;
  (void)port;
  (void)dbname;
  (void)user;
  (void)password;
  return false;
#endif
}

void DatabaseService::disconnect() {
#ifdef HAVE_PQXX
  connection_.reset();
#endif
}

bool DatabaseService::isConnected() const {
#ifdef HAVE_PQXX
  return connection_ && connection_->is_open();
#else
  return false;
#endif
}

bool DatabaseService::executeQuery(const std::string &query,
                                   const std::vector<std::string> &params) {
#ifdef HAVE_PQXX
  if (!isConnected()) {
    std::cerr << "Not connected to database" << std::endl;
    return false;
  }

  try {
    pqxx::work txn(*connection_);
#if defined(__APPLE__)
    pqxx::params p;
    for (const auto &param : params) {
      p.append(param);
    }
    txn.exec(query, p);
#else
    std::string q = query;
    for (size_t i = 0; i < params.size(); ++i) {
      std::string placeholder = "$" + std::to_string(i + 1);
      size_t pos = q.find(placeholder);
      if (pos != std::string::npos) {
        q.replace(pos, placeholder.size(), txn.quote(params[i]));
      }
    }
    txn.exec(q);
#endif
    txn.commit();
    return true;
  } catch (const std::exception &e) {
    std::cerr << "Query execution error: " << e.what() << std::endl;
    return false;
  }
#else
  (void)query;
  (void)params;
  std::cerr << "Database support is disabled as libpqxx is not available."
            << std::endl;
  return false;
#endif
}

std::unique_ptr<pqxx::result>
DatabaseService::executeSelect(const std::string &query,
                               const std::vector<std::string> &params) {
#ifdef HAVE_PQXX
  if (!isConnected()) {
    std::cerr << "Not connected to database" << std::endl;
    return nullptr;
  }

  try {
    pqxx::work txn(*connection_);
#if defined(__APPLE__)
    pqxx::params p;
    for (const auto &param : params) {
      p.append(param);
    }
    auto result = txn.exec(query, p);
    txn.commit();
    return std::make_unique<pqxx::result>(std::move(result));
#else
    std::string q = query;
    for (size_t i = 0; i < params.size(); ++i) {
      std::string placeholder = "$" + std::to_string(i + 1);
      size_t pos = q.find(placeholder);
      if (pos != std::string::npos) {
        q.replace(pos, placeholder.size(), txn.quote(params[i]));
      }
    }
    auto result = txn.exec(q);
    txn.commit();
    return std::make_unique<pqxx::result>(std::move(result));
#endif
  } catch (const std::exception &e) {
    std::cerr << "Select query error: " << e.what() << std::endl;
    return nullptr;
  }
#else
  (void)query;
  (void)params;
  std::cerr << "Database support is disabled as libpqxx is not available."
            << std::endl;
  return nullptr;
#endif
}

} // namespace agentware
