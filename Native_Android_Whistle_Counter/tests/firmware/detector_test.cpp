#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <EEPROM.h>
#include "SettingsStore.h"
#include "WhistleDetector.h"

SerialStub Serial;
EEPROMStub EEPROM;
uint64_t clockMs = 0;
bool loud = false;
unsigned sampleNumber = 0;
uint32_t millis() { return static_cast<uint32_t>(clockMs); }
uint32_t micros() { return static_cast<uint32_t>(clockMs * 1000); }
int analogRead(uint8_t) { return loud ? (++sampleNumber % 2 ? 400 : 600) : 512; }

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

struct Rig {
  WhistleDetector detector;
  DetectorSettings settings;
  unsigned counts = 0;
  explicit Rig(uint64_t start = 0) {
    clockMs = start;
    loud = false;
    sampleNumber = 0;
    detector.begin();
  }
  void run(bool sound, uint32_t durationMs) {
    require(durationMs % 5 == 0, "Use actual 5 ms sampling");
    loud = sound;
    for (uint32_t elapsed = 0; elapsed < durationMs; elapsed += 5) {
      clockMs += 5;
      if (detector.update(settings)) ++counts;
    }
  }
  void whistle() { run(true, 1000); run(false, 2000); }
};

struct LegacySettings { uint32_t magic; uint16_t threshold, checksum; };
void installLegacy(uint16_t threshold) {
  LegacySettings legacy{Config::EEPROM_MAGIC, threshold,
                        static_cast<uint16_t>(threshold ^ 0xA55A)};
  std::memcpy(EEPROM.stored.data(), &legacy, sizeof(legacy));
}
void checkDefaults(const DetectorSettings &settings) {
  require(settings.minimumDurationMs == 500 && settings.quietDurationMs == 1500 &&
          settings.cooldownMs == 10000, "Expected researched starting defaults");
}

int main() {
  const std::vector<std::pair<std::string, std::function<void()>>> tests = {
    {"short noise rejected", [] {
      Rig rig; rig.run(true, 300); rig.run(false, 3000);
      require(rig.counts == 0, "300 ms noise must not qualify");
    }},
    {"first whistle accepted without startup cooldown", [] {
      Rig rig; rig.whistle(); require(rig.counts == 1, "First whistle lost");
    }},
    {"count waits for ending quiet", [] {
      Rig rig; rig.run(true, 1000); rig.run(false, 1000);
      require(rig.counts == 0, "Counted before 1.5 seconds of quiet");
      rig.run(false, 1000); require(rig.counts == 1, "Did not count after quiet");
    }},
    {"long continuous whistle counts only once", [] {
      Rig rig; rig.run(true, 30000);
      require(rig.counts == 0, "Continuous sound must wait for ending");
      rig.run(false, 20000); require(rig.counts == 1, "Long whistle split or lost");
    }},
    {"brief gaps merge into one whistle", [] {
      Rig rig; rig.run(true, 1000); rig.run(false, 600);
      rig.run(true, 1000); rig.run(false, 2000);
      require(rig.counts == 1, "Brief lull split one whistle");
    }},
    {"normal separated whistles counted", [] {
      Rig rig; rig.whistle(); rig.run(false, 11000); rig.whistle();
      require(rig.counts == 2, "Separated whistle lost");
    }},
    {"bursts inside cooldown ignored", [] {
      Rig rig; rig.whistle(); rig.whistle();
      require(rig.counts == 1, "Cooldown did not suppress repeat burst");
      rig.run(false, 11000); rig.whistle();
      require(rig.counts == 2, "Cooldown never rearmed");
    }},
    {"burst spanning cooldown expiry not counted halfway", [] {
      Rig rig; rig.whistle(); rig.run(false, 7000); rig.run(true, 7000);
      rig.run(false, 2000);
      require(rig.counts == 1, "Counted a burst that started during cooldown");
      rig.whistle(); require(rig.counts == 2, "Fresh post-quiet whistle lost");
    }},
    {"minimum duration tunable", [] {
      Rig rig; rig.settings.minimumDurationMs = 2000; rig.whistle();
      require(rig.counts == 0, "Runtime minimum duration ignored");
      rig.run(true, 3000); rig.run(false, 2000);
      require(rig.counts == 1, "Long enough sound rejected");
    }},
    {"quiet duration tunable", [] {
      Rig rig; rig.settings.quietDurationMs = 3000; rig.whistle();
      require(rig.counts == 0, "Runtime quiet duration ignored");
      rig.run(false, 2000); require(rig.counts == 1, "Long enough quiet rejected");
    }},
    {"cooldown tunable", [] {
      Rig rig; rig.settings.cooldownMs = 1000; rig.whistle();
      rig.run(false, 1000); rig.whistle();
      require(rig.counts == 2, "Runtime cooldown ignored");
    }},
    {"reset clears cooldown and old sampling window", [] {
      Rig rig; rig.whistle(); clockMs += 500000; rig.detector.reset();
      rig.whistle(); require(rig.counts == 2, "New session inherited cooldown or stale window");
    }},
    {"reset discards active unfinished whistle", [] {
      Rig rig; rig.run(true, 2000); rig.detector.reset(); rig.run(false, 3000);
      require(rig.counts == 0, "Previous session whistle leaked through reset");
      rig.whistle(); require(rig.counts == 1, "New session cannot count");
    }},
    {"millis wraparound preserves ending quiet", [] {
      Rig rig(uint64_t(UINT32_MAX) - 2000); rig.whistle();
      require(rig.counts == 1, "Whistle ending across wraparound lost");
      rig.run(false, 11000); rig.whistle();
      require(rig.counts == 2, "Cooldown across wraparound failed");
    }},
    {"millis wraparound preserves cooldown", [] {
      Rig rig(uint64_t(UINT32_MAX) - 5000); rig.whistle();
      rig.whistle(); require(rig.counts == 1, "Cooldown ended early across wraparound");
      rig.run(false, 11000); rig.whistle();
      require(rig.counts == 2, "Cooldown failed to expire across wraparound");
    }},
    {"erased EEPROM uses defaults without writing", [] {
      EEPROM.erase(); SettingsStore store; auto settings = store.load();
      checkDefaults(settings); require(settings.threshold == 65, "Default threshold wrong");
      require(EEPROM.commits == 0, "Boot must not write EEPROM");
    }},
    {"legacy threshold retained with new timing defaults", [] {
      EEPROM.erase(); installLegacy(32); SettingsStore store; auto settings = store.load();
      checkDefaults(settings); require(settings.threshold == 32, "Legacy threshold lost");
      require(EEPROM.commits == 0, "Migration must not write on boot");
    }},
    {"settings persist and old threshold layout remains compatible", [] {
      EEPROM.erase(); SettingsStore store; auto settings = store.load();
      settings.threshold = 38; settings.minimumDurationMs = 1200;
      settings.quietDurationMs = 2500; settings.cooldownMs = 60000;
      require(store.save(settings), "Save failed"); auto saved = store.load();
      require(saved.threshold == 38 && saved.minimumDurationMs == 1200 &&
              saved.quietDurationMs == 2500 && saved.cooldownMs == 60000,
              "Saved settings did not survive reload");
      LegacySettings legacy{}; EEPROM.get(0, legacy);
      require(legacy.magic == Config::EEPROM_MAGIC && legacy.threshold == 38 &&
              legacy.checksum == (38 ^ 0xA55A), "Legacy layout changed");
    }},
    {"corrupt timing falls back while retaining threshold", [] {
      EEPROM.erase(); SettingsStore store; auto settings = store.load();
      settings.threshold = 32; settings.minimumDurationMs = 1500;
      require(store.save(settings), "Save failed"); EEPROM.stored[15] ^= 0x80;
      auto saved = store.load(); checkDefaults(saved);
      require(saved.threshold == 32, "Timing corruption destroyed threshold");
    }},
    {"corrupt threshold does not destroy valid timing", [] {
      EEPROM.erase(); SettingsStore store; auto settings = store.load();
      settings.minimumDurationMs = 1200; require(store.save(settings), "Save failed");
      EEPROM.stored[6] ^= 1; auto saved = store.load();
      require(saved.threshold == 65 && saved.minimumDurationMs == 1200,
              "Independent settings records did not recover separately");
    }},
    {"unknown timing format falls back", [] {
      EEPROM.erase(); SettingsStore store; auto settings = store.load();
      settings.cooldownMs = 20000; require(store.save(settings), "Save failed");
      EEPROM.stored[12] = 2; checkDefaults(store.load());
    }},
    {"out of range settings rejected without writes", [] {
      EEPROM.erase(); SettingsStore store; auto settings = store.load();
      const std::vector<std::pair<uint16_t DetectorSettings::*, uint16_t>> bad = {
        {&DetectorSettings::threshold, 24}, {&DetectorSettings::threshold, 901},
        {&DetectorSettings::minimumDurationMs, 99}, {&DetectorSettings::minimumDurationMs, 5001},
        {&DetectorSettings::quietDurationMs, 99}, {&DetectorSettings::quietDurationMs, 5001},
        {&DetectorSettings::cooldownMs, 999}, {&DetectorSettings::cooldownMs, 60001}};
      for (auto entry : bad) {
        auto invalid = settings; invalid.*(entry.first) = entry.second;
        require(!store.save(invalid), "Invalid setting accepted");
      }
      require(EEPROM.commits == 0, "Invalid settings committed");
    }},
    {"failed commit reported and previous settings retained", [] {
      EEPROM.erase(); SettingsStore store; auto settings = store.load();
      settings.threshold = 32; require(store.save(settings), "Initial save failed");
      EEPROM.failCommit = true; settings.cooldownMs = 20000;
      require(!store.save(settings), "Failed commit reported as successful");
      auto saved = store.load(); checkDefaults(saved);
      require(saved.threshold == 32, "Failed save lost previous threshold");
    }}
  };
  unsigned passed = 0;
  for (const auto &test : tests) {
    try { test.second(); ++passed; std::cout << "PASS " << test.first << '\n'; }
    catch (const std::exception &error) {
      std::cerr << "FAIL " << test.first << ": " << error.what() << '\n'; return 1;
    }
  }
  std::cout << passed << " firmware tests passed\n";
}
