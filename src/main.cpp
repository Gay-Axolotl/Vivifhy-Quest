#include "main.hpp"
#include "VivifyRuntime.hpp"
#include <string_view>
#include <fstream>
#include <mutex>
#include <filesystem>
#include "HMUI/ViewController.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Transform.hpp"
#include "bsml/shared/BSML-Lite/Creation/Layout.hpp"
#include "bsml/shared/BSML-Lite/Creation/Settings.hpp"
#include "bsml/shared/BSML/Settings/BSMLSettings.hpp"
#include "custom-types/shared/register.hpp"
#include "scotland2/shared/modloader.h"

static modloader::ModInfo modInfo{MOD_ID, VERSION, 0};

namespace {
constexpr std::string_view kMultipassRenderingConfigKey = "multipassRendering";
constexpr std::string_view kVivifyDebugLoggingConfigKey = "vivifyDebugLogging";
constexpr std::string_view kDisableBeat0FilmgrainBlitConfigKey = "disableBeat0FilmgrainBlit";
constexpr std::string_view kDisableAllBlitsConfigKey = "disableAllBlits";
constexpr std::string_view kDisableCreateCameraDepthConfigKey = "disableCreateCameraDepth";
bool gMultipassRenderingEnabled = true;
bool gVivifyDebugLogging = false;
bool gDisableBeat0FilmgrainBlit = false;
bool gDisableAllBlits = false;
bool gDisableCreateCameraDepth = false;

constexpr std::string_view kVivifyLogDir = "/sdcard/ModData/com.beatgames.beatsaber/Logs";
constexpr std::string_view kVivifyLogPath = "/sdcard/ModData/com.beatgames.beatsaber/Logs/Vivify.log";
std::ofstream gVivifyLogFile;
std::mutex gVivifyLogMutex;
bool gVivifyLogSinkInstalled = false;

void InstallVivifyFileLogSink() {
  if (gVivifyLogSinkInstalled) return;
  gVivifyLogSinkInstalled = true;
  std::error_code ec;
  std::filesystem::create_directories(std::string(kVivifyLogDir), ec);

  gVivifyLogFile.open(std::string(kVivifyLogPath), std::ios::out | std::ios::trunc);
  if (!gVivifyLogFile.is_open()) {
    PaperLogger.warn("Vivify: could not open log file at {} (logging to logcat only)", kVivifyLogPath);
    return;
  }
  gVivifyLogFile << "=== Vivify " << VERSION << " session log ===\n";
  gVivifyLogFile.flush();

  Paper::Logger::AddLogSink([](Paper::LogData const& data) {
    if (!data.tag.has_value() || *data.tag != std::string_view(MOD_ID)) return;
    std::lock_guard<std::mutex> lock(gVivifyLogMutex);
    if (!gVivifyLogFile.is_open()) return;
    gVivifyLogFile << '[' << Paper::format_as(data.level) << "] " << data.message << '\n';
    gVivifyLogFile.flush();
  });
}

void EnsureConfigObject() {
  auto& doc = getConfig().config;
  if (!doc.IsObject()) {
    doc.SetObject();
  }
}

bool EnsureBoolConfigValue(std::string_view key, bool defaultValue, bool& value) {
  auto& doc = getConfig().config;
  auto it = doc.FindMember(key.data());
  if (it != doc.MemberEnd() && it->value.IsBool()) {
    value = it->value.GetBool();
    return false;
  }

  auto& allocator = doc.GetAllocator();
  value = defaultValue;
  if (it == doc.MemberEnd()) {
    doc.AddMember(rapidjson::Value(key.data(), allocator), rapidjson::Value(defaultValue), allocator);
  } else {
    it->value.SetBool(defaultValue);
  }
  return true;
}

void SetBoolConfigValue(std::string_view key, bool enabled, bool& value) {
  auto& config = getConfig();
  auto& doc = config.config;
  EnsureConfigObject();
  auto& allocator = doc.GetAllocator();
  auto it = doc.FindMember(key.data());
  if (it == doc.MemberEnd()) {
    doc.AddMember(rapidjson::Value(key.data(), allocator), rapidjson::Value(enabled), allocator);
  } else {
    it->value.SetBool(enabled);
  }
  value = enabled;
  config.Write();
}

void RegisterModSettings() {
  BSML::BSMLSettings::get_instance()->TryAddSettingsMenu(
      [](HMUI::ViewController* viewController, bool firstActivation, bool, bool) {
        if (!firstActivation || viewController == nullptr) return;
        auto* container = BSML::Lite::CreateScrollableSettingsContainer(viewController->get_transform());
        if (container == nullptr) return;
        BSML::Lite::CreateToggle(
            container->get_transform(), u"Debug logging", GetVivifyDebugLogging(),
            [](bool value) { SetBoolConfigValue(kVivifyDebugLoggingConfigKey, value, gVivifyDebugLogging); });
      },
      "Vivify", false);
}
}

Configuration &getConfig() {
  static Configuration config(modInfo);
  return config;
}

bool GetMultipassRenderingEnabled() {
  return gMultipassRenderingEnabled;
}

bool GetVivifyDebugLogging() {
  return gVivifyDebugLogging;
}

bool GetDisableBeat0FilmgrainBlit() {
  return gDisableBeat0FilmgrainBlit;
}

bool GetDisableAllBlits() {
  return gDisableAllBlits;
}

bool GetDisableCreateCameraDepth() {
  return gDisableCreateCameraDepth;
}

void EnsureConfigDefaults() {
  auto& config = getConfig();
  auto& doc = config.config;
  EnsureConfigObject();
  bool needsWrite = false;

  needsWrite |= EnsureBoolConfigValue(kMultipassRenderingConfigKey, false, gMultipassRenderingEnabled);

  needsWrite |= EnsureBoolConfigValue(kVivifyDebugLoggingConfigKey, false, gVivifyDebugLogging);
  needsWrite |= EnsureBoolConfigValue(kDisableBeat0FilmgrainBlitConfigKey, false, gDisableBeat0FilmgrainBlit);
  needsWrite |= EnsureBoolConfigValue(kDisableAllBlitsConfigKey, false, gDisableAllBlits);
  needsWrite |= EnsureBoolConfigValue(kDisableCreateCameraDepthConfigKey, false, gDisableCreateCameraDepth);
  if (needsWrite) {
    config.Write();
  }
}

MOD_EXTERN_FUNC void setup(CModInfo *info) noexcept {
  *info = modInfo.to_c();
  InstallVivifyFileLogSink();
  getConfig().Load();
  EnsureConfigDefaults();

  gMultipassRenderingEnabled = false;

  gVivifyDebugLogging = false;
  PaperLogger.info("Vivify file logging active -> {}", kVivifyLogPath);
}
MOD_EXTERN_FUNC void late_load() noexcept {
  il2cpp_functions::Init();
  custom_types::Register::AutoRegister();
  RegisterModSettings();
  Vivify::LateLoad();
}
