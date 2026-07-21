#pragma once
#include <string>
#include <vector>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <optional>
#include "VivifyTypes.hpp"
#include "UnityEngine/Rendering/CommandBuffer.hpp"
#include "beatsaber-hook/shared/utils/typedefs-wrappers.hpp"

namespace Vivify {
struct FollowedSaberTrail;
struct RuntimeBehaviour;
struct CullingCameraController;
struct MultipassKeywordController;
struct CameraApplier;

class Runtime {
public:
  static Runtime& Instance();

  CustomJSONData::CustomBeatmapData* GetCurrentBeatmapData() const { return _currentBeatmapData; }
  bool IsResetting() const { return _isResetting; }
  bool GetPreEffectsEmpty() const { return _preEffects.empty(); }
  bool GetPostEffectsEmpty() const { return _postEffects.empty(); }

  void AddMidRenderCommandBuffers(UnityEngine::Camera* camera);
  void RemoveMidRenderCommandBuffers();
  void CacheMainRenderDescriptor(UnityEngine::RenderTexture* src);
  bool HasMidRenderEffects() const {
    for (auto const& bucket : _midEffects) {
      if (!bucket.empty()) return true;
    }
    return false;
  }

  void LateLoad();
  void Update();
  void OnBehaviourDestroyed(RuntimeBehaviour* behaviour);
  void SetPauseMenuActive(bool active);
  void RefreshMultipassRendering();
  void RefreshIsolationSettings();

  void RegisterSyncedObject(UnityEngine::GameObject* root, float startTime);
  void UnregisterSyncedObject(UnityEngine::GameObject* root);

  std::vector<AssignedPrefabInfo*> FindAssignedPrefabs(std::string_view objectType,
                                                       GlobalNamespace::NoteData* noteData,
                                                       AssignedPrefabKind kind);
  std::vector<AssignedPrefabInfo*> FindAssignedSaberPrefabs(int type);
  std::vector<AssignedPrefabInfo*> FindAssignedSaberTrailPrefabs(int type);
  std::vector<AssignedPrefabInfo*> FindAssignedDebrisPrefabs(GlobalNamespace::NoteData* noteData);

  bool IsReduceDebrisEnabled();
  void PushActiveDebrisPrefabs(std::vector<AssignedPrefabInfo*> infos);
  void PopActiveDebrisPrefabs();
  void ApplyNotePrefabFor(GlobalNamespace::NoteController* noteController);
  void RestoreNoteVisuals(GlobalNamespace::NoteController* noteController);
  void RestoreDebrisVisuals(GlobalNamespace::NoteDebris* debris);
  void RestoreSaberVisuals(GlobalNamespace::SaberModelController* smc);
  void ReplaceNoteVisuals(GlobalNamespace::NoteController* noteController,
                          std::vector<AssignedPrefabInfo*> const& infos);
  void TrackSaberModel(GlobalNamespace::SaberModelController* smc, GlobalNamespace::Saber* saber,
                       UnityEngine::Transform* parent);
  void ApplySaberVisualsToActive();
  void ApplySaberVisuals(GlobalNamespace::SaberModelController* smc, GlobalNamespace::Saber* saber,
                         UnityEngine::Transform* parent);
  void ReplaceDebrisVisuals(GlobalNamespace::NoteDebris* debris);

  void ApplyBlits(UnityEngine::RenderTexture* src, UnityEngine::RenderTexture* dest,
                  std::string const& cameraName = "_Main", int phase = 0);

  bool ShouldBypassImageEffect(GlobalNamespace::ImageEffectController* controller);

  void ApplySecondaryCameraMainEffects(GlobalNamespace::MainEffectController* mainEffectController);

  void BindSecondaryCameraTextures();
  void RestoreSecondaryCullingLayers();

private:
  Runtime() = default;

  static void OnCustomEventStatic(GlobalNamespace::BeatmapCallbacksController* callbackController,
                                  CustomJSONData::CustomEventData* customEventData);
  void EnsureBehaviour();
  void ResetRuntime();
  float CurrentSongTime();
  void DetectSongRestart();
  float CurrentBpm() const;
  float DurationBeatsToSeconds(float durationBeats) const;
  bool IsAlive(UnityEngine::Object* object) const;
  void UpdateSyncedObjects();

  void UpdatePrefabAnimationSpeed();
  void LogThrottledUpdateError(std::string_view what);

  void HandleLevelSelected(SongCore::API::LevelSelect::LevelWasSelectedEventArgs const& event);
  void DownloadBundle(uint32_t checksum, std::string const& levelPath, std::function<void(bool)> callback);
  void PreloadBundle(std::string const& bundlePath);
  void LoadMainBundle();
  void CacheBundleAssets();
  UnityEngine::Object* GetAssetObject(std::string_view assetName) const;
  template <typename T>
  T* GetAssetAs(std::string_view assetName) const {
    auto* asset = GetAssetObject(assetName);
    if (asset == nullptr) {
      return nullptr;
    }
    return il2cpp_utils::try_cast<T>(asset).value_or(nullptr);
  }
  void LogUnityPlatformInfoOnce();
  UnityEngine::RenderTextureFormat SupportedRenderTextureFormat(UnityEngine::RenderTextureFormat requested,
                                                                std::string_view context) const;
  void LogMaterialShader(std::string_view context, std::string_view assetPath, UnityEngine::Material* material) const;
  UnityEngine::Shader* FindUsableShader(std::string const& shaderName) const;
  UnityEngine::Shader* FindFallbackShader() const;
  void RepairMaterialShader(UnityEngine::Material* material, std::string_view context = {});
  void RepairGameObjectMaterials(UnityEngine::GameObject* gameObject, std::string_view context = {});
  void ApplyStereoKeywords(UnityEngine::Material* material) const;
  void ApplyGameObjectStereoKeywords(UnityEngine::GameObject* gameObject);
  void RefreshLoadedMaterialStereoKeywords();
  void RepairLoadedMaterialShaders();
  void SetMaterialKeyword(UnityEngine::Material* material, ::StringW keyword, bool enabled) const;

  void HandleCustomEvent(GlobalNamespace::BeatmapCallbacksController* callbackController,
                         CustomJSONData::CustomEventData* customEventData);
  void DispatchEvent(std::string_view type, CustomJSONData::CustomEventData* customEventData,
                     rapidjson::Value const& json);
  CustomJSONData::CustomBeatmapData* GetCustomBeatmapData(GlobalNamespace::BeatmapCallbacksController* callbackController);
  bool EnsureBeatmapPrepared(GlobalNamespace::BeatmapCallbacksController* callbackController, float triggerTime = 0.0f);
  void PrepareBeatmap(CustomJSONData::CustomBeatmapData* beatmapData, float triggerTime = 0.0f);

  void ApplyMissedVivifyEvents(float upToTime);
  rapidjson::Value const* GetEventJson(CustomJSONData::CustomEventData* customEventData) const;
  std::optional<PointDefinitionW> MakePointDefinition(rapidjson::Value const& object,
                                                      std::string_view key,
                                                      Tracks::ffi::WrapBaseValueType type);
  std::vector<TrackW> ReadTracks(rapidjson::Value const& object, bool v2);

  std::optional<MaterialPropertyChange> ParseMaterialProperty(rapidjson::Value const& propertyJson);
  std::vector<MaterialPropertyChange> ParseMaterialProperties(rapidjson::Value const& json);
  std::optional<AnimatorPropertyChange> ParseAnimatorProperty(rapidjson::Value const& propertyJson);
  std::vector<AnimatorPropertyChange> ParseAnimatorProperties(rapidjson::Value const& json);
  bool IsAnimated(MaterialPropertyChange const& property) const;
  bool IsAnimated(AnimatorPropertyChange const& property) const;
  void ApplyMaterialProperty(UnityEngine::Material* material, MaterialPropertyChange const& property, float progress);
  void SaveOriginalGlobalProperty(MaterialPropertyChange const& property);
  void ApplyGlobalProperty(MaterialPropertyChange const& property, float progress);
  void ApplyAnimatorProperty(std::vector<UnityEngine::Animator*> const& animators,
                             AnimatorPropertyChange const& property, float progress);
  void HandleSetMaterialProperty(CustomJSONData::CustomEventData* customEventData, rapidjson::Value const& json);
  void HandleSetAnimatorProperty(CustomJSONData::CustomEventData* customEventData, rapidjson::Value const& json);
  void HandleSetGlobalProperty(CustomJSONData::CustomEventData* customEventData, rapidjson::Value const& json);
  float AnimationProgress(float startTime, float duration, Functions easing, float songTime) const;
  void UpdateMaterialAnimations();
  void UpdateGlobalAnimations();
  void UpdateAnimatorAnimations();
  void RestoreGlobalProperties();
  void ReapplyCurrentGlobalProperties();
  void QueueMaterialPropertyAnimation(UnityEngine::Material* material,
                                      std::vector<MaterialPropertyChange> const& properties,
                                      float startTime, float duration, Functions easing);

  void HandleSetRenderingSettings(CustomJSONData::CustomEventData* customEventData, rapidjson::Value const& json);
  void SaveRenderSetting(std::string const& name, RenderSettingKind kind);
  void ApplyRenderSettingFloat(std::string const& name, float val);
  void ApplyRenderSettingColor(std::string const& name, UnityEngine::Color val);
  void ApplyRenderSettingBool(std::string const& name, bool val);
  void ApplyRenderSettingInt(std::string const& name, int val);
  void ApplyRenderSettingObject(std::string const& name, std::string const& assetOrId);
  void ParseAndApplyRenderSetting(std::string const& key, rapidjson::Value const& parent,
                                  rapidjson::Value const& val,
                                  std::vector<RenderSettingValue>& out, bool immediate);
  void UpdateRenderSettingAnimations();
  void RestoreRenderSettings();
  void ReapplyCurrentRenderSettings();

  bool SameBlitData(BlitMaterialData const& left, BlitMaterialData const& right);
  PostProcessingOrder ParsePostProcessingOrder(rapidjson::Value const& json);
  void AddOrUpdateBlitEffect(std::vector<ActiveBlitEffect>& effects, ActiveBlitEffect effect);
  void HandleBlit(CustomJSONData::CustomEventData* customEventData, rapidjson::Value const& json);
  void UpdateBlitEffects();
  void HandleCreateScreenTexture(rapidjson::Value const& json);
  void HandleCreateCamera(rapidjson::Value const& json);
  std::optional<UnityEngine::DepthTextureMode> ParseDepthTextureModeName(std::string_view s);
  std::optional<UnityEngine::DepthTextureMode> ParseDepthTextureModeValue(rapidjson::Value const& value);
  void ParseCameraPropertyData(rapidjson::Value const& json, CameraPropertyData& out);
  void ApplyCameraProperties(UnityEngine::Camera* camera, CameraPropertyData const& props);
  void ApplyCameraGameObjectProperties(UnityEngine::Camera* camera, CameraPropertyData const& props);
  void HandleSetCameraProperty(rapidjson::Value const& json);
  void CaptureMainCameraOriginals(UnityEngine::Camera* mainCam, UnityEngine::GameObject* mainCamGO);
  void RestoreMainCameraOriginals();
  UnityEngine::Rendering::CommandBuffer* BuildMidRenderCommandBuffer(std::vector<ActiveBlitEffect> const& effects);
  bool EnsureMidRenderTextures();
  void ReleaseMidRenderTextures();
  UnityEngine::RenderTexture* EnsureCachedBlitTexture(UnityEngine::RenderTexture*& texture,
                                                      UnityEngine::RenderTexture* src);
  void ReleaseRenderTexture(UnityEngine::RenderTexture*& texture);
  void ReleaseCachedBlitTextures();
  void RefreshCameraComponents(bool allowCameraApplier);
  void RefreshCameraApplier(UnityEngine::GameObject* mainCamGO, bool allowCameraApplier);
  void DestroyCameraApplier();
  void RefreshMultipassRendering(UnityEngine::GameObject* mainCamGO);
  MultipassKeywordController* EnsureMultipassKeywordController(UnityEngine::GameObject* gameObject);
  CullingCameraController* EnsureCullingController(UnityEngine::GameObject* gameObject);
  void ApplyCullingProperty(UnityEngine::GameObject* cameraGO, std::optional<CullingMaskData> const& culling);
  void ReleaseDeclaredTextureData(DeclaredTextureData& data);
  void ReleaseSecondaryCameraData(SecondaryCameraData& data);
  UnityEngine::RenderTexture* SecondaryCameraColorRT(SecondaryCameraData const& cam);
  bool DestroyDeclaredTextureById(std::string const& id);
  bool DestroySecondaryCameraById(std::string const& id);
  bool CanUseBlitMaterial(UnityEngine::Material* material, int pass) const;

  void CleanCustomObject(UnityEngine::GameObject* go);
  void PreloadInstantiatePrefabs();
  std::string GetPrefabStorageId(CustomJSONData::CustomEventData* customEventData,
                                 InstantiatePrefabData const& data) const;
  void InstantiatePrefab(CustomJSONData::CustomEventData* customEventData, rapidjson::Value const& json);
  bool DestroyPrefabById(std::string const& id);
  void DestroyObjects(rapidjson::Value const& json);
  void HandleAssignObjectPrefab(CustomJSONData::CustomEventData* customEventData, rapidjson::Value const& json);

  void RefreshActiveNoteVisuals();
  std::string ComputePrefabFingerprint(std::vector<AssignedPrefabInfo*> const& infos) const;
  std::string ComputeSaberFingerprint(std::vector<AssignedPrefabInfo*> const& modelInfos,
                                      std::vector<AssignedPrefabInfo*> const& trailInfos) const;
  bool ReplacementIntact(VisualReplacement const& replacement) const;
  void ReassertNoteReplacement(GlobalNamespace::NoteController* noteController, VisualReplacement& replacement);
  bool AssignmentMatchesTracks(AssignedPrefabInfo const& info, GlobalNamespace::NoteData* noteData);
  void AddAssignedPrefab(std::string_view objectType, AssignedPrefabKind kind, std::string asset,
                         std::vector<TrackW> tracks, bool additive, std::optional<int> saberType = std::nullopt);
  void AddAssignedTrail(std::string asset, bool additive, int saberType,
                        std::optional<UnityEngine::Vector3> topPos, std::optional<UnityEngine::Vector3> bottomPos,
                        std::optional<float> duration, std::optional<int> samplingFrequency,
                        std::optional<int> granularity);
  bool TracksOverlap(std::vector<TrackW> const& left, std::vector<TrackW> const& right) const;
  void ClearAssignedPrefabs(std::string_view objectType, std::optional<AssignedPrefabKind> kind = std::nullopt,
                            std::optional<int> saberType = std::nullopt, std::vector<TrackW> const* tracks = nullptr);
  std::vector<AssignedPrefabInfo*> GetValidPrefabInfos(std::vector<AssignedPrefabInfo*> const& infos);
  bool ShouldHideOriginal(std::vector<AssignedPrefabInfo*> const& infos) const;
  void DisableOriginalRenderers(UnityEngine::GameObject* gameObject, VisualReplacement& replacement);
  void DisableOriginalRenderers(ArrayW<UnityEngine::Renderer*, Array<UnityEngine::Renderer*>*> const& renderers,
                                VisualReplacement& replacement);
  UnityEngine::Transform* GetReplacementParent(GlobalNamespace::NoteController* noteController);
  GlobalNamespace::MaterialPropertyBlockController* GetReplacementMaterialPropertyBlockController(
      GlobalNamespace::NoteController* noteController, UnityEngine::Transform* replacementParent);
  void RestoreReplacementData(VisualReplacement& replacement);
  void CacheReplacementRenderers(UnityEngine::GameObject* spawned, VisualReplacement& replacement);
  void InstantiateReplacementPrefab(AssignedPrefabInfo const& info, UnityEngine::Transform* parent,
                                    VisualReplacement& replacement);
  UnityEngine::Color GetSaberColor(GlobalNamespace::SaberModelController* smc, GlobalNamespace::Saber* saber);
  UnityEngine::Color GetNoteColor(GlobalNamespace::NoteController* noteController);
  void ApplyColorToRenderers(std::vector<UnityEngine::Renderer*> const& renderers, UnityEngine::Color color);
  void ApplySaberReplacementColor(GlobalNamespace::SaberModelController* smc, GlobalNamespace::Saber* saber,
                                  VisualReplacement& replacement, bool force = false);
  void UpdateSaberReplacementColors();
  void ApplySaberTrailVisuals(GlobalNamespace::SaberModelController* smc,
                              std::vector<AssignedPrefabInfo*> const& infos, VisualReplacement& replacement);
  void ApplyReplacementRenderersToMaterialBlock(GlobalNamespace::MaterialPropertyBlockController* mpb,
                                                VisualReplacement& replacement, bool hideOriginal);
  void ApplyReplacementRenderersToMaterialBlock(UnityEngine::GameObject* gameObject,
                                                VisualReplacement& replacement, bool hideOriginal);
  void RestoreAllVisualReplacements();
  void PurgeInvalidActiveSabers();
  void ForceGameObjectRenderersOnTop(UnityEngine::GameObject* gameObject);
  void ForceRendererOnTop(UnityEngine::Renderer* renderer);
  void RestoreOverlayRenderState();
  void SetLayerRecursively(UnityEngine::GameObject* gameObject, int layer);

  RuntimeBehaviour* _behaviour = nullptr;
  CustomJSONData::CustomBeatmapData* _currentBeatmapData = nullptr;
  GlobalNamespace::AudioTimeSyncController* _audioTimeSyncController = nullptr;
  TracksAD::BeatmapAssociatedData* _beatmapAD = nullptr;
  TracksAD::BeatmapAssociatedData _fallbackBeatmapAD;
  UnityEngine::AssetBundle* _mainBundle = nullptr;
  std::string _selectedLevelPath;
  std::string _preloadedBundlePath;
  bool _selectedMapHasVivifyRequirement = false;
  std::unordered_map<std::string, UnityEngine::Object*> _assets;
  std::unordered_map<std::string, UnityEngine::Object*> _assetsByName;
  std::unordered_map<std::string, UnityEngine::Shader*> _supportedShadersByName;
  std::unordered_map<CustomJSONData::CustomEventData*, InstantiatePrefabData> _instantiatePrefabs;
  std::unordered_map<std::string, LivePrefab> _livePrefabs;
  std::vector<SyncedObject> _syncedObjects;
  std::vector<ActiveMaterialAnimation> _materialAnimations;
  std::vector<ActiveGlobalAnimation> _globalAnimations;
  std::vector<ActiveAnimatorAnimation> _animatorAnimations;

  std::vector<ActiveMaterialAnimation> _pausedMaterialAnimations;
  std::vector<ActiveGlobalAnimation> _pausedGlobalAnimations;
  std::vector<ActiveAnimatorAnimation> _pausedAnimatorAnimations;
  std::vector<ActiveRenderSettingAnimation> _pausedRenderSettingAnimations;
  std::unordered_map<int, SavedGlobalValue> _savedGlobalProperties;
  std::unordered_map<std::string, bool> _savedGlobalKeywords;

  std::unordered_map<int, SavedGlobalValue> _currentGlobalProperties;
  std::unordered_map<std::string, bool> _currentGlobalKeywords;
  std::unordered_set<UnityEngine::Material*> _repairedMaterials;
  std::unordered_map<UnityEngine::Renderer*, int> _overlayRendererSortingOrders;
  std::unordered_map<std::string, DeclaredTextureData> _declaredTextures;
  std::unordered_map<std::string, SecondaryCameraData> _secondaryCameras;
  std::unordered_map<std::string, CameraPropertyData> _cameraProperties;
  std::vector<ActiveBlitEffect> _preEffects;
  std::vector<ActiveBlitEffect> _postEffects;

  std::array<std::vector<ActiveBlitEffect>, kMidRenderOrderCount> _midEffects;

  std::vector<std::pair<int, SafePtr<UnityEngine::Rendering::CommandBuffer>>> _activeMidCommandBuffers;
  UnityEngine::Camera* _midCommandBufferCamera = nullptr;

  UnityEngine::RenderTextureDescriptor _cachedMainDescriptor{};

  int _cachedMainVrUsage = -1;
  bool _hasMainDescriptor = false;

  UnityEngine::RenderTexture* _midMainRT = nullptr;
  UnityEngine::RenderTexture* _midScratchRT = nullptr;

  std::vector<UnityEngine::RenderTexture*> _midSelfBlitTemps;
  std::vector<ActiveRenderSettingAnimation> _renderSettingAnimations;
  std::vector<SavedRenderSetting> _savedRenderSettings;
  std::vector<SavedRenderSetting> _currentRenderSettings;
  std::vector<AssignedPrefabInfo> _assignedPrefabs;
  std::vector<std::vector<AssignedPrefabInfo*>> _activeDebrisPrefabStack;
  std::vector<AssignedPrefabInfo*> _lastCutDebrisPrefabs;
  std::vector<ActiveSaberVisual> _activeSabers;
  std::unordered_map<GlobalNamespace::NoteController*, VisualReplacement> _noteReplacements;
  std::unordered_map<GlobalNamespace::SaberModelController*, VisualReplacement> _saberReplacements;
  std::unordered_map<GlobalNamespace::NoteDebris*, VisualReplacement> _debrisReplacements;
  CameraApplier* _cameraApplier = nullptr;
  UnityEngine::GameObject* _cameraApplierGO = nullptr;
  MultipassKeywordController* _multipassController = nullptr;

  UnityEngine::GameObject* _lastMainCameraGO = nullptr;
  bool _mainCameraPropsDirty = false;
  std::optional<int> _mainCamOriginalDepthMode;
  std::optional<int> _mainCamOriginalClearFlags;
  std::optional<UnityEngine::Color> _mainCamOriginalBackgroundColor;
  UnityEngine::RenderTexture* _mainBlitTexture = nullptr;
  UnityEngine::RenderTexture* _scratchBlitTexture = nullptr;
  int _cachedBlitWidth = 0;
  int _cachedBlitHeight = 0;
  int _cachedBlitFormat = 0;
  mutable std::unordered_map<UnityEngine::Material*, bool> _blitMaterialValidCache;
  std::unordered_set<CustomJSONData::CustomEventData*> _catchUpAppliedCustomEvents;
  float _lastSongTime = -1.0f;
  float _lastSyncSongTime = -1.0f;
  int _songTimeCacheFrame = -1;
  int _lastUpdateErrorFrame = -1000;
  int _lastSyncHeartbeatFrame = -1000;
  float _songTimeCache = 0.0f;
  bool _loggedUnityPlatformInfo = false;
  bool _pauseMenuActive = false;
  bool _isResetting = false;

  bool _reduceDebris = false;
  bool _reduceDebrisCached = false;
};
}
