#include "VivifyRuntimeInternal.hpp"
#include "VivifyComponents.hpp"

namespace Vivify {

std::optional<MaterialPropertyChange> Runtime::ParseMaterialProperty(rapidjson::Value const& propertyJson) {
  auto id = ReadStringView(propertyJson, "id");
  auto type = ReadStringView(propertyJson, "type");
  if (!id.has_value() || !type.has_value()) {
    return std::nullopt;
  }
  MaterialPropertyChange property;
  property.kind = ParseMaterialPropertyKind(*type);
  if (property.kind == MaterialPropertyKind::Keyword) {
    property.id = std::string(*id);
  } else {
    property.id = UnityEngine::Shader::PropertyToID(StringW(*id));
  }
  switch (property.kind) {
    case MaterialPropertyKind::Texture: {
      auto texture = ReadStringView(propertyJson, "value");
      if (!texture.has_value()) {
        return std::nullopt;
      }
      property.value = std::string(*texture);
      return property;
    }
    case MaterialPropertyKind::Color:
    case MaterialPropertyKind::Vector: {
      auto point = MakePointDefinition(propertyJson, "value", Tracks::ffi::WrapBaseValueType::Vec4);
      if (!point.has_value()) {
        return std::nullopt;
      }
      property.value = *point;
      return property;
    }
    case MaterialPropertyKind::Float: {
      if (auto point = MakePointDefinition(propertyJson, "value", Tracks::ffi::WrapBaseValueType::Float); point.has_value()) {
        property.value = *point;
      } else if (auto value = ReadFloat(propertyJson, "value"); value.has_value()) {
        property.value = *value;
      } else {
        return std::nullopt;
      }
      return property;
    }
    case MaterialPropertyKind::Int: {
      if (auto value = ReadInt(propertyJson, "value"); value.has_value()) {
        property.value = *value;
        return property;
      }
      return std::nullopt;
    }
    case MaterialPropertyKind::Keyword: {
      if (auto point = MakePointDefinition(propertyJson, "value", Tracks::ffi::WrapBaseValueType::Float); point.has_value()) {
        property.value = *point;
      } else if (auto value = ReadBool(propertyJson, "value"); value.has_value()) {
        property.value = *value;
      } else {
        return std::nullopt;
      }
      return property;
    }
    case MaterialPropertyKind::Unsupported:
    default:
      return std::nullopt;
  }
}

std::vector<MaterialPropertyChange> Runtime::ParseMaterialProperties(rapidjson::Value const& json) {
  std::vector<MaterialPropertyChange> properties;
  auto* propertiesValue = ReadValuePtr(json, "properties");
  if (propertiesValue == nullptr || !propertiesValue->IsArray()) {
    return properties;
  }
  properties.reserve(propertiesValue->Size());
  for (auto const& propertyJson : propertiesValue->GetArray()) {
    if (!propertyJson.IsObject()) {
      continue;
    }
    if (auto property = ParseMaterialProperty(propertyJson); property.has_value()) {
      properties.emplace_back(std::move(*property));
    }
  }
  return properties;
}

std::optional<AnimatorPropertyChange> Runtime::ParseAnimatorProperty(rapidjson::Value const& propertyJson) {
  auto name = ReadStringView(propertyJson, "id");
  auto type = ReadStringView(propertyJson, "type");
  if (!name.has_value() || !type.has_value()) {
    return std::nullopt;
  }
  AnimatorPropertyChange property;
  property.name = std::string(*name);
  property.id = UnityEngine::Animator::StringToHash(StringW(*name));
  property.kind = ParseAnimatorPropertyKind(*type);
  switch (property.kind) {
    case AnimatorPropertyKind::Bool: {
      if (auto point = MakePointDefinition(propertyJson, "value", Tracks::ffi::WrapBaseValueType::Float); point.has_value()) {
        property.value = *point;
      } else if (auto value = ReadBool(propertyJson, "value"); value.has_value()) {
        property.value = *value;
      } else {
        return std::nullopt;
      }
      return property;
    }
    case AnimatorPropertyKind::Float: {
      if (auto point = MakePointDefinition(propertyJson, "value", Tracks::ffi::WrapBaseValueType::Float); point.has_value()) {
        property.value = *point;
      } else if (auto value = ReadFloat(propertyJson, "value"); value.has_value()) {
        property.value = *value;
      } else {
        return std::nullopt;
      }
      return property;
    }
    case AnimatorPropertyKind::Integer: {
      if (auto point = MakePointDefinition(propertyJson, "value", Tracks::ffi::WrapBaseValueType::Float); point.has_value()) {
        property.value = *point;
      } else if (auto value = ReadInt(propertyJson, "value"); value.has_value()) {
        property.value = *value;
      } else {
        return std::nullopt;
      }
      return property;
    }
    case AnimatorPropertyKind::Trigger: {
      property.value = ReadBool(propertyJson, "value").value_or(true);
      return property;
    }
    case AnimatorPropertyKind::Unsupported:
    default:
      return std::nullopt;
  }
}

std::vector<AnimatorPropertyChange> Runtime::ParseAnimatorProperties(rapidjson::Value const& json) {
  std::vector<AnimatorPropertyChange> properties;
  auto* propertiesValue = ReadValuePtr(json, "properties");
  if (propertiesValue == nullptr || !propertiesValue->IsArray()) {
    return properties;
  }
  properties.reserve(propertiesValue->Size());
  for (auto const& propertyJson : propertiesValue->GetArray()) {
    if (!propertyJson.IsObject()) {
      continue;
    }
    if (auto property = ParseAnimatorProperty(propertyJson); property.has_value()) {
      properties.emplace_back(std::move(*property));
    }
  }
  return properties;
}

bool Runtime::IsAnimated(MaterialPropertyChange const& property) const {
  return std::holds_alternative<PointDefinitionW>(property.value);
}

bool Runtime::IsAnimated(AnimatorPropertyChange const& property) const {
  return std::holds_alternative<PointDefinitionW>(property.value);
}

void Runtime::ApplyMaterialProperty(UnityEngine::Material* material, MaterialPropertyChange const& property, float progress) {
  if (material == nullptr) {
    return;
  }
  switch (property.kind) {
    case MaterialPropertyKind::Texture: {
      auto propertyId = std::get<int>(property.id);
      auto* texture = GetAssetAs<UnityEngine::Texture>(std::get<std::string>(property.value));
      if (texture != nullptr) {
        material->SetTexture(propertyId, texture);
      }
      break;
    }
    case MaterialPropertyKind::Color: {
      auto propertyId = std::get<int>(property.id);
      auto const& point = std::get<PointDefinitionW>(property.value);
      material->SetColor(propertyId, VectorToColor(point.InterpolateVector4(progress)));
      break;
    }
    case MaterialPropertyKind::Float: {
      auto propertyId = std::get<int>(property.id);
      if (std::holds_alternative<PointDefinitionW>(property.value)) {
        material->SetFloat(propertyId, std::get<PointDefinitionW>(property.value).InterpolateLinear(progress));
      } else {
        material->SetFloat(propertyId, std::get<float>(property.value));
      }
      break;
    }
    case MaterialPropertyKind::Int: {
      auto propertyId = std::get<int>(property.id);
      material->SetInt(propertyId, std::get<int>(property.value));
      break;
    }
    case MaterialPropertyKind::Vector: {
      auto propertyId = std::get<int>(property.id);
      auto const& point = std::get<PointDefinitionW>(property.value);
      material->SetVector(propertyId, ToUnityVector(point.InterpolateVector4(progress)));
      break;
    }
    case MaterialPropertyKind::Keyword: {
      auto const& keyword = std::get<std::string>(property.id);
      bool enabled = false;
      if (std::holds_alternative<PointDefinitionW>(property.value)) {
        enabled = std::get<PointDefinitionW>(property.value).InterpolateLinear(progress) >= 1.0f;
      } else {
        enabled = std::get<bool>(property.value);
      }
      SetMaterialKeyword(material, StringW(keyword), enabled);
      ApplyStereoKeywords(material);
      break;
    }
    case MaterialPropertyKind::Unsupported:
    default:
      break;
  }
}

void Runtime::SaveOriginalGlobalProperty(MaterialPropertyChange const& property) {
  switch (property.kind) {
    case MaterialPropertyKind::Texture: {
      int propertyId = std::get<int>(property.id);
      if (!_savedGlobalProperties.contains(propertyId)) {
        _savedGlobalProperties.emplace(propertyId, UnityEngine::Shader::GetGlobalTexture(propertyId).ptr());
      }
      break;
    }
    case MaterialPropertyKind::Color: {
      int propertyId = std::get<int>(property.id);
      if (!_savedGlobalProperties.contains(propertyId)) {
        _savedGlobalProperties.emplace(propertyId, UnityEngine::Shader::GetGlobalColor(propertyId));
      }
      break;
    }
    case MaterialPropertyKind::Float: {
      int propertyId = std::get<int>(property.id);
      if (!_savedGlobalProperties.contains(propertyId)) {
        _savedGlobalProperties.emplace(propertyId, UnityEngine::Shader::GetGlobalFloat(propertyId));
      }
      break;
    }
    case MaterialPropertyKind::Vector: {
      int propertyId = std::get<int>(property.id);
      if (!_savedGlobalProperties.contains(propertyId)) {
        _savedGlobalProperties.emplace(propertyId, UnityEngine::Shader::GetGlobalVector(propertyId));
      }
      break;
    }
    case MaterialPropertyKind::Keyword: {
      auto const& keyword = std::get<std::string>(property.id);
      if (!_savedGlobalKeywords.contains(keyword)) {
        _savedGlobalKeywords.emplace(keyword, UnityEngine::Shader::IsKeywordEnabled(StringW(keyword)));
      }
      break;
    }
    case MaterialPropertyKind::Int:
    case MaterialPropertyKind::Unsupported:
    default:
      break;
  }
}

void Runtime::ApplyGlobalProperty(MaterialPropertyChange const& property, float progress) {
  SaveOriginalGlobalProperty(property);
  switch (property.kind) {
    case MaterialPropertyKind::Texture: {
      int propertyId = std::get<int>(property.id);
      auto* texture = GetAssetAs<UnityEngine::Texture>(std::get<std::string>(property.value));
      if (texture != nullptr) {
        UnityEngine::Shader::SetGlobalTexture(propertyId, texture);
        _currentGlobalProperties[propertyId] = texture;
      }
      break;
    }
    case MaterialPropertyKind::Color: {
      int propertyId = std::get<int>(property.id);
      auto const& point = std::get<PointDefinitionW>(property.value);
      auto color = VectorToColor(point.InterpolateVector4(progress));
      UnityEngine::Shader::SetGlobalColor(propertyId, color);
      _currentGlobalProperties[propertyId] = color;
      break;
    }
    case MaterialPropertyKind::Float: {
      int propertyId = std::get<int>(property.id);
      float value;
      if (std::holds_alternative<PointDefinitionW>(property.value)) {
        value = std::get<PointDefinitionW>(property.value).InterpolateLinear(progress);
      } else {
        value = std::get<float>(property.value);
      }
      UnityEngine::Shader::SetGlobalFloat(propertyId, value);
      _currentGlobalProperties[propertyId] = value;
      break;
    }
    case MaterialPropertyKind::Vector: {
      int propertyId = std::get<int>(property.id);
      auto const& point = std::get<PointDefinitionW>(property.value);
      auto vector = ToUnityVector(point.InterpolateVector4(progress));
      UnityEngine::Shader::SetGlobalVector(propertyId, vector);
      _currentGlobalProperties[propertyId] = vector;
      break;
    }
    case MaterialPropertyKind::Keyword: {
      auto const& keyword = std::get<std::string>(property.id);
      bool enabled = false;
      if (std::holds_alternative<PointDefinitionW>(property.value)) {
        enabled = std::get<PointDefinitionW>(property.value).InterpolateLinear(progress) >= 1.0f;
      } else {
        enabled = std::get<bool>(property.value);
      }
      if (enabled) {
        UnityEngine::Shader::EnableKeyword(StringW(keyword));
      } else {
        UnityEngine::Shader::DisableKeyword(StringW(keyword));
      }
      _currentGlobalKeywords[keyword] = enabled;
      auto currentCamera = UnityEngine::Camera::get_current();
      SetMultipassShaderStateForCamera(currentCamera.unsafePtr());
      break;
    }
    case MaterialPropertyKind::Int:
    case MaterialPropertyKind::Unsupported:
    default:
      break;
  }
}

void Runtime::ApplyAnimatorProperty(std::vector<UnityEngine::Animator*> const& animators,
                                    AnimatorPropertyChange const& property,
                                    float progress) {
  for (auto* animator : animators) {
    if (!IsManagedAlive(animator)) {
      continue;
    }
    switch (property.kind) {
      case AnimatorPropertyKind::Bool: {
        bool value = false;
        if (std::holds_alternative<PointDefinitionW>(property.value)) {
          value = std::get<PointDefinitionW>(property.value).InterpolateLinear(progress) >= 1.0f;
        } else {
          value = std::get<bool>(property.value);
        }
        animator->SetBool(property.id, value);
        break;
      }
      case AnimatorPropertyKind::Float: {
        float value = 0.0f;
        if (std::holds_alternative<PointDefinitionW>(property.value)) {
          value = std::get<PointDefinitionW>(property.value).InterpolateLinear(progress);
        } else {
          value = std::get<float>(property.value);
        }
        animator->SetFloat(property.id, value);
        break;
      }
      case AnimatorPropertyKind::Integer: {
        int value = 0;
        if (std::holds_alternative<PointDefinitionW>(property.value)) {
          value = static_cast<int>(std::get<PointDefinitionW>(property.value).InterpolateLinear(progress));
        } else {
          value = std::get<int>(property.value);
        }
        animator->SetInteger(property.id, value);
        break;
      }
      case AnimatorPropertyKind::Trigger: {
        bool enabled = std::get<bool>(property.value);
        if (enabled) {
          animator->SetTrigger(property.id);
        } else {
          animator->ResetTrigger(property.id);
        }
        break;
      }
      case AnimatorPropertyKind::Unsupported:
      default:
        break;
    }
  }
}

void Runtime::QueueMaterialPropertyAnimation(UnityEngine::Material* material,
                                             std::vector<MaterialPropertyChange> const& properties,
                                             float startTime, float duration, Functions easing) {
  std::vector<MaterialPropertyChange> animatedProperties;
  animatedProperties.reserve(properties.size());
  for (auto const& property : properties) {
    if (IsAnimated(property)) {
      animatedProperties.emplace_back(property);
    }
  }
  if (animatedProperties.empty()) return;
  _materialAnimations.emplace_back(ActiveMaterialAnimation{
      .material = material,
      .properties = std::move(animatedProperties),
      .startTime = startTime,
      .duration = duration,
      .endTime = startTime + duration,
      .easing = easing,
  });
}

void Runtime::HandleSetMaterialProperty(CustomJSONData::CustomEventData* customEventData, rapidjson::Value const& json) {
  auto asset = ReadStringView(json, "asset");
  if (!asset.has_value()) {
    return;
  }
  auto* material = GetAssetAs<UnityEngine::Material>(*asset);
  if (material == nullptr) {
    return;
  }
  auto properties = ParseMaterialProperties(json);
  if (properties.empty()) {
    return;
  }
  float duration = DurationBeatsToSeconds(ReadFloat(json, "duration").value_or(0.0f));
  Functions easing = ParseEasing(ReadStringView(json, "easing").value_or("easeLinear"sv));
  float startTime = customEventData->time;
  float currentSongTime = CurrentSongTime();
  bool completed = duration <= 0.0f || startTime + duration <= currentSongTime;
  float initialProgress = completed ? 1.0f : 0.0f;
  for (auto const& property : properties) {
    ApplyMaterialProperty(material, property, initialProgress);
  }
  if (!completed) {
    QueueMaterialPropertyAnimation(material, properties, startTime, duration, easing);
  }
}

void Runtime::HandleSetAnimatorProperty(CustomJSONData::CustomEventData* customEventData, rapidjson::Value const& json) {
  auto id = ReadStringView(json, "id");
  if (!id.has_value()) {
    return;
  }
  std::string prefabId(*id);
  auto prefabIt = _livePrefabs.find(prefabId);
  if (prefabIt == _livePrefabs.end()) {
    return;
  }
  auto properties = ParseAnimatorProperties(json);
  if (properties.empty()) {
    return;
  }
  float duration = DurationBeatsToSeconds(ReadFloat(json, "duration").value_or(0.0f));
  Functions easing = ParseEasing(ReadStringView(json, "easing").value_or("easeLinear"sv));
  float startTime = customEventData->time;
  std::vector<AnimatorPropertyChange> animatedProperties;
  animatedProperties.reserve(properties.size());
  float currentSongTime = CurrentSongTime();
  bool completed = duration <= 0.0f || startTime + duration <= currentSongTime;
  float initialProgress = completed ? 1.0f : 0.0f;
  for (auto const& property : properties) {
    ApplyAnimatorProperty(prefabIt->second.animators, property, initialProgress);
    if (!completed && IsAnimated(property)) {
      animatedProperties.emplace_back(property);
    }
  }
  if (!animatedProperties.empty()) {
    _animatorAnimations.emplace_back(ActiveAnimatorAnimation{
        .prefabId = std::move(prefabId),
        .properties = std::move(animatedProperties),
        .startTime = startTime,
        .duration = duration,
        .endTime = startTime + duration,
        .easing = easing,
    });
  }
}

void Runtime::HandleSetGlobalProperty(CustomJSONData::CustomEventData* customEventData, rapidjson::Value const& json) {
  auto properties = ParseMaterialProperties(json);
  if (properties.empty()) {
    return;
  }
  float duration = DurationBeatsToSeconds(ReadFloat(json, "duration").value_or(0.0f));
  Functions easing = ParseEasing(ReadStringView(json, "easing").value_or("easeLinear"sv));
  float startTime = customEventData->time;
  std::vector<MaterialPropertyChange> animatedProperties;
  animatedProperties.reserve(properties.size());
  float currentSongTime = CurrentSongTime();
  bool completed = duration <= 0.0f || startTime + duration <= currentSongTime;
  float initialProgress = completed ? 1.0f : 0.0f;
  for (auto const& property : properties) {
    ApplyGlobalProperty(property, initialProgress);
    if (!completed && IsAnimated(property)) {
      animatedProperties.emplace_back(property);
    }
  }
  if (!animatedProperties.empty()) {
    _globalAnimations.emplace_back(ActiveGlobalAnimation{
        .properties = std::move(animatedProperties),
        .startTime = startTime,
        .duration = duration,
        .endTime = startTime + duration,
        .easing = easing,
    });
  }
}

float Runtime::AnimationProgress(float startTime, float duration, Functions easing, float songTime) const {
  if (duration <= 0.0f) {
    return 1.0f;
  }
  float raw = std::clamp((songTime - startTime) / duration, 0.0f, 1.0f);
  return Easings::Interpolate(raw, easing);
}

void Runtime::UpdateMaterialAnimations() {
  if (_materialAnimations.empty()) return;
  float const songTime = CurrentSongTime();
  auto write = _materialAnimations.begin();
  for (auto read = _materialAnimations.begin(); read != _materialAnimations.end(); ++read) {
    if (!IsManagedAlive(read->material)) continue;
    float const progress = AnimationProgress(read->startTime, read->duration, read->easing, songTime);
    for (auto const& property : read->properties) ApplyMaterialProperty(read->material, property, progress);
    if (songTime < read->endTime) {
      if (write != read) *write = std::move(*read);
      ++write;
    }
  }
  _materialAnimations.erase(write, _materialAnimations.end());
}

void Runtime::UpdateGlobalAnimations() {
  if (_globalAnimations.empty()) return;
  float const songTime = CurrentSongTime();
  auto write = _globalAnimations.begin();
  for (auto read = _globalAnimations.begin(); read != _globalAnimations.end(); ++read) {
    float const progress = AnimationProgress(read->startTime, read->duration, read->easing, songTime);
    for (auto const& property : read->properties) ApplyGlobalProperty(property, progress);
    if (songTime < read->endTime) {
      if (write != read) *write = std::move(*read);
      ++write;
    }
  }
  _globalAnimations.erase(write, _globalAnimations.end());
}

void Runtime::UpdateAnimatorAnimations() {
  if (_animatorAnimations.empty()) return;
  float const songTime = CurrentSongTime();
  auto write = _animatorAnimations.begin();
  for (auto read = _animatorAnimations.begin(); read != _animatorAnimations.end(); ++read) {
    auto prefabIt = _livePrefabs.find(read->prefabId);
    if (prefabIt == _livePrefabs.end()) continue;
    float const progress = AnimationProgress(read->startTime, read->duration, read->easing, songTime);
    for (auto const& property : read->properties) ApplyAnimatorProperty(prefabIt->second.animators, property, progress);
    if (songTime < read->endTime) {
      if (write != read) *write = std::move(*read);
      ++write;
    }
  }
  _animatorAnimations.erase(write, _animatorAnimations.end());
}

void Runtime::RestoreGlobalProperties() {
  for (auto const& [propertyId, value] : _savedGlobalProperties) {
    if (std::holds_alternative<UnityEngine::Texture*>(value)) {
      UnityEngine::Shader::SetGlobalTexture(propertyId, std::get<UnityEngine::Texture*>(value));
    } else if (std::holds_alternative<UnityEngine::Color>(value)) {
      UnityEngine::Shader::SetGlobalColor(propertyId, std::get<UnityEngine::Color>(value));
    } else if (std::holds_alternative<float>(value)) {
      UnityEngine::Shader::SetGlobalFloat(propertyId, std::get<float>(value));
    } else if (std::holds_alternative<UnityEngine::Vector4>(value)) {
      UnityEngine::Shader::SetGlobalVector(propertyId, std::get<UnityEngine::Vector4>(value));
    }
  }
  for (auto const& [keyword, enabled] : _savedGlobalKeywords) {
    if (enabled) {
      UnityEngine::Shader::EnableKeyword(StringW(keyword));
    } else {
      UnityEngine::Shader::DisableKeyword(StringW(keyword));
    }
  }
}

void Runtime::ReapplyCurrentGlobalProperties() {
  for (auto const& [propertyId, value] : _currentGlobalProperties) {
    if (std::holds_alternative<UnityEngine::Texture*>(value)) {
      auto* texture = std::get<UnityEngine::Texture*>(value);
      if (IsManagedAlive(texture)) {
        UnityEngine::Shader::SetGlobalTexture(propertyId, texture);
      }
    } else if (std::holds_alternative<UnityEngine::Color>(value)) {
      UnityEngine::Shader::SetGlobalColor(propertyId, std::get<UnityEngine::Color>(value));
    } else if (std::holds_alternative<float>(value)) {
      UnityEngine::Shader::SetGlobalFloat(propertyId, std::get<float>(value));
    } else if (std::holds_alternative<UnityEngine::Vector4>(value)) {
      UnityEngine::Shader::SetGlobalVector(propertyId, std::get<UnityEngine::Vector4>(value));
    }
  }
  for (auto const& [keyword, enabled] : _currentGlobalKeywords) {
    if (enabled) {
      UnityEngine::Shader::EnableKeyword(StringW(keyword));
    } else {
      UnityEngine::Shader::DisableKeyword(StringW(keyword));
    }
  }
}

namespace {
enum class SettingType { Float, Color, Int, Bool, MaterialAsset, LightPrefab };

std::unordered_map<std::string_view, SettingType> const kRenderSettingTypes = {
    {"ambientEquatorColor"sv, SettingType::Color},
    {"ambientGroundColor"sv, SettingType::Color},
    {"ambientIntensity"sv, SettingType::Float},
    {"ambientLight"sv, SettingType::Color},
    {"ambientMode"sv, SettingType::Int},
    {"ambientSkyColor"sv, SettingType::Color},
    {"defaultReflectionMode"sv, SettingType::Int},
    {"defaultReflectionResolution"sv, SettingType::Int},
    {"flareFadeSpeed"sv, SettingType::Float},
    {"flareStrength"sv, SettingType::Float},
    {"fog"sv, SettingType::Bool},
    {"fogColor"sv, SettingType::Color},
    {"fogDensity"sv, SettingType::Float},
    {"fogEndDistance"sv, SettingType::Float},
    {"fogMode"sv, SettingType::Int},
    {"fogStartDistance"sv, SettingType::Float},
    {"haloStrength"sv, SettingType::Float},
    {"reflectionBounces"sv, SettingType::Int},
    {"reflectionIntensity"sv, SettingType::Float},
    {"subtractiveShadowColor"sv, SettingType::Color},
    {"skybox"sv, SettingType::MaterialAsset},
    {"sun"sv, SettingType::LightPrefab},
    {"antiAliasing"sv, SettingType::Int},
    {"realtimeReflectionProbes"sv, SettingType::Bool},
};

bool IsLiteralColorArray(rapidjson::Value const& value) {
  if (!value.IsArray() || value.Size() < 3 || value.Size() > 4) return false;
  for (auto const& entry : value.GetArray()) {
    if (!entry.IsNumber()) return false;
  }
  return true;
}
}

void Runtime::HandleSetRenderingSettings(CustomJSONData::CustomEventData* customEventData,
                                         rapidjson::Value const& json) {
  float duration = DurationBeatsToSeconds(ReadFloat(json, "duration").value_or(0.0f));
  Functions easing = ParseEasing(ReadStringView(json, "easing").value_or("easeLinear"));
  float startTime = customEventData->time;
  float songTime = CurrentSongTime();
  bool noDuration = duration <= 0.0f || startTime + duration <= songTime;
  std::vector<RenderSettingValue> settings;

  for (auto categoryKey : {"renderSettings"sv, "qualitySettings"sv, "xrSettings"sv}) {
    auto* categoryVal = ReadValuePtr(json, categoryKey);
    if (categoryVal == nullptr || !categoryVal->IsObject()) continue;
    for (auto it = categoryVal->MemberBegin(); it != categoryVal->MemberEnd(); ++it) {
      std::string key = it->name.GetString();
      ParseAndApplyRenderSetting(key, *categoryVal, it->value, settings, noDuration);
    }
  }
  if (!noDuration && !settings.empty()) {
    _renderSettingAnimations.emplace_back(ActiveRenderSettingAnimation{
        std::move(settings), startTime, duration, startTime + duration, easing});
  }

  if (GetVivifyDebugLogging()) {
    PaperLogger.info("Vivify SetRenderingSettings: time={} durationSeconds={} immediate={}",
                     customEventData->time, duration, BoolText(noDuration));
  }
}

void Runtime::ParseAndApplyRenderSetting(std::string const& key, rapidjson::Value const& parent,
                                         rapidjson::Value const& val,
                                         std::vector<RenderSettingValue>& out, bool immediate) {
  auto typeIt = kRenderSettingTypes.find(key);
  if (typeIt == kRenderSettingTypes.end()) {
    if (GetVivifyDebugLogging()) {
      PaperLogger.warn("Vivify SetRenderingSettings: unsupported setting '{}'", key);
    }
    return;
  }
  float const initialProgress = immediate ? 1.0f : 0.0f;
  auto queue = [&](RenderSettingKind kind, PointDefinitionW point) {
    if (immediate) return;
    RenderSettingValue rsv;
    rsv.name = key;
    rsv.kind = kind;
    rsv.value = point;
    out.push_back(std::move(rsv));
  };
  switch (typeIt->second) {
    case SettingType::Color: {
      if (IsLiteralColorArray(val)) {
        if (auto color = ReadColorArray(val); color.has_value()) {
          ApplyRenderSettingColor(key, *color);
        }
        return;
      }
      if (auto point = MakePointDefinition(parent, key, Tracks::ffi::WrapBaseValueType::Vec4); point.has_value()) {
        ApplyRenderSettingColor(key, VectorToColor(point->InterpolateVector4(initialProgress)));
        queue(RenderSettingKind::Color, *point);
      }
      return;
    }
    case SettingType::Float: {
      if (val.IsNumber()) {
        ApplyRenderSettingFloat(key, val.GetFloat());
        return;
      }
      if (auto point = MakePointDefinition(parent, key, Tracks::ffi::WrapBaseValueType::Float); point.has_value()) {
        ApplyRenderSettingFloat(key, point->InterpolateLinear(initialProgress));
        queue(RenderSettingKind::Float, *point);
      }
      return;
    }
    case SettingType::Int: {
      if (val.IsNumber()) {
        ApplyRenderSettingInt(key, static_cast<int>(val.GetFloat()));
        return;
      }
      if (auto point = MakePointDefinition(parent, key, Tracks::ffi::WrapBaseValueType::Float); point.has_value()) {
        ApplyRenderSettingInt(key, static_cast<int>(point->InterpolateLinear(initialProgress)));
        queue(RenderSettingKind::Int, *point);
      }
      return;
    }
    case SettingType::Bool: {
      if (val.IsBool() || val.IsNumber()) {
        bool b = val.IsBool() ? val.GetBool() : val.GetFloat() != 0.0f;
        ApplyRenderSettingBool(key, b);
        return;
      }
      if (auto point = MakePointDefinition(parent, key, Tracks::ffi::WrapBaseValueType::Float); point.has_value()) {
        ApplyRenderSettingBool(key, point->InterpolateLinear(initialProgress) >= 1.0f);
        queue(RenderSettingKind::Bool, *point);
      }
      return;
    }
    case SettingType::MaterialAsset:
    case SettingType::LightPrefab: {
      if (val.IsString()) {
        ApplyRenderSettingObject(key, val.GetString());
      }
      return;
    }
  }
}

namespace {

using QualityBoolSetter = void (*)(bool);
using QualityBoolGetter = bool (*)();

QualityBoolSetter RealtimeReflectionProbesSetter() {
  static auto* setter = reinterpret_cast<QualityBoolSetter>(
      il2cpp_functions::resolve_icall("UnityEngine.QualitySettings::set_realtimeReflectionProbes"));
  return setter;
}

QualityBoolGetter RealtimeReflectionProbesGetter() {
  static auto* getter = reinterpret_cast<QualityBoolGetter>(
      il2cpp_functions::resolve_icall("UnityEngine.QualitySettings::get_realtimeReflectionProbes"));
  return getter;
}

void LogRealtimeReflectionProbesIcallOnce() {
  static bool logged = false;
  if (logged) return;
  logged = true;
  auto* setter = RealtimeReflectionProbesSetter();
  auto* getter = RealtimeReflectionProbesGetter();
  PaperLogger.info("Vivify realtimeReflectionProbes icall: setter={} getter={} deviceDefault={}",
                   setter != nullptr, getter != nullptr,
                   getter != nullptr ? (getter() ? "on" : "off") : "?");
}

void RawApplyRenderSettingFloat(std::string const& name, float val) {
  if (name == "fogDensity") UnityEngine::RenderSettings::set_fogDensity(val);
  else if (name == "fogStartDistance") UnityEngine::RenderSettings::set_fogStartDistance(val);
  else if (name == "fogEndDistance") UnityEngine::RenderSettings::set_fogEndDistance(val);
  else if (name == "ambientIntensity") UnityEngine::RenderSettings::set_ambientIntensity(val);
  else if (name == "reflectionIntensity") UnityEngine::RenderSettings::set_reflectionIntensity(val);
  else if (name == "haloStrength") UnityEngine::RenderSettings::set_haloStrength(val);
  else if (name == "flareStrength") UnityEngine::RenderSettings::set_flareStrength(val);
  else if (name == "flareFadeSpeed") UnityEngine::RenderSettings::set_flareFadeSpeed(val);
}
void RawApplyRenderSettingColor(std::string const& name, UnityEngine::Color val) {
  if (name == "fogColor") UnityEngine::RenderSettings::set_fogColor(val);
  else if (name == "ambientLight") UnityEngine::RenderSettings::set_ambientLight(val);
  else if (name == "ambientSkyColor") UnityEngine::RenderSettings::set_ambientSkyColor(val);
  else if (name == "ambientEquatorColor") UnityEngine::RenderSettings::set_ambientEquatorColor(val);
  else if (name == "ambientGroundColor") UnityEngine::RenderSettings::set_ambientGroundColor(val);
  else if (name == "subtractiveShadowColor") UnityEngine::RenderSettings::set_subtractiveShadowColor(val);
}
void RawApplyRenderSettingBool(std::string const& name, bool val) {
  if (name == "fog") UnityEngine::RenderSettings::set_fog(val);
  else if (name == "realtimeReflectionProbes") {
    LogRealtimeReflectionProbesIcallOnce();
    if (auto* setter = RealtimeReflectionProbesSetter()) setter(val);
  }
}
void RawApplyRenderSettingInt(std::string const& name, int val) {
  if (name == "antiAliasing") UnityEngine::QualitySettings::set_antiAliasing(val);
  else if (name == "fogMode") UnityEngine::RenderSettings::set_fogMode(static_cast<UnityEngine::FogMode>(val));
  else if (name == "ambientMode") UnityEngine::RenderSettings::set_ambientMode(static_cast<UnityEngine::Rendering::AmbientMode>(val));
  else if (name == "defaultReflectionMode") UnityEngine::RenderSettings::set_defaultReflectionMode(static_cast<UnityEngine::Rendering::DefaultReflectionMode>(val));
  else if (name == "defaultReflectionResolution") UnityEngine::RenderSettings::set_defaultReflectionResolution(val);
  else if (name == "reflectionBounces") UnityEngine::RenderSettings::set_reflectionBounces(val);
}
void RawApplySavedRenderSetting(SavedRenderSetting const& s) {
  switch (s.kind) {
    case RenderSettingKind::Float:
      if (std::holds_alternative<float>(s.saved)) RawApplyRenderSettingFloat(s.name, std::get<float>(s.saved));
      break;
    case RenderSettingKind::Color:
      if (std::holds_alternative<UnityEngine::Color>(s.saved)) RawApplyRenderSettingColor(s.name, std::get<UnityEngine::Color>(s.saved));
      break;
    case RenderSettingKind::Bool:
      if (std::holds_alternative<bool>(s.saved)) RawApplyRenderSettingBool(s.name, std::get<bool>(s.saved));
      break;
    case RenderSettingKind::Int:
      if (std::holds_alternative<int>(s.saved)) RawApplyRenderSettingInt(s.name, std::get<int>(s.saved));
      break;
    case RenderSettingKind::Material:
      if (std::holds_alternative<UnityEngine::Material*>(s.saved)) {
        UnityEngine::RenderSettings::set_skybox(std::get<UnityEngine::Material*>(s.saved));
      }
      break;
    case RenderSettingKind::Light:
      if (std::holds_alternative<UnityEngine::Light*>(s.saved)) {
        UnityEngine::RenderSettings::set_sun(std::get<UnityEngine::Light*>(s.saved));
      }
      break;
  }
}
}

void Runtime::SaveRenderSetting(std::string const& name, RenderSettingKind kind) {
  for (auto const& s : _savedRenderSettings) {
    if (s.name == name) return;
  }
  SavedRenderSetting saved;
  saved.name = name;
  saved.kind = kind;
  if (name == "fog") saved.saved = UnityEngine::RenderSettings::get_fog();
  else if (name == "fogColor") saved.saved = UnityEngine::RenderSettings::get_fogColor();
  else if (name == "fogDensity") saved.saved = UnityEngine::RenderSettings::get_fogDensity();
  else if (name == "fogStartDistance") saved.saved = UnityEngine::RenderSettings::get_fogStartDistance();
  else if (name == "fogEndDistance") saved.saved = UnityEngine::RenderSettings::get_fogEndDistance();
  else if (name == "fogMode") saved.saved = static_cast<int>(UnityEngine::RenderSettings::get_fogMode());
  else if (name == "ambientIntensity") saved.saved = UnityEngine::RenderSettings::get_ambientIntensity();
  else if (name == "ambientLight") saved.saved = UnityEngine::RenderSettings::get_ambientLight();
  else if (name == "ambientMode") saved.saved = static_cast<int>(UnityEngine::RenderSettings::get_ambientMode());
  else if (name == "reflectionIntensity") saved.saved = UnityEngine::RenderSettings::get_reflectionIntensity();
  else if (name == "reflectionBounces") saved.saved = UnityEngine::RenderSettings::get_reflectionBounces();
  else if (name == "defaultReflectionMode") saved.saved = static_cast<int>(UnityEngine::RenderSettings::get_defaultReflectionMode());
  else if (name == "defaultReflectionResolution") saved.saved = UnityEngine::RenderSettings::get_defaultReflectionResolution();
  else if (name == "haloStrength") saved.saved = UnityEngine::RenderSettings::get_haloStrength();
  else if (name == "flareStrength") saved.saved = UnityEngine::RenderSettings::get_flareStrength();
  else if (name == "flareFadeSpeed") saved.saved = UnityEngine::RenderSettings::get_flareFadeSpeed();
  else if (name == "ambientSkyColor") saved.saved = UnityEngine::RenderSettings::get_ambientSkyColor();
  else if (name == "ambientEquatorColor") saved.saved = UnityEngine::RenderSettings::get_ambientEquatorColor();
  else if (name == "ambientGroundColor") saved.saved = UnityEngine::RenderSettings::get_ambientGroundColor();
  else if (name == "subtractiveShadowColor") saved.saved = UnityEngine::RenderSettings::get_subtractiveShadowColor();
  else if (name == "skybox") saved.saved = UnityEngine::RenderSettings::get_skybox().unsafePtr();
  else if (name == "sun") saved.saved = UnityEngine::RenderSettings::get_sun().unsafePtr();
  else if (name == "antiAliasing") saved.saved = UnityEngine::QualitySettings::get_antiAliasing();
  else if (name == "realtimeReflectionProbes") {
    auto* getter = RealtimeReflectionProbesGetter();
    saved.saved = getter != nullptr ? getter() : false;
  }
  else return;
  _savedRenderSettings.push_back(std::move(saved));
}

void Runtime::ApplyRenderSettingFloat(std::string const& name, float val) {
  SaveRenderSetting(name, RenderSettingKind::Float);
  RawApplyRenderSettingFloat(name, val);
  for (auto& s : _currentRenderSettings) {
    if (s.name == name) { s.saved = val; return; }
  }
  _currentRenderSettings.push_back(SavedRenderSetting{name, RenderSettingKind::Float, val});
}

void Runtime::ApplyRenderSettingColor(std::string const& name, UnityEngine::Color val) {
  SaveRenderSetting(name, RenderSettingKind::Color);
  RawApplyRenderSettingColor(name, val);
  for (auto& s : _currentRenderSettings) {
    if (s.name == name) { s.saved = val; return; }
  }
  _currentRenderSettings.push_back(SavedRenderSetting{name, RenderSettingKind::Color, val});
}

void Runtime::ApplyRenderSettingBool(std::string const& name, bool val) {
  SaveRenderSetting(name, RenderSettingKind::Bool);
  RawApplyRenderSettingBool(name, val);
  for (auto& s : _currentRenderSettings) {
    if (s.name == name) { s.saved = val; return; }
  }
  _currentRenderSettings.push_back(SavedRenderSetting{name, RenderSettingKind::Bool, val});
}

void Runtime::ApplyRenderSettingInt(std::string const& name, int val) {
  SaveRenderSetting(name, RenderSettingKind::Int);
  RawApplyRenderSettingInt(name, val);
  for (auto& s : _currentRenderSettings) {
    if (s.name == name) { s.saved = val; return; }
  }
  _currentRenderSettings.push_back(SavedRenderSetting{name, RenderSettingKind::Int, val});
}

void Runtime::ApplyRenderSettingObject(std::string const& name, std::string const& assetOrId) {
  if (name == "skybox") {
    auto* material = GetAssetAs<UnityEngine::Material>(assetOrId);
    if (!IsAlive(material)) {
      if (GetVivifyDebugLogging()) {
        PaperLogger.warn("Vivify SetRenderingSettings: skybox material '{}' not found", assetOrId);
      }
      return;
    }
    SaveRenderSetting(name, RenderSettingKind::Material);
    RepairMaterialShader(material, "skybox");
    UnityEngine::RenderSettings::set_skybox(material);
    for (auto& s : _currentRenderSettings) {
      if (s.name == name) { s.saved = material; return; }
    }
    _currentRenderSettings.push_back(SavedRenderSetting{name, RenderSettingKind::Material, material});
    return;
  }
  if (name == "sun") {

    auto prefabIt = _livePrefabs.find(assetOrId);
    if (prefabIt == _livePrefabs.end() || !IsAlive(prefabIt->second.gameObject)) {
      if (GetVivifyDebugLogging()) {
        PaperLogger.warn("Vivify SetRenderingSettings: sun prefab '{}' not found", assetOrId);
      }
      return;
    }
    UnityEngine::Light* directional = nullptr;
    auto lights = prefabIt->second.gameObject->GetComponentsInChildren<UnityEngine::Light*>(true);
    for (int i = 0; i < lights.size(); i++) {
      if (IsAlive(lights[i]) && lights[i]->get_type().value__ == UnityEngine::LightType::Directional.value__) {
        directional = lights[i];
        break;
      }
    }
    if (directional == nullptr) {
      if (GetVivifyDebugLogging()) {
        PaperLogger.warn("Vivify SetRenderingSettings: sun prefab '{}' has no directional light", assetOrId);
      }
      return;
    }
    SaveRenderSetting(name, RenderSettingKind::Light);
    UnityEngine::RenderSettings::set_sun(directional);
    for (auto& s : _currentRenderSettings) {
      if (s.name == name) { s.saved = directional; return; }
    }
    _currentRenderSettings.push_back(SavedRenderSetting{name, RenderSettingKind::Light, directional});
  }
}

void Runtime::UpdateRenderSettingAnimations() {
  if (_renderSettingAnimations.empty()) return;
  float const songTime = CurrentSongTime();
  auto write = _renderSettingAnimations.begin();
  for (auto read = _renderSettingAnimations.begin(); read != _renderSettingAnimations.end(); ++read) {
    float const progress = AnimationProgress(read->startTime, read->duration, read->easing, songTime);
    for (auto const& s : read->settings) {
      if (!std::holds_alternative<PointDefinitionW>(s.value)) continue;
      auto const& point = std::get<PointDefinitionW>(s.value);
      switch (s.kind) {
        case RenderSettingKind::Float:
          ApplyRenderSettingFloat(s.name, point.InterpolateLinear(progress));
          break;
        case RenderSettingKind::Color:
          ApplyRenderSettingColor(s.name, VectorToColor(point.InterpolateVector4(progress)));
          break;
        case RenderSettingKind::Int:
          ApplyRenderSettingInt(s.name, static_cast<int>(point.InterpolateLinear(progress)));
          break;
        case RenderSettingKind::Bool:
          ApplyRenderSettingBool(s.name, point.InterpolateLinear(progress) >= 1.0f);
          break;
        default:
          break;
      }
    }
    if (songTime < read->endTime) {
      if (write != read) *write = std::move(*read);
      ++write;
    }
  }
  _renderSettingAnimations.erase(write, _renderSettingAnimations.end());
}

void Runtime::RestoreRenderSettings() {
  for (auto const& s : _savedRenderSettings) {
    RawApplySavedRenderSetting(s);
  }
}

void Runtime::ReapplyCurrentRenderSettings() {
  for (auto const& s : _currentRenderSettings) {
    RawApplySavedRenderSetting(s);
  }
}

}
