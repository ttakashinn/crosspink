#include <esp_app_desc.h>
#include <sdkconfig.h>

namespace {
template <size_t N>
constexpr void copyString(char (&destination)[N], const char* source) {
  size_t i = 0;
  for (; i + 1 < N && source[i] != '\0'; ++i) destination[i] = source[i];
  destination[i] = '\0';
}

constexpr esp_app_desc_t makeAppDescription() {
  esp_app_desc_t description{};
  description.magic_word = ESP_APP_DESC_MAGIC_WORD;
  description.secure_version = 0;
  copyString(description.version, CROSSPOINT_VERSION);
  copyString(description.project_name, "crosspink");
  copyString(description.time, __TIME__);
  copyString(description.date, __DATE__);
  copyString(description.idf_ver, IDF_VER);
  // app_elf_sha256 is deliberately left zero; esptool fills it from the final
  // ELF after linking.
  description.min_efuse_blk_rev_full = CONFIG_ESP_EFUSE_BLOCK_REV_MIN_FULL;
  description.max_efuse_blk_rev_full = CONFIG_ESP_EFUSE_BLOCK_REV_MAX_FULL;
  description.mmu_page_size = static_cast<uint8_t>(31 - __builtin_clz(CONFIG_MMU_PAGE_SIZE));
  return description;
}
}  // namespace

// The precompiled Arduino/ESP-IDF archive carries whichever PROJECT_VER was
// present when that archive was produced. With the custom SDK build cache this
// can lag behind platformio.ini, so the binary's standard ESP app descriptor
// used to report an old VNS release even though the UI and OTA comparator used
// CROSSPOINT_VERSION. esp_app_desc is weak in ESP-IDF; this strong definition
// makes the image metadata deterministic. The standard field is 32 bytes, so
// long development branch versions are safely truncated there; the UI keeps
// using the full CROSSPOINT_VERSION string.
extern "C" const __attribute__((section(".rodata_desc"), used)) esp_app_desc_t esp_app_desc = makeAppDescription();
