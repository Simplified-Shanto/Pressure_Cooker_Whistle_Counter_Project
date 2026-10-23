#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>

struct EEPROMStub {
  std::array<uint8_t, 32> stored{}, working{};
  unsigned commits = 0;
  bool failCommit = false;

  void erase() {
    stored.fill(0xFF);
    working = stored;
    commits = 0;
    failCommit = false;
  }
  bool begin(size_t bytes) {
    if (bytes != stored.size()) throw std::runtime_error("EEPROM allocation changed");
    working = stored;
    return true;
  }
  template <typename T> void get(size_t offset, T &value) {
    if (offset + sizeof(T) > working.size()) throw std::runtime_error("EEPROM read overflow");
    std::memcpy(&value, working.data() + offset, sizeof(T));
  }
  template <typename T> void put(size_t offset, const T &value) {
    if (offset + sizeof(T) > working.size()) throw std::runtime_error("EEPROM write overflow");
    std::memcpy(working.data() + offset, &value, sizeof(T));
  }
  bool commit() {
    ++commits;
    if (failCommit) return false;
    stored = working;
    return true;
  }
};
extern EEPROMStub EEPROM;
