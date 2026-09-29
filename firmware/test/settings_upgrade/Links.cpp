// Host definitions for what the settings model links against on the device: the SD file (a capture here), the
// obfuscation helpers (no obfuscated setting is in the fixtures) and the SD font registry (never given one).
#include <ObfuscationUtils.h>
#include <PersistableStore.h>
#include <SdCardFontRegistry.h>

#include "LastWrite.h"

std::string settings_upgrade::lastWrittenJson;

bool PersistableStoreBase::writeDocToFile(const char*, const JsonDocument& doc) {
  settings_upgrade::lastWrittenJson.clear();
  serializeJson(doc, settings_upgrade::lastWrittenJson);
  return true;
}

bool PersistableStoreBase::readDocFromFile(const char*, JsonDocument&) { return false; }

String obfuscation::obfuscateToBase64(const std::string& plaintext) { return plaintext; }

std::string obfuscation::deobfuscateFromBase64(const char*, const size_t, bool* ok, bool* tooLong) {
  if (ok) *ok = false;
  if (tooLong) *tooLong = false;
  return {};
}

const SdCardFontFamilyInfo* SdCardFontRegistry::findFamily(const std::string&) const { return nullptr; }

std::vector<uint8_t> SdCardFontFamilyInfo::availableSizes() const { return {}; }
