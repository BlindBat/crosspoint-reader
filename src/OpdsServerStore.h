#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>
#include <vector>

struct OpdsServer {
  std::string name;
  std::string url;
  std::string username;
  std::string password;  // Plaintext in memory; obfuscated with hardware key on disk

  // Used to drop a save that would rewrite opds.json unchanged: the server
  // editor persists after every field, including the fields the keyboard
  // returned untouched.
  bool operator==(const OpdsServer& other) const {
    return name == other.name && url == other.url && username == other.username && password == other.password;
  }
  bool operator!=(const OpdsServer& other) const { return !(*this == other); }
};

/**
 * Singleton class for storing OPDS server configurations on the SD card.
 * Passwords are XOR-obfuscated with the device's unique hardware MAC address
 * and base64-encoded before writing to JSON.
 */
class OpdsServerStore : public PersistableStore<OpdsServerStore> {
 private:
  std::vector<OpdsServer> servers;

  // Set when a save failed, so the list in memory is ahead of opds.json. The
  // unchanged-record guard in updateServer() must not skip the write that would
  // retry it, or an identical re-save reports success over a stale file.
  bool saveFailed = false;

  static constexpr size_t MAX_SERVERS = 8;

  // Writes opds.json and remembers whether it landed.
  bool persist();

  OpdsServerStore() = default;

  friend class PersistableStore<OpdsServerStore>;

 public:
  static const char* getFilePath() { return "/.crosspoint/opds.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  bool addServer(const OpdsServer& server);
  bool updateServer(size_t index, const OpdsServer& server);
  bool removeServer(size_t index);

  const std::vector<OpdsServer>& getServers() const { return servers; }
  const OpdsServer* getServer(size_t index) const;
  size_t getCount() const { return servers.size(); }
  bool hasServers() const { return !servers.empty(); }
};

#define OPDS_STORE OpdsServerStore::getInstance()
