#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <Preferences.h>
#include <esp_bt_defs.h>

namespace stackchan_bluetooth {

static constexpr size_t kAddressTextSize = 18;

inline bool hasAddress(const esp_bd_addr_t address)
{
  static const esp_bd_addr_t empty = { 0, 0, 0, 0, 0, 0 };
  return memcmp(address, empty, ESP_BD_ADDR_LEN) != 0;
}

inline bool addressesEqual(const esp_bd_addr_t lhs, const esp_bd_addr_t rhs)
{
  return memcmp(lhs, rhs, ESP_BD_ADDR_LEN) == 0;
}

inline const char* formatAddress(const esp_bd_addr_t address,
                                 char (&text)[kAddressTextSize])
{
  snprintf(text, sizeof(text), "%02x:%02x:%02x:%02x:%02x:%02x",
           address[0], address[1], address[2],
           address[3], address[4], address[5]);
  return text;
}

class PeerStore
{
public:
  enum class LoadResult : uint8_t {
    Loaded,
    LegacyPeerCleared,
    StorageError
  };

  enum class LockResult : uint8_t {
    Locked,
    AlreadyLocked,
    Conflict,
    StorageError
  };

  bool isLoaded() const { return _loaded; }
  bool hasPeer() const { return hasAddress(_peer); }
  const esp_bd_addr_t& peer() const { return _peer; }
  bool matches(const esp_bd_addr_t peer) const { return addressesEqual(_peer, peer); }

  LoadResult load()
  {
    if (_loaded) {
      return LoadResult::Loaded;
    }

    LoadResult result = LoadResult::Loaded;
    Preferences preferences;
    if (!preferences.begin(preferenceNamespace(), false)) {
      _loaded = true;
      return LoadResult::StorageError;
    }

    const uint8_t stored_schema = preferences.getUChar(schemaKey(), 0);
    if (stored_schema != schemaVersion()) {
      // 旧版はライブラリ側のlast_bdaを移行していたため、誤登録の可能性がある
      // 接続先を一度だけ破棄します。
      preferences.remove(peerKey());
      if (preferences.putUChar(schemaKey(), schemaVersion()) != sizeof(uint8_t)) {
        result = LoadResult::StorageError;
      } else {
        result = LoadResult::LegacyPeerCleared;
      }
    } else if (preferences.getBytesLength(peerKey()) == ESP_BD_ADDR_LEN) {
      if (preferences.getBytes(peerKey(), _peer, ESP_BD_ADDR_LEN)
          != ESP_BD_ADDR_LEN) {
        memset(_peer, 0, sizeof(_peer));
        result = LoadResult::StorageError;
      }
    }

    preferences.end();
    _loaded = true;
    return result;
  }

  LockResult lockIfEmpty(const esp_bd_addr_t peer)
  {
    if (!_loaded) {
      load();
    }
    if (hasPeer()) {
      return matches(peer) ? LockResult::AlreadyLocked : LockResult::Conflict;
    }

    Preferences preferences;
    if (!preferences.begin(preferenceNamespace(), false)) {
      return LockResult::StorageError;
    }

    const bool peer_saved =
        preferences.putBytes(peerKey(), peer, ESP_BD_ADDR_LEN) == ESP_BD_ADDR_LEN;
    const bool schema_saved =
        preferences.putUChar(schemaKey(), schemaVersion()) == sizeof(uint8_t);
    preferences.end();
    if (!peer_saved || !schema_saved) {
      return LockResult::StorageError;
    }

    memcpy(_peer, peer, ESP_BD_ADDR_LEN);
    return LockResult::Locked;
  }

  bool clear()
  {
    memset(_peer, 0, sizeof(_peer));
    _loaded = true;

    Preferences preferences;
    if (!preferences.begin(preferenceNamespace(), false)) {
      return false;
    }
    preferences.remove(peerKey());
    preferences.end();
    return true;
  }

private:
  static const char* preferenceNamespace() { return "stackchan-bt"; }
  static const char* peerKey() { return "peer"; }
  static const char* schemaKey() { return "peer-version"; }
  static constexpr uint8_t schemaVersion() { return 2; }

  bool _loaded = false;
  esp_bd_addr_t _peer = { 0, 0, 0, 0, 0, 0 };
};

}  // namespace stackchan_bluetooth
