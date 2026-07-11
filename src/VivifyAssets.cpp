#include "VivifyRuntimeInternal.hpp"
#include "VivifyComponents.hpp"
#include <cstring>
#include <vector>

namespace Vivify {

namespace {

std::string ResolveBundlePath(std::string const& levelPath) {
  std::string bundlePath = JoinPath(levelPath, kBundleFile);
  if (std::filesystem::exists(bundlePath)) {
    return bundlePath;
  }
  std::error_code ec;
  for (auto const& entry : std::filesystem::directory_iterator(levelPath, ec)) {
    if (ec) break;
    auto const& p = entry.path();
    if (p.extension() == ".vivify") {
      return p.string();
    }
  }
  return {};
}

uint32_t ReadAndroidChecksumFromInfoDat(std::string const& levelPath) {
  std::string infoPath = JoinPath(levelPath, "Info.dat");
  if (!std::filesystem::exists(infoPath)) infoPath = JoinPath(levelPath, "info.dat");
  if (!std::filesystem::exists(infoPath)) return 0;
  std::ifstream ifs(infoPath);
  if (!ifs.is_open()) return 0;
  std::string str((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));
  rapidjson::Document doc;
  doc.Parse(str.c_str());
  if (doc.HasParseError()) return 0;
  rapidjson::Value const* customData = nullptr;
  if (doc.HasMember("_customData")) customData = &doc["_customData"];
  else if (doc.HasMember("customData")) customData = &doc["customData"];
  if (customData == nullptr || !customData->IsObject()) return 0;
  rapidjson::Value const* assetBundle = nullptr;
  if (customData->HasMember("_assetBundle")) assetBundle = &(*customData)["_assetBundle"];
  else if (customData->HasMember("assetBundle")) assetBundle = &(*customData)["assetBundle"];
  if (assetBundle == nullptr || !assetBundle->IsObject()) return 0;
  if (assetBundle->HasMember("_android2021") && (*assetBundle)["_android2021"].IsUint()) {
    return (*assetBundle)["_android2021"].GetUint();
  }
  if (assetBundle->HasMember("android2021") && (*assetBundle)["android2021"].IsUint()) {
    return (*assetBundle)["android2021"].GetUint();
  }
  return 0;
}

struct MaterialFallbackState {
  std::optional<UnityEngine::Color> color;
  UnityEngine::Texture* mainTexture = nullptr;
  int renderQueue = -1;
};

std::optional<UnityEngine::Color> ReadMaterialFallbackColor(UnityEngine::Material* material) {
  if (!IsManagedAlive(material)) return std::nullopt;
  static int const colorIds[] = {
      UnityEngine::Shader::PropertyToID(u"_Color"),
      UnityEngine::Shader::PropertyToID(u"_BaseColor"),
      UnityEngine::Shader::PropertyToID(u"_TintColor"),
      UnityEngine::Shader::PropertyToID(u"_MainColor"),
      UnityEngine::Shader::PropertyToID(u"_HorizonCol"),
      UnityEngine::Shader::PropertyToID(u"_SkyCol"),
      UnityEngine::Shader::PropertyToID(u"_EmissionColor"),
  };
  for (int id : colorIds) {
    if (material->HasProperty(id)) {
      return material->GetColor(id);
    }
  }
  auto names = material->GetPropertyNames(UnityEngine::MaterialPropertyType::Vector);
  if (!names) return std::nullopt;
  for (auto name : names) {
    if (!name) continue;
    std::string key = NormalizeAssetKey(ToStdString(name));
    if (key.find("color") == std::string::npos &&
        key.find("colour") == std::string::npos &&
        key.find("col") == std::string::npos) {
      continue;
    }
    return material->GetColor(name);
  }
  return std::nullopt;
}

MaterialFallbackState CaptureMaterialFallbackState(UnityEngine::Material* material) {
  MaterialFallbackState state;
  if (!IsManagedAlive(material)) return state;
  state.color = ReadMaterialFallbackColor(material);
  state.mainTexture = material->get_mainTexture().unsafePtr();
  state.renderQueue = material->get_renderQueue();
  return state;
}

void RestoreMaterialFallbackState(UnityEngine::Material* material, MaterialFallbackState const& state) {
  if (!IsManagedAlive(material)) return;
  if (state.color.has_value()) {
    static int const fallbackColorIds[] = {
        UnityEngine::Shader::PropertyToID(u"_Color"),
        UnityEngine::Shader::PropertyToID(u"_BaseColor"),
        UnityEngine::Shader::PropertyToID(u"_TintColor"),
   };
    for (int id : fallbackColorIds) {
      if (material->HasProperty(id)) {
        material->SetColor(id, state.color.value());
      }
    }
  }
  if (IsManagedAlive(state.mainTexture)) {
    material->set_mainTexture(state.mainTexture);
  }
  if (state.renderQueue >= 0) {
    material->set_renderQueue(state.renderQueue);
  }
}
}

void Runtime::HandleLevelSelected(SongCore::API::LevelSelect::LevelWasSelectedEventArgs const& event) {

  std::string incomingLevelPath;
  if (event.isCustom && event.customBeatmapLevel != nullptr) {
    incomingLevelPath = std::string(event.customBeatmapLevel->customLevelPath);
  }
  if (!incomingLevelPath.empty() && incomingLevelPath != _selectedLevelPath) {
    if (_mainBundle != nullptr && UnityEngine::Object::op_Implicit_bool(_mainBundle)) {
      _mainBundle->Unload(true);
      _mainBundle = nullptr;
    }
    _preloadedBundlePath.clear();
  }
  ResetRuntime();

  _activeSabers.clear();
  _selectedLevelPath.clear();
  _selectedMapHasVivifyRequirement = false;
  if (!event.isCustom || event.customBeatmapLevel == nullptr) {
    SongCore::API::PlayButton::EnablePlayButton("Vivify");
    return;
  }
  _selectedLevelPath = std::string(event.customBeatmapLevel->customLevelPath);
  if (GetVivifyDebugLogging()) {
    PaperLogger.info("Vivify level selected: path='{}' isCustom={} hasDetails={}",
                     _selectedLevelPath, BoolText(event.isCustom), BoolText(event.customLevelDetails.has_value()));
  }
  if (event.customLevelDetails) {
    auto const& requirements = event.customLevelDetails->difficultyDetails.requirements;
    _selectedMapHasVivifyRequirement = std::any_of(requirements.begin(), requirements.end(), [](std::string const& requirement) {
      return requirement == kCapability;
    });
  }
  if (!_selectedMapHasVivifyRequirement) {
    MetaCore::Game::SetScoreSubmission("Vivify", true);
    SongCore::API::PlayButton::EnablePlayButton("Vivify");
    return;
  }

  MetaCore::Game::SetScoreSubmission("Vivify", false);

  std::string const androidBundlePath = JoinPath(_selectedLevelPath, std::string(kBundleFile));
  bool const hasAndroidBundle = std::filesystem::exists(androidBundlePath);
  std::string bundlePath = ResolveBundlePath(_selectedLevelPath);

  if (hasAndroidBundle) {
    if (GetVivifyDebugLogging()) {
      PaperLogger.info("Vivify bundle selection: using local Android bundle '{}'", androidBundlePath);
    }
    SongCore::API::PlayButton::EnablePlayButton("Vivify");

    PreloadBundle(androidBundlePath);
    return;
  }

  if (!bundlePath.empty()) {

    SongCore::API::PlayButton::DisablePlayButton("Vivify", "No Android asset bundle for this map.");
    return;
  }

  uint32_t const androidChecksum = ReadAndroidChecksumFromInfoDat(_selectedLevelPath);
  if (GetVivifyDebugLogging()) {
    PaperLogger.info("Vivify bundle selection: no local bundle in '{}' android2021={}",
                     _selectedLevelPath, androidChecksum);
  }
  if (androidChecksum != 0) {
    SongCore::API::PlayButton::DisablePlayButton("Vivify", "Downloading assets...");
    std::string levelPath = _selectedLevelPath;
    DownloadBundle(androidChecksum, levelPath, [this, levelPath](bool success) {
      if (success) {

        if (levelPath == _selectedLevelPath) {
          std::string downloaded = ResolveBundlePath(levelPath);
          if (!downloaded.empty()) {
            PreloadBundle(downloaded);
          }
        }
        SongCore::API::PlayButton::EnablePlayButton("Vivify");
      } else {
        SongCore::API::PlayButton::DisablePlayButton("Vivify", "Failed to download assets.");
      }
    });
  } else {
    SongCore::API::PlayButton::DisablePlayButton("Vivify", "This map does not support your game version.");
  }
}

void Runtime::DownloadBundle(uint32_t checksum, std::string const& levelPath, std::function<void(bool)> callback) {
  std::string url = "https://repo.totalbs.dev/api/v1/bundles/" + std::to_string(checksum);
  std::string bundlePath = JoinPath(levelPath, kBundleFile);
  if (GetVivifyDebugLogging()) {
    PaperLogger.info("Vivify bundle download: android2021={} metadataUrl='{}' cachePath='{}'",
                     checksum, url, bundlePath);
  }
  WebUtils::GetAsync<WebUtils::StringResponse>(WebUtils::URLOptions(url), [bundlePath, callback, url](WebUtils::StringResponse res) {
    if (!res.IsSuccessful() || !res.responseData.has_value()) {
      if (GetVivifyDebugLogging()) {
        PaperLogger.warn("Vivify bundle download failed: metadata request unsuccessful url='{}'", url);
      }
      BSML::MainThreadScheduler::Schedule([callback] { callback(false); });
      return;
    }
    rapidjson::Document doc;
    doc.Parse(res.responseData->c_str());
    if (doc.HasParseError() || !doc.HasMember("downloadUrl") || !doc["downloadUrl"].IsString()) {
      if (GetVivifyDebugLogging()) {
        PaperLogger.warn("Vivify bundle download failed: metadata response did not contain downloadUrl");
      }
      BSML::MainThreadScheduler::Schedule([callback] { callback(false); });
      return;
    }
    std::string downloadUrl = doc["downloadUrl"].GetString();
    if (GetVivifyDebugLogging()) {
      PaperLogger.info("Vivify bundle download URL resolved: '{}'", downloadUrl);
    }
    WebUtils::GetAsync<WebUtils::DataResponse>(WebUtils::URLOptions(downloadUrl), [bundlePath, callback](WebUtils::DataResponse dataRes) {
      if (!dataRes.IsSuccessful() || !dataRes.responseData.has_value()) {
        if (GetVivifyDebugLogging()) {
          PaperLogger.warn("Vivify bundle download failed: data request unsuccessful path='{}'", bundlePath);
        }
        BSML::MainThreadScheduler::Schedule([callback] { callback(false); });
        return;
      }
      std::ofstream os(bundlePath, std::ios::binary);
      bool const written = os.is_open();
      if (written) {
        os.write(reinterpret_cast<char const*>(dataRes.responseData->data()),
                 static_cast<std::streamsize>(dataRes.responseData->size()));
        os.close();
      }
      if (GetVivifyDebugLogging()) {
        PaperLogger.info("Vivify bundle download complete: path='{}' bytes={} written={}",
                         bundlePath, dataRes.responseData->size(), BoolText(written));
      }
      BSML::MainThreadScheduler::Schedule([callback, written] { callback(written); });
    });
  });
}

void Runtime::CacheBundleAssets() {
  if (_mainBundle == nullptr || !UnityEngine::Object::op_Implicit_bool(_mainBundle)) return;
  auto assetNames = _mainBundle->GetAllAssetNames();
  if (!assetNames) return;
  _assets.clear();
  _assetsByName.clear();
  _supportedShadersByName.clear();
  for (auto assetName : assetNames) {
    if (!assetName) continue;
    std::string originalAssetPath = il2cpp_utils::detail::to_string(assetName);
    std::string key = NormalizeAssetKey(originalAssetPath);
    auto asset = _mainBundle->LoadAsset(assetName);
    if (asset == nullptr) {
      if (GetVivifyDebugLogging()) {
        PaperLogger.warn("Vivify asset load failed: path='{}'", originalAssetPath);
      }
      continue;
    }
    if (!key.empty()) _assets[key] = asset;
    auto name = asset->get_name();
    if (name) {
      auto nameKey = NormalizeAssetKey(il2cpp_utils::detail::to_string(name));
      if (!nameKey.empty() && !_assetsByName.contains(nameKey)) {
        _assetsByName[nameKey] = asset;
      }
      if (auto* shader = il2cpp_utils::try_cast<UnityEngine::Shader>(asset.unsafePtr()).value_or(nullptr);
          IsAlive(shader) && shader->get_isSupported() && !nameKey.empty()) {
        _supportedShadersByName[nameKey] = shader;
      }
    }
    if (GetVivifyDebugLogging()) {
      if (auto* material = il2cpp_utils::try_cast<UnityEngine::Material>(asset.unsafePtr()).value_or(nullptr);
          IsAlive(material)) {
        LogMaterialShader("bundle-load", originalAssetPath, material);
      } else if (auto* shader = il2cpp_utils::try_cast<UnityEngine::Shader>(asset.unsafePtr()).value_or(nullptr);
                 IsAlive(shader)) {
        PaperLogger.info("Vivify shader asset: path='{}' shader='{}' supported={}",
                         originalAssetPath, ShaderNameForLog(shader), BoolText(shader->get_isSupported()));
      }
    }
  }
}

void Runtime::PreloadBundle(std::string const& bundlePath) {
  if (_preloadedBundlePath == bundlePath && _mainBundle != nullptr &&
      UnityEngine::Object::op_Implicit_bool(_mainBundle)) {
    if (GetVivifyDebugLogging()) {
      PaperLogger.info("Vivify bundle already preloaded: '{}'", bundlePath);
    }
    return;
  }
  if (_mainBundle != nullptr && UnityEngine::Object::op_Implicit_bool(_mainBundle)) {
    _mainBundle->Unload(true);
    _mainBundle = nullptr;
  }
  _preloadedBundlePath = bundlePath;
  _mainBundle = UnityEngine::AssetBundle::LoadFromFile(StringW(bundlePath));
  if (_mainBundle == nullptr) {
    if (GetVivifyDebugLogging()) {
      PaperLogger.warn("Vivify bundle preload failed: '{}'", bundlePath);
    }
    _preloadedBundlePath.clear();
    return;
  }
  if (GetVivifyDebugLogging()) {
    PaperLogger.info("Vivify bundle preloaded: '{}'", bundlePath);
  }
  CacheBundleAssets();
}

void Runtime::LoadMainBundle() {
  LogUnityPlatformInfoOnce();
  if (_selectedLevelPath.empty()) {
    if (_selectedMapHasVivifyRequirement && GetVivifyDebugLogging()) {
      PaperLogger.warn("Vivify bundle load skipped: selected level path is empty");
    }
    return;
  }
  std::string bundlePath = ResolveBundlePath(_selectedLevelPath);
  if (bundlePath.empty()) {
    if (GetVivifyDebugLogging()) {
      PaperLogger.warn("Vivify bundle not found in '{}' (no *.vivify file)", _selectedLevelPath);
    }
    return;
  }
  if (!_preloadedBundlePath.empty() && _preloadedBundlePath == bundlePath &&
      _mainBundle != nullptr && UnityEngine::Object::op_Implicit_bool(_mainBundle)) {
    if (GetVivifyDebugLogging()) {
      PaperLogger.info("Vivify bundle preloaded, rebuilding asset caches: '{}'", bundlePath);
    }
    CacheBundleAssets();
    RepairLoadedMaterialShaders();
    return;
  }
  if (GetVivifyDebugLogging()) {
    PaperLogger.info("Vivify loading asset bundle: path='{}'", bundlePath);
  }
  _mainBundle = UnityEngine::AssetBundle::LoadFromFile(StringW(bundlePath));
  if (_mainBundle == nullptr) {
    if (GetVivifyDebugLogging()) {
      PaperLogger.warn("Vivify asset bundle load failed: path='{}'", bundlePath);
    }
    return;
  }
  _preloadedBundlePath = bundlePath;
  CacheBundleAssets();
  RepairLoadedMaterialShaders();
}

UnityEngine::Object* Runtime::GetAssetObject(std::string_view assetName) const {
  auto it = _assets.find(NormalizeAssetKey(assetName));
  if (it != _assets.end()) {
    return it->second;
  }
  auto nameIt = _assetsByName.find(NormalizeAssetKey(assetName));
  if (nameIt != _assetsByName.end()) {
    return nameIt->second;
  }
  if (GetVivifyDebugLogging()) {
    PaperLogger.warn("Vivify asset lookup miss: '{}'", std::string(assetName));
  }
  return nullptr;
}

void Runtime::LogUnityPlatformInfoOnce() {
  if (!GetVivifyDebugLogging() || _loggedUnityPlatformInfo) return;
  _loggedUnityPlatformInfo = true;
  auto stereoMode = UnityEngine::XR::XRSettings::get_stereoRenderingMode();
  auto graphicsType = UnityEngine::SystemInfo::get_graphicsDeviceType();
  PaperLogger.info(
      "Vivify platform: os='{}' device='{}' gpu='{}' vendor='{}' api={} stereoMode={} xrOcclusionMesh={} supportsInstancing={} supportsR8={} supportsDepthRT={}",
      ToStdString(UnityEngine::SystemInfo::get_operatingSystem()),
      ToStdString(UnityEngine::SystemInfo::get_deviceModel()),
      ToStdString(UnityEngine::SystemInfo::get_graphicsDeviceName()),
      ToStdString(UnityEngine::SystemInfo::get_graphicsDeviceVendor()),
      graphicsType.value__,
      stereoMode.value__,
      BoolText(UnityEngine::XR::XRSettings::get_useOcclusionMesh()),
      BoolText(UnityEngine::SystemInfo::get_supportsInstancing()),
      BoolText(UnityEngine::SystemInfo::SupportsRenderTextureFormat(UnityEngine::RenderTextureFormat::R8)),
      BoolText(UnityEngine::SystemInfo::SupportsRenderTextureFormat(UnityEngine::RenderTextureFormat::Depth)));
}

UnityEngine::RenderTextureFormat Runtime::SupportedRenderTextureFormat(UnityEngine::RenderTextureFormat requested,
                                                                       std::string_view context) const {
  if (UnityEngine::SystemInfo::SupportsRenderTextureFormat(requested)) {
    return requested;
  }
  auto fallback = UnityEngine::RenderTextureFormat::ARGB32;
  if (GetVivifyDebugLogging()) {
    PaperLogger.warn("Vivify RT format unsupported: context={} requested={} fallback={}",
                     context, requested.value__, fallback.value__);
  }
  return fallback;
}

void Runtime::LogMaterialShader(std::string_view context, std::string_view assetPath, UnityEngine::Material* material) const {
  if (!GetVivifyDebugLogging()) return;
  if (!IsAlive(material)) {
    PaperLogger.warn("Vivify material missing: context={} asset={}", context, assetPath);
    return;
  }
  auto* shader = material->get_shader().unsafePtr();
  auto shaderName = ShaderNameForLog(shader);
  PaperLogger.info("Vivify material: context={} asset={} material='{}' shader='{}' supported={} internalError={}",
                   context,
                   assetPath,
                   ToStdString(material->get_name()),
                   shaderName,
                   BoolText(IsAlive(shader) && shader->get_isSupported()),
                   BoolText(IsInternalErrorShaderName(shaderName)));
}

UnityEngine::Shader* Runtime::FindUsableShader(std::string const& shaderName) const {
  if (shaderName.empty()) return nullptr;
  if (auto it = _supportedShadersByName.find(NormalizeAssetKey(shaderName));
      it != _supportedShadersByName.end() && IsAlive(it->second) && it->second->get_isSupported()) {
    return it->second;
  }
  auto* bundled = il2cpp_utils::try_cast<UnityEngine::Shader>(GetAssetObject(shaderName)).value_or(nullptr);
  if (IsAlive(bundled) && bundled->get_isSupported()) {
    return bundled;
  }
  auto found = UnityEngine::Shader::Find(StringW(shaderName));
  auto* foundShader = found.unsafePtr();
  if (IsAlive(foundShader) && foundShader->get_isSupported()) {
    return foundShader;
  }
  return nullptr;
}

UnityEngine::Shader* Runtime::FindFallbackShader() const {
  static constexpr std::string_view fallbackNames[] = {
      "Unlit/Texture"sv,
      "Unlit/Color"sv,
      "Sprites/Default"sv,
      "Standard"sv,
  };
  for (auto name : fallbackNames) {
    auto shader = UnityEngine::Shader::Find(StringW(std::string(name)));
    auto* rawShader = shader.unsafePtr();
    if (IsAlive(rawShader) && rawShader->get_isSupported()) {
      return rawShader;
    }
  }
  return nullptr;
}

void Runtime::RepairMaterialShader(UnityEngine::Material* material, std::string_view context) {
  if (!IsAlive(material)) return;
  if (_repairedMaterials.contains(material)) return;
  auto shader = material->get_shader();
  auto* rawShader = shader.unsafePtr();
  auto originalShaderName = ShaderNameForLog(rawShader);
  int const originalPassCount = IsAlive(rawShader) ? material->get_passCount() : 0;
  if (GetVivifyDebugLogging() &&
      (!IsAlive(rawShader) || IsInternalErrorShaderName(originalShaderName) ||
       (IsAlive(rawShader) && !rawShader->get_isSupported()))) {
    PaperLogger.warn("Vivify shader diagnostic: context={} material='{}' shader='{}' supported={} internalError={} passes={}",
                     context,
                     ToStdString(material->get_name()),
                     originalShaderName,
                     BoolText(IsAlive(rawShader) && rawShader->get_isSupported()),
                     BoolText(IsInternalErrorShaderName(originalShaderName)),
                     originalPassCount);
  }
  if (IsAlive(rawShader) && !IsInternalErrorShaderName(originalShaderName) &&
      (rawShader->get_isSupported() || originalPassCount > 0)) {
    ApplyStereoKeywords(material);
    _repairedMaterials.emplace(material);
    return;
  }
  auto fallbackState = CaptureMaterialFallbackState(material);
  UnityEngine::Shader* replacement = nullptr;
  if (IsAlive(rawShader)) {
    auto shaderName = rawShader->get_name();
    if (shaderName) {
      replacement = FindUsableShader(ToStdString(shaderName));
    }
  }
  if (!IsAlive(replacement)) {
    replacement = FindFallbackShader();
  }
  if (IsAlive(replacement)) {
    material->set_shader(replacement);
    RestoreMaterialFallbackState(material, fallbackState);
    ApplyStereoKeywords(material);
    if (GetVivifyDebugLogging()) {
      PaperLogger.info("Vivify shader repaired: context={} material='{}' from='{}' to='{}' preservedColor={} preservedTexture={}",
                       context, ToStdString(material->get_name()), originalShaderName, ShaderNameForLog(replacement),
                       BoolText(fallbackState.color.has_value()), BoolText(IsManagedAlive(fallbackState.mainTexture)));
    }
  } else if (GetVivifyDebugLogging()) {
    PaperLogger.warn("Vivify shader repair failed: context={} material='{}' original='{}'",
                     context, ToStdString(material->get_name()), originalShaderName);
  }
  _repairedMaterials.emplace(material);
}

void Runtime::RepairGameObjectMaterials(UnityEngine::GameObject* gameObject, std::string_view context) {
  if (!IsAlive(gameObject)) return;
  auto renderers = gameObject->GetComponentsInChildren<UnityEngine::Renderer*>(true);
  for (int i = 0; i < renderers.size(); i++) {
    auto* renderer = renderers[i];
    if (!IsAlive(renderer)) continue;
    auto materials = renderer->get_sharedMaterials();
    if (!materials) continue;
    for (int j = 0; j < materials.size(); j++) {
      RepairMaterialShader(materials[j].unsafePtr(), context);
    }
  }
}

void Runtime::SetMaterialKeyword(UnityEngine::Material* material, ::StringW keyword, bool enabled) const {
  if (!IsAlive(material)) return;
  if (enabled) {
    material->EnableKeyword(keyword);
  } else {
    material->DisableKeyword(keyword);
  }
}

void Runtime::ApplyStereoKeywords(UnityEngine::Material* material) const {
  if (!IsAlive(material)) return;

  SetMaterialKeyword(material, u"MULTIPASS_ENABLED", GetMultipassRenderingEnabled());
}

void Runtime::ApplyGameObjectStereoKeywords(UnityEngine::GameObject* gameObject) {
  if (!IsAlive(gameObject)) return;
  auto renderers = gameObject->GetComponentsInChildren<UnityEngine::Renderer*>(true);
  for (int i = 0; i < renderers.size(); i++) {
    auto* renderer = renderers[i];
    if (!IsAlive(renderer)) continue;
    auto materials = renderer->get_sharedMaterials();
    if (!materials) continue;
    for (int j = 0; j < materials.size(); j++) {
      ApplyStereoKeywords(materials[j].unsafePtr());
    }
  }
}

void Runtime::RefreshLoadedMaterialStereoKeywords() {
  for (auto const& [_, asset] : _assets) {
    if (!IsAlive(asset)) continue;
    if (auto* material = il2cpp_utils::try_cast<UnityEngine::Material>(asset).value_or(nullptr); IsAlive(material)) {
      ApplyStereoKeywords(material);
    } else if (auto* gameObject = il2cpp_utils::try_cast<UnityEngine::GameObject>(asset).value_or(nullptr); IsAlive(gameObject)) {
      ApplyGameObjectStereoKeywords(gameObject);
    }
  }
}

void Runtime::RepairLoadedMaterialShaders() {
  for (auto const& [path, asset] : _assets) {
    if (!IsAlive(asset)) continue;
    if (auto* material = il2cpp_utils::try_cast<UnityEngine::Material>(asset).value_or(nullptr); IsAlive(material)) {
      RepairMaterialShader(material, path);
    } else if (auto* gameObject = il2cpp_utils::try_cast<UnityEngine::GameObject>(asset).value_or(nullptr); IsAlive(gameObject)) {
      RepairGameObjectMaterials(gameObject, path);
    }
  }
}

}
