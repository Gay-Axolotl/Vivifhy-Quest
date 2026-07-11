#pragma once
#include "VivifyRuntime.hpp"
#include "main.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
#include <filesystem>
#include <fstream>
#include "GlobalNamespace/AudioTimeSyncController.hpp"
#include "GlobalNamespace/AlwaysVisibleQuad.hpp"
#include "GlobalNamespace/BeatmapCallbacksController.hpp"
#include "GlobalNamespace/BloomPrePass.hpp"
#include "GlobalNamespace/BpmController.hpp"
#include "GlobalNamespace/ImageEffectController.hpp"
#include "GlobalNamespace/StaticBeatmapObjectSpawnMovementData.hpp"
#include "UnityEngine/Animator.hpp"
#include "UnityEngine/AnimatorUpdateMode.hpp"
#include "UnityEngine/AssetBundle.hpp"
#include "UnityEngine/Camera.hpp"
#include "UnityEngine/CameraClearFlags.hpp"
#include "UnityEngine/Component.hpp"
#include "UnityEngine/Color.hpp"
#include "UnityEngine/DepthTextureMode.hpp"
#include "UnityEngine/FilterMode.hpp"
#include "UnityEngine/FogMode.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/GL.hpp"
#include "UnityEngine/Graphics.hpp"
#include "UnityEngine/Light.hpp"
#include "UnityEngine/LightType.hpp"
#include "UnityEngine/Material.hpp"
#include "UnityEngine/MaterialPropertyType.hpp"
#include "UnityEngine/MaterialPropertyBlock.hpp"
#include "UnityEngine/MeshRenderer.hpp"
#include "UnityEngine/MonoBehaviour.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/ParticleSystem.hpp"
#include "UnityEngine/QualitySettings.hpp"
#include "UnityEngine/Quaternion.hpp"
#include "UnityEngine/RenderBuffer.hpp"
#include "UnityEngine/RenderSettings.hpp"
#include "UnityEngine/RenderTexture.hpp"
#include "UnityEngine/RenderTextureDescriptor.hpp"
#include "UnityEngine/RenderTextureFormat.hpp"
#include "UnityEngine/Rendering/AmbientMode.hpp"
#include "UnityEngine/Rendering/DefaultReflectionMode.hpp"
#include "UnityEngine/Shader.hpp"
#include "UnityEngine/StereoTargetEyeMask.hpp"
#include "UnityEngine/SystemInfo.hpp"
#include "UnityEngine/Texture.hpp"
#include "UnityEngine/Time.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/Vector3.hpp"
#include "UnityEngine/Vector4.hpp"
#include "UnityEngine/Renderer.hpp"
#include "UnityEngine/Rigidbody.hpp"
#include "UnityEngine/Collider.hpp"
#include "UnityEngine/Video/VideoPlayer.hpp"
#include "UnityEngine/XR/XRSettings.hpp"
#include "GlobalNamespace/SaberModelController.hpp"
#include "GlobalNamespace/Saber.hpp"
#include "GlobalNamespace/BladeMovementDataElement.hpp"
#include "GlobalNamespace/ColorManager.hpp"
#include "GlobalNamespace/IBladeMovementData.hpp"
#include "GlobalNamespace/SaberTrail.hpp"
#include "GlobalNamespace/SaberTrailRenderer.hpp"
#include "GlobalNamespace/SaberType.hpp"
#include "GlobalNamespace/ColorType.hpp"
#include "GlobalNamespace/NoteController.hpp"
#include "GlobalNamespace/GameNoteController.hpp"
#include "GlobalNamespace/BombNoteController.hpp"
#include "GlobalNamespace/BurstSliderGameNoteController.hpp"
#include "GlobalNamespace/NoteData.hpp"
#include "GlobalNamespace/NoteCutCoreEffectsSpawner.hpp"
#include "GlobalNamespace/NoteCutDirection.hpp"
#include "GlobalNamespace/NoteCutInfo.hpp"
#include "GlobalNamespace/NoteDebris.hpp"
#include "GlobalNamespace/NoteVisualModifierType.hpp"
#include "GlobalNamespace/MaterialPropertyBlockController.hpp"
#include "GlobalNamespace/NoteSpawnData.hpp"
#include "GlobalNamespace/GamePause.hpp"
#include "GlobalNamespace/LayerMasks.hpp"
#include "GlobalNamespace/PauseController.hpp"
#include "GlobalNamespace/PauseMenuManager.hpp"
#include "GlobalNamespace/VRCenterAdjust.hpp"
#include "GlobalNamespace/MainEffectController.hpp"
#include "GlobalNamespace/MainEffectContainerSO.hpp"
#include "GlobalNamespace/MainEffectSO.hpp"
#include "System/Object.hpp"
#include "beatsaber-hook/shared/utils/hooking.hpp"
#include "custom-json-data/shared/CustomBeatmapData.h"
#include "custom-json-data/shared/CustomEventData.h"
#include "custom-types/shared/macros.hpp"
#include "paper2_scotland2/shared/logger.hpp"
#include "scotland2/shared/modloader.h"
#include "songcore/shared/Capabilities.hpp"
#include "songcore/shared/SongCore.hpp"
#include "tracks/shared/Animation/Easings.h"
#include "tracks/shared/Animation/GameObjectTrackController.hpp"
#include "tracks/shared/Animation/PointDefinition.h"
#include "tracks/shared/Animation/TransformData.hpp"
#include "tracks/shared/AssociatedData.h"
#include "tracks/shared/Constants.h"
#include "tracks/shared/StaticHolders.hpp"
#include "web-utils/shared/WebUtils.hpp"
#include "bsml/shared/BSML/MainThreadScheduler.hpp"
#include "metacore/shared/game.hpp"
#include "beatsaber-hook/shared/config/rapidjson-utils.hpp"

namespace Vivify {
struct FollowedSaberTrail;
using namespace std::string_view_literals;

inline constexpr std::string_view kCapability = "Vivify"sv;
inline constexpr std::string_view kBundleFile = "bundleAndroid2021.vivify"sv;

inline constexpr std::string_view kInstantiatePrefabEvent = "InstantiatePrefab"sv;
inline constexpr std::string_view kDestroyObjectEvent = "DestroyObject"sv;
inline constexpr std::string_view kSetMaterialPropertyEvent = "SetMaterialProperty"sv;
inline constexpr std::string_view kSetAnimatorPropertyEvent = "SetAnimatorProperty"sv;
inline constexpr std::string_view kSetGlobalPropertyEvent = "SetGlobalProperty"sv;
inline constexpr std::string_view kAssignObjectPrefabEvent = "AssignObjectPrefab"sv;
inline constexpr std::string_view kBlitEvent = "Blit"sv;
inline constexpr std::string_view kCreateCameraEvent = "CreateCamera"sv;
inline constexpr std::string_view kCreateScreenTextureEvent = "CreateScreenTexture"sv;
inline constexpr std::string_view kSetCameraPropertyEvent = "SetCameraProperty"sv;
inline constexpr std::string_view kSetRenderingSettingsEvent = "SetRenderingSettings"sv;
inline constexpr std::string_view kMainCameraId = "_Main"sv;
inline constexpr int kMaxActiveBlitEffects = 96;
inline constexpr int kMaxRenderTextureSize = 4096;
inline constexpr float kMaxTrailDuration = 2.0f;
inline constexpr int kMaxTrailSamplingFrequency = 120;
inline constexpr int kMaxTrailGranularity = 240;
inline constexpr int kGameplayOverlaySortingOrder = 32767;
enum class MaterialPropertyKind {
  Unsupported,
  Texture,
  Color,
  Float,
  Int,
  Vector,
  Keyword,
};
enum class AnimatorPropertyKind {
  Unsupported,
  Bool,
  Float,
  Integer,
  Trigger,
};
using MaterialValue = std::variant<std::monostate, std::string, bool, float, int, PointDefinitionW>;
using AnimatorValue = std::variant<std::monostate, bool, float, int, PointDefinitionW>;
using SavedGlobalValue = std::variant<UnityEngine::Texture*, UnityEngine::Color, float, UnityEngine::Vector4>;
struct MaterialPropertyChange {
  std::variant<int, std::string> id;
  MaterialPropertyKind kind = MaterialPropertyKind::Unsupported;
  MaterialValue value;
};
struct AnimatorPropertyChange {

  int id = 0;
  std::string name;
  AnimatorPropertyKind kind = AnimatorPropertyKind::Unsupported;
  AnimatorValue value;
};
struct InstantiatePrefabData {
  std::string asset;
  std::optional<std::string> id;
  Tracks::TransformData transformData;
  std::vector<TrackW> tracks;
  UnityEngine::GameObject* instance = nullptr;
};
struct LivePrefab {
  UnityEngine::GameObject* gameObject = nullptr;
  std::vector<TrackW> tracks;
  std::vector<UnityEngine::Animator*> animators;
  std::vector<UnityEngine::ParticleSystem*> particleSystems;
};

struct SyncedObject {
  UnityEngine::GameObject* root = nullptr;
  float startTime = 0.0f;
  std::vector<UnityEngine::Video::VideoPlayer*> videoPlayers;
  std::vector<UnityEngine::Animator*> animators;
  std::vector<UnityEngine::ParticleSystem*> particleSystems;
};
struct ActiveMaterialAnimation {
  UnityEngine::Material* material = nullptr;
  std::vector<MaterialPropertyChange> properties;
  float startTime = 0.0f;
  float duration = 0.0f;
  float endTime = 0.0f;
  Functions easing = Functions::EaseLinear;
};
struct ActiveGlobalAnimation {
  std::vector<MaterialPropertyChange> properties;
  float startTime = 0.0f;
  float duration = 0.0f;
  float endTime = 0.0f;
  Functions easing = Functions::EaseLinear;
};
struct ActiveAnimatorAnimation {
  std::string prefabId;
  std::vector<AnimatorPropertyChange> properties;
  float startTime = 0.0f;
  float duration = 0.0f;
  float endTime = 0.0f;
  Functions easing = Functions::EaseLinear;
};

enum class PostProcessingOrder {
  BeforeSkybox,
  AfterSkybox,
  BeforeOpaque,
  AfterOpaque,
  BeforeAlpha,
  AfterAlpha,
  BeforeMainEffect,
  AfterMainEffect,
};
inline constexpr int kMidRenderOrderCount = 6;
struct BlitMaterialData {
  UnityEngine::Material* material = nullptr;
  int priority = 0;
  std::string source;
  std::vector<std::string> targets;
  int pass = -1;
  std::optional<int> frame;
  std::string asset;
  float eventBeat = 0.0f;
};
struct ActiveBlitEffect {
  BlitMaterialData data;
  float expireTime = 0.0f;
};
struct DeclaredTextureData {
  std::string name;
  int propertyId = 0;
  float xRatio = 1.0f;
  float yRatio = 1.0f;
  std::optional<int> width;
  std::optional<int> height;
  std::optional<UnityEngine::RenderTextureFormat> format;
  std::optional<UnityEngine::FilterMode> filterMode;
  UnityEngine::RenderTexture* texture = nullptr;
};

struct CullingMaskData {
  std::vector<TrackW> tracks;
  bool whitelist = false;
};
struct CameraPropertyData {
  std::optional<UnityEngine::DepthTextureMode> depthTextureMode;
  std::optional<UnityEngine::CameraClearFlags> clearFlags;
  std::optional<UnityEngine::Color> backgroundColor;
  std::optional<CullingMaskData> culling;
  std::optional<bool> bloomPrePass;
  std::optional<bool> mainEffect;
};
struct SecondaryCameraData {
  std::string name;
  std::optional<std::string> textureName;
  std::optional<std::string> depthTextureName;
  std::optional<int> texturePropertyId;
  std::optional<int> depthTexturePropertyId;
  CameraPropertyData properties;
  UnityEngine::Camera* camera = nullptr;
  UnityEngine::RenderTexture* colorRT = nullptr;
  UnityEngine::RenderTexture* depthRT = nullptr;
};
enum class RenderSettingKind { Float, Color, Bool, Int, Material, Light };
struct RenderSettingValue {
  std::string name;
  RenderSettingKind kind = RenderSettingKind::Float;
  std::variant<std::monostate, float, UnityEngine::Color, int, bool, std::string, PointDefinitionW> value;
};
struct SavedRenderSetting {
  std::string name;
  RenderSettingKind kind;
  std::variant<float, UnityEngine::Color, int, bool, UnityEngine::Material*, UnityEngine::Light*> saved;
};
struct ActiveRenderSettingAnimation {
  std::vector<RenderSettingValue> settings;
  float startTime = 0.0f;
  float duration = 0.0f;
  float endTime = 0.0f;
  Functions easing = Functions::EaseLinear;
};
enum class AssignedPrefabKind { Object, AnyDirectionObject, Debris, Trail };
struct AssignedPrefabInfo {
  std::string asset;
  std::vector<TrackW> tracks;
  std::string objectType;
  std::optional<int> saberType;
  AssignedPrefabKind kind = AssignedPrefabKind::Object;
  bool additive = false;
  std::optional<UnityEngine::Vector3> trailTopPos;
  std::optional<UnityEngine::Vector3> trailBottomPos;
  std::optional<float> trailDuration;
  std::optional<int> trailSamplingFrequency;
  std::optional<int> trailGranularity;
};
struct VisualReplacement {
  std::vector<UnityEngine::GameObject*> spawnedObjects;
  std::vector<UnityEngine::Renderer*> disabledRenderers;
  std::vector<UnityEngine::Renderer*> replacementRenderers;
  std::vector<FollowedSaberTrail*> followedTrails;
  GlobalNamespace::MaterialPropertyBlockController* materialPropertyBlockController = nullptr;
  ArrayW<UnityW<UnityEngine::Renderer>, Array<UnityW<UnityEngine::Renderer>>*> originalMaterialBlockRenderers;
  bool hasOriginalMaterialBlockRenderers = false;
  UnityEngine::Color lastSaberColor;
  bool hasLastSaberColor = false;
};
struct ActiveSaberVisual {
  GlobalNamespace::SaberModelController* controller = nullptr;
  GlobalNamespace::Saber* saber = nullptr;
  UnityEngine::Transform* parent = nullptr;
};
inline bool IsVivifyEvent(std::string_view type) {
  static std::unordered_set<std::string_view> const kVivifyEvents = {
      kInstantiatePrefabEvent, kDestroyObjectEvent, kSetMaterialPropertyEvent,
      kSetAnimatorPropertyEvent, kSetGlobalPropertyEvent, kAssignObjectPrefabEvent,
      kBlitEvent, kCreateCameraEvent, kCreateScreenTextureEvent,
      kSetCameraPropertyEvent, kSetRenderingSettingsEvent,
  };
  return kVivifyEvents.count(type) > 0;
}
inline std::string NormalizeAssetKey(std::string_view input) {
  std::string key(input);
  for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return key;
}
inline std::string JoinPath(std::string_view left, std::string_view right) {
  std::string result(left);
  if (!result.empty() && result.back() != '/' && result.back() != '\\') {
    result.push_back('/');
  }
  result.append(right);
  return result;
}
inline std::optional<std::string_view> ReadStringView(rapidjson::Value const& object, std::string_view key) {
  auto member = object.FindMember(key.data());
  if (member == object.MemberEnd() || !member->value.IsString()) {
    return std::nullopt;
  }
  return member->value.GetString();
}
inline std::optional<float> ReadFloat(rapidjson::Value const& object, std::string_view key) {
  auto member = object.FindMember(key.data());
  if (member == object.MemberEnd()) {
    return std::nullopt;
  }
  if (member->value.IsNumber()) {
    return member->value.GetFloat();
  }
  if (member->value.IsBool()) {
    return member->value.GetBool() ? 1.0f : 0.0f;
  }
  return std::nullopt;
}
inline std::optional<int> ReadInt(rapidjson::Value const& object, std::string_view key) {
  auto member = object.FindMember(key.data());
  if (member == object.MemberEnd()) {
    return std::nullopt;
  }
  if (member->value.IsInt()) {
    return member->value.GetInt();
  }
  if (member->value.IsNumber()) {
    return static_cast<int>(member->value.GetFloat());
  }
  return std::nullopt;
}
inline std::optional<bool> ReadBool(rapidjson::Value const& object, std::string_view key) {
  auto member = object.FindMember(key.data());
  if (member == object.MemberEnd()) {
    return std::nullopt;
  }
  auto const& value = member->value;
  if (value.IsBool()) {
    return value.GetBool();
  }
  if (value.IsNumber()) {
    return value.GetFloat() != 0.0f;
  }
  if (value.IsString()) {
    return std::string_view(value.GetString()) == "true"sv;
  }
  return std::nullopt;
}
inline std::optional<UnityEngine::Vector3> ReadVector3(rapidjson::Value const& object, std::string_view key) {
  auto member = object.FindMember(key.data());
  if (member == object.MemberEnd() || !member->value.IsArray() || member->value.Size() < 3) {
    return std::nullopt;
  }
  auto arr = member->value.GetArray();
  if (!arr[0].IsNumber() || !arr[1].IsNumber() || !arr[2].IsNumber()) {
    return std::nullopt;
  }
  return UnityEngine::Vector3(arr[0].GetFloat(), arr[1].GetFloat(), arr[2].GetFloat());
}

inline std::optional<UnityEngine::Color> ReadColorArray(rapidjson::Value const& value) {
  if (!value.IsArray() || value.Size() < 3 || value.Size() > 4) {
    return std::nullopt;
  }
  auto arr = value.GetArray();
  for (auto const& entry : arr) {
    if (!entry.IsNumber()) return std::nullopt;
  }
  return UnityEngine::Color(arr[0].GetFloat(), arr[1].GetFloat(), arr[2].GetFloat(),
                            value.Size() > 3 ? arr[3].GetFloat() : 1.0f);
}
inline rapidjson::Value const* ReadValuePtr(rapidjson::Value const& object, std::string_view key) {
  auto member = object.FindMember(key.data());
  return member == object.MemberEnd() ? nullptr : &member->value;
}
enum class AssetValueState { Missing, Null, String };
struct AssetValue {
  AssetValueState state = AssetValueState::Missing;
  std::string value;
};
inline AssetValue ReadAssetValue(rapidjson::Value const& object, std::string_view key) {
  auto* value = ReadValuePtr(object, key);
  if (value == nullptr) {
    return {};
  }
  if (value->IsNull()) {
    return {.state = AssetValueState::Null};
  }
  if (value->IsString()) {
    return {.state = AssetValueState::String, .value = value->GetString()};
  }
  return {};
}
inline std::vector<std::string> ReadStringListOrSingle(rapidjson::Value const& object, std::string_view key) {
  std::vector<std::string> result;
  auto value = ReadValuePtr(object, key);
  if (value == nullptr) {
    return result;
  }
  if (value->IsString()) {
    result.emplace_back(value->GetString());
    return result;
  }
  if (!value->IsArray()) {
    return result;
  }
  result.reserve(value->Size());
  for (auto const& entry : value->GetArray()) {
    if (entry.IsString()) {
      result.emplace_back(entry.GetString());
    }
  }
  return result;
}
inline Functions ParseEasing(std::string_view easing) {
  static std::unordered_map<std::string_view, Functions> const kEasingMap = {
      {"easeStep"sv, Functions::EaseStep},
      {"easeInQuad"sv, Functions::EaseInQuad},
      {"easeOutQuad"sv, Functions::EaseOutQuad},
      {"easeInOutQuad"sv, Functions::EaseInOutQuad},
      {"easeInCubic"sv, Functions::EaseInCubic},
      {"easeOutCubic"sv, Functions::EaseOutCubic},
      {"easeInOutCubic"sv, Functions::EaseInOutCubic},
      {"easeInQuart"sv, Functions::EaseInQuart},
      {"easeOutQuart"sv, Functions::EaseOutQuart},
      {"easeInOutQuart"sv, Functions::EaseInOutQuart},
      {"easeInQuint"sv, Functions::EaseInQuint},
      {"easeOutQuint"sv, Functions::EaseOutQuint},
      {"easeInOutQuint"sv, Functions::EaseInOutQuint},
      {"easeInSine"sv, Functions::EaseInSine},
      {"easeOutSine"sv, Functions::EaseOutSine},
      {"easeInOutSine"sv, Functions::EaseInOutSine},
      {"easeInCirc"sv, Functions::EaseInCirc},
      {"easeOutCirc"sv, Functions::EaseOutCirc},
      {"easeInOutCirc"sv, Functions::EaseInOutCirc},
      {"easeInExpo"sv, Functions::EaseInExpo},
      {"easeOutExpo"sv, Functions::EaseOutExpo},
      {"easeInOutExpo"sv, Functions::EaseInOutExpo},
      {"easeInElastic"sv, Functions::EaseInElastic},
      {"easeOutElastic"sv, Functions::EaseOutElastic},
      {"easeInOutElastic"sv, Functions::EaseInOutElastic},
      {"easeInBack"sv, Functions::EaseInBack},
      {"easeOutBack"sv, Functions::EaseOutBack},
      {"easeInOutBack"sv, Functions::EaseInOutBack},
      {"easeInBounce"sv, Functions::EaseInBounce},
      {"easeOutBounce"sv, Functions::EaseOutBounce},
      {"easeInOutBounce"sv, Functions::EaseInOutBounce},
  };
  auto it = kEasingMap.find(easing);
  return it != kEasingMap.end() ? it->second : Functions::EaseLinear;
}
inline MaterialPropertyKind ParseMaterialPropertyKind(std::string_view kind) {
  static std::unordered_map<std::string_view, MaterialPropertyKind> const kMap = {
      {"Texture"sv, MaterialPropertyKind::Texture},
      {"Color"sv, MaterialPropertyKind::Color},
      {"Float"sv, MaterialPropertyKind::Float},
      {"Int"sv, MaterialPropertyKind::Int},
      {"Vector"sv, MaterialPropertyKind::Vector},
      {"Keyword"sv, MaterialPropertyKind::Keyword},
  };
  auto it = kMap.find(kind);
  return it != kMap.end() ? it->second : MaterialPropertyKind::Unsupported;
}
inline AnimatorPropertyKind ParseAnimatorPropertyKind(std::string_view kind) {
  static std::unordered_map<std::string_view, AnimatorPropertyKind> const kMap = {
      {"Bool"sv, AnimatorPropertyKind::Bool},
      {"Float"sv, AnimatorPropertyKind::Float},
      {"Integer"sv, AnimatorPropertyKind::Integer},
      {"Trigger"sv, AnimatorPropertyKind::Trigger},
  };
  auto it = kMap.find(kind);
  return it != kMap.end() ? it->second : AnimatorPropertyKind::Unsupported;
}
inline UnityEngine::Color VectorToColor(NEVector::Vector4 value) {
  return UnityEngine::Color(value.x, value.y, value.z, value.w);
}
inline UnityEngine::Vector4 ToUnityVector(NEVector::Vector4 value) {
  return UnityEngine::Vector4(value.x, value.y, value.z, value.w);
}
inline int ColorPropertyId() {
  static int id = UnityEngine::Shader::PropertyToID(u"_Color");
  return id;
}
inline bool IsManagedAlive(UnityEngine::Object* object) {
  return object != nullptr && UnityEngine::Object::op_Implicit_bool(object);
}
inline bool NearlySameColor(UnityEngine::Color a, UnityEngine::Color b) {
  return std::fabs(a.r - b.r) < 0.001f &&
         std::fabs(a.g - b.g) < 0.001f &&
         std::fabs(a.b - b.b) < 0.001f &&
         std::fabs(a.a - b.a) < 0.001f;
}
inline std::string ToStdString(::StringW value) {
  return value ? il2cpp_utils::detail::to_string(value) : std::string("<null>");
}
inline std::string BoolText(bool value) {
  return value ? "true" : "false";
}
inline std::string ShaderNameForLog(UnityEngine::Shader* shader) {
  if (!IsManagedAlive(shader)) return "<null>";
  return ToStdString(shader->get_name());
}
inline bool IsInternalErrorShaderName(std::string_view shaderName) {
  return shaderName.find("Hidden/InternalErrorShader") != std::string_view::npos;
}
inline UnityEngine::Vector3 AddVectors(UnityEngine::Vector3 left, UnityEngine::Vector3 right) {
  return UnityEngine::Vector3(left.x + right.x, left.y + right.y, left.z + right.z);
}
inline void ClearRenderTexture(UnityEngine::RenderTexture* rt) {
  if (!IsManagedAlive(rt)) return;
  UnityEngine::RenderTexture* prevActive = UnityEngine::RenderTexture::get_active().unsafePtr();
  UnityEngine::RenderTexture::set_active(rt);
  UnityEngine::GL::Clear(true, true, UnityEngine::Color(0.0f, 0.0f, 0.0f, 0.0f));
  UnityEngine::RenderTexture::set_active(prevActive);
}
}
