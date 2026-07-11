#include "VivifyRuntimeInternal.hpp"
#include "VivifyComponents.hpp"
#include "UnityEngine/ParticleSystem.hpp"
#include "GlobalNamespace/PlayerDataModel.hpp"
#include "GlobalNamespace/PlayerData.hpp"
#include "GlobalNamespace/PlayerSpecificSettings.hpp"

namespace Vivify {

Runtime& Runtime::Instance() {
  static Runtime runtime;
  return runtime;
}

bool Runtime::IsAlive(UnityEngine::Object* object) const {
  return IsManagedAlive(object);
}

void Runtime::LateLoad() {
  auto cjdModInfo = CustomJSONData::modInfo.to_c();
  auto tracksModInfo = CModInfo{.id = "Tracks"};
  modloader_require_mod(&cjdModInfo, CMatchType::MatchType_IdOnly);
  modloader_require_mod(&tracksModInfo, CMatchType::MatchType_IdOnly);
  EnsureBehaviour();
  SongCore::API::Capabilities::RegisterCapability(kCapability);
  CustomJSONData::CustomEventCallbacks::AddCustomEventCallback(&Runtime::OnCustomEventStatic);
  SongCore::API::LevelSelect::GetLevelWasSelectedEvent() += [](SongCore::API::LevelSelect::LevelWasSelectedEventArgs const& event) {
    Runtime::Instance().HandleLevelSelected(event);
  };
}

void Runtime::EnsureBehaviour() {
  if (_behaviour != nullptr) {
    return;
  }
  auto* gameObject = UnityEngine::GameObject::New_ctor(u"VivifyRuntime");
  UnityEngine::Object::DontDestroyOnLoad(gameObject);
  _behaviour = gameObject->AddComponent<RuntimeBehaviour*>();
  RefreshCameraComponents(false);
}

void Runtime::OnBehaviourDestroyed(RuntimeBehaviour* behaviour) {
  if (_behaviour == behaviour) _behaviour = nullptr;
}

void Runtime::OnCustomEventStatic(GlobalNamespace::BeatmapCallbacksController* callbackController,
                                  CustomJSONData::CustomEventData* customEventData) {
  Runtime::Instance().HandleCustomEvent(callbackController, customEventData);
}

void Runtime::Update() {

  try {
    UpdateSyncedObjects();
    if (_audioTimeSyncController != nullptr && !UnityEngine::Object::op_Implicit_bool(_audioTimeSyncController)) {
      ResetRuntime();

      _activeSabers.clear();
      return;
    }
    if (_currentBeatmapData == nullptr) {
      RefreshCameraComponents(false);
      return;
    }
    UpdateMaterialAnimations();
    UpdateGlobalAnimations();
    UpdateAnimatorAnimations();
    UpdatePrefabAnimationSpeed();
    UpdateBlitEffects();
    UpdateRenderSettingAnimations();
    RefreshCameraComponents(true);
    DetectSongRestart();
    UpdateSaberReplacementColors();
  } catch (std::exception const& ex) {
    LogThrottledUpdateError(ex.what());
  } catch (...) {
    LogThrottledUpdateError("non-std exception");
  }
}

void Runtime::LogThrottledUpdateError(std::string_view what) {

  int const frame = static_cast<int>(UnityEngine::Time::get_frameCount());
  if (frame - _lastUpdateErrorFrame < 60) return;
  _lastUpdateErrorFrame = frame;
  PaperLogger.error("Vivify Update threw (frame {}, songTime {}): {} — repeats suppressed ~1s",
                    frame, CurrentSongTime(), std::string(what));
}

float Runtime::CurrentSongTime() {

  int const frame = static_cast<int>(UnityEngine::Time::get_frameCount());
  if (frame == _songTimeCacheFrame) return _songTimeCache;
  _songTimeCacheFrame = frame;
  if (!IsAlive(_audioTimeSyncController)) {
    _audioTimeSyncController = UnityEngine::Object::FindObjectOfType<GlobalNamespace::AudioTimeSyncController*>();
  }
  _songTimeCache = IsAlive(_audioTimeSyncController) ? _audioTimeSyncController->get_songTime() : 0.0f;
  return _songTimeCache;
}

void Runtime::DetectSongRestart() {
  if (_currentBeatmapData == nullptr || _isResetting) return;
  float songTime = CurrentSongTime();
  if (_lastSongTime >= 0.0f && songTime + 0.25f < _lastSongTime) {
    ResetRuntime();
    return;
  }
  _lastSongTime = songTime;
}

float Runtime::CurrentBpm() const {
  if (TracksStatic::bpmController) {
    return TracksStatic::bpmController->get_currentBpm();
  }
  return 0.0f;
}

float Runtime::DurationBeatsToSeconds(float durationBeats) const {
  float bpm = CurrentBpm();
  if (bpm <= 0.0f) {
    return 0.0f;
  }
  return (60.0f * durationBeats) / bpm;
}

void Runtime::RegisterSyncedObject(UnityEngine::GameObject* root, float startTime) {
  if (!IsAlive(root)) return;
  UnregisterSyncedObject(root);

  SyncedObject synced;
  synced.root = root;
  synced.startTime = startTime;

  auto videoPlayers = root->GetComponentsInChildren<UnityEngine::Video::VideoPlayer*>(true);
  for (int i = 0; i < videoPlayers.size(); i++) {
    auto* vp = videoPlayers[i];
    if (IsAlive(vp)) {

      synced.videoPlayers.emplace_back(vp);
    }
  }

  auto animators = root->GetComponentsInChildren<UnityEngine::Animator*>(true);
  for (int i = 0; i < animators.size(); i++) {
    if (IsAlive(animators[i])) synced.animators.emplace_back(animators[i]);
  }
  auto particleSystems = root->GetComponentsInChildren<UnityEngine::ParticleSystem*>(true);
  for (int i = 0; i < particleSystems.size(); i++) {
    if (IsAlive(particleSystems[i])) synced.particleSystems.emplace_back(particleSystems[i]);
  }

  if (!synced.videoPlayers.empty() || !synced.animators.empty() || !synced.particleSystems.empty()) {
    VIVIFY_DEBUG("Vivify sync: registered '{}' with {} video / {} animator / {} particle component(s), startTime={}",
                 IsAlive(root) ? ToStdString(root->get_name()) : std::string("?"),
                 synced.videoPlayers.size(), synced.animators.size(), synced.particleSystems.size(), startTime);
    _syncedObjects.emplace_back(std::move(synced));
  }
}

void Runtime::UpdatePrefabAnimationSpeed() {
  if (_syncedObjects.empty() && _livePrefabs.empty()) {

    _lastSyncSongTime = IsManagedAlive(_audioTimeSyncController) ? _audioTimeSyncController->get_songTime() : -1.0f;
    return;
  }

  float speed = 0.0f;
  if (!_pauseMenuActive && IsManagedAlive(_audioTimeSyncController)) {
    float const songTime = _audioTimeSyncController->get_songTime();
    float const deltaTime = UnityEngine::Time::get_deltaTime();
    float const deltaSong = songTime - _lastSyncSongTime;
    if (_lastSyncSongTime < 0.0f) {
      speed = 1.0f;
    } else if (deltaTime > 1e-5f && deltaSong > 0.0f) {
      speed = deltaSong / deltaTime;
    } else {
      speed = 0.0f;
    }
    _lastSyncSongTime = songTime;
  }

  auto drive = [speed](std::vector<UnityEngine::Animator*> const& animators,
                       std::vector<UnityEngine::ParticleSystem*> const& particleSystems) {
    for (auto* animator : animators) {
      if (IsManagedAlive(animator)) animator->set_speed(speed);
    }
    for (auto* particleSystem : particleSystems) {
      if (!IsManagedAlive(particleSystem)) continue;
      auto main = particleSystem->get_main();
      main.set_simulationSpeed(speed);
    }
  };
  for (auto& synced : _syncedObjects) drive(synced.animators, synced.particleSystems);
  for (auto& [id, prefab] : _livePrefabs) drive(prefab.animators, prefab.particleSystems);
}

void Runtime::UnregisterSyncedObject(UnityEngine::GameObject* root) {
  _syncedObjects.erase(std::remove_if(_syncedObjects.begin(), _syncedObjects.end(),
                                      [root](SyncedObject const& synced) { return synced.root == root; }),
                       _syncedObjects.end());
}

void Runtime::UpdateSyncedObjects() {
  if (_syncedObjects.empty()) return;
  _syncedObjects.erase(std::remove_if(_syncedObjects.begin(), _syncedObjects.end(),
                                      [](SyncedObject const& synced) { return !IsManagedAlive(synced.root); }),
                       _syncedObjects.end());
  if (_syncedObjects.empty()) return;

  bool const paused = _pauseMenuActive || !IsManagedAlive(_audioTimeSyncController);
  float const songTime = IsManagedAlive(_audioTimeSyncController) ? _audioTimeSyncController->get_songTime() : 0.0f;

  bool const heartbeat = [&]() {
    if (!GetVivifyDebugLogging()) return false;
    int const frame = static_cast<int>(UnityEngine::Time::get_frameCount());
    if (frame - _lastSyncHeartbeatFrame < 120) return false;
    _lastSyncHeartbeatFrame = frame;
    return true;
  }();

  for (auto& synced : _syncedObjects) {
    for (auto* vp : synced.videoPlayers) {
      if (!IsManagedAlive(vp)) continue;
      if (paused) {
        if (vp->get_isPlaying()) {
          VIVIFY_DEBUG("Vivify sync: pausing video (songTime={})", songTime);
          vp->Pause();
        }
        continue;
      }
      if (!vp->get_isPlaying()) {
        VIVIFY_DEBUG("Vivify sync: starting video (prepared={} songTime={})", vp->get_isPrepared(), songTime);
        vp->Play();
      }

      float const drift = static_cast<float>(vp->get_time()) - songTime;
      if (std::abs(drift) > 0.15f) {
        vp->set_time(static_cast<double>(songTime));
      }
      if (heartbeat) {
        PaperLogger.info("Vivify sync heartbeat: videoTime={} songTime={} drift={} prepared={} playing={}",
                         static_cast<float>(vp->get_time()), songTime, drift, vp->get_isPrepared(),
                         vp->get_isPlaying());
      }
    }
  }
}

bool Runtime::IsReduceDebrisEnabled() {

  if (!_reduceDebrisCached) {
    _reduceDebrisCached = true;
    _reduceDebris = false;
    auto* playerDataModel = UnityEngine::Object::FindObjectOfType<GlobalNamespace::PlayerDataModel*>();
    if (IsManagedAlive(playerDataModel)) {
      auto* playerData = playerDataModel->get_playerData();
      if (playerData != nullptr) {
        auto* settings = playerData->get_playerSpecificSettings();
        if (settings != nullptr) {
          _reduceDebris = settings->get_reduceDebris();
        }
      }
    }
    VIVIFY_DEBUG("Vivify: Reduce Debris player option = {} (custom debris {})", _reduceDebris,
                 _reduceDebris ? "disabled" : "enabled");
  }
  return _reduceDebris;
}

void Runtime::SetPauseMenuActive(bool active) {
  if (_currentBeatmapData == nullptr) {
    _pauseMenuActive = false;
    return;
  }
  if (_pauseMenuActive == active) return;
  _pauseMenuActive = active;
  VIVIFY_DEBUG("Vivify pause menu {}: syncedObjects={} secondaryCameras={}",
               active ? "opened" : "closed", _syncedObjects.size(), _secondaryCameras.size());
  if (active) {

    if (_cameraApplier != nullptr && UnityEngine::Object::op_Implicit_bool(_cameraApplier)) {
      _cameraApplier->set_enabled(false);
    }
    for (auto& [name, cam] : _secondaryCameras) {
      if (IsAlive(cam.camera)) cam.camera->set_enabled(false);
    }

    RestoreGlobalProperties();
    RestoreRenderSettings();

    _pausedMaterialAnimations = std::move(_materialAnimations);
    _pausedGlobalAnimations = std::move(_globalAnimations);
    _pausedAnimatorAnimations = std::move(_animatorAnimations);
    _pausedRenderSettingAnimations = std::move(_renderSettingAnimations);
    _materialAnimations.clear();
    _globalAnimations.clear();
    _animatorAnimations.clear();
    _renderSettingAnimations.clear();
    RestoreOverlayRenderState();
    for (auto& synced : _syncedObjects) {
      for (auto* vp : synced.videoPlayers) {
        if (IsManagedAlive(vp) && vp->get_isPlaying()) {
          vp->Pause();
        }
      }
    }
  } else {
    for (auto& [name, cam] : _secondaryCameras) {
      if (IsAlive(cam.camera)) cam.camera->set_enabled(true);
    }

    ReapplyCurrentGlobalProperties();
    ReapplyCurrentRenderSettings();
    _materialAnimations = std::move(_pausedMaterialAnimations);
    _globalAnimations = std::move(_pausedGlobalAnimations);
    _animatorAnimations = std::move(_pausedAnimatorAnimations);
    _renderSettingAnimations = std::move(_pausedRenderSettingAnimations);
    _pausedMaterialAnimations.clear();
    _pausedGlobalAnimations.clear();
    _pausedAnimatorAnimations.clear();
    _pausedRenderSettingAnimations.clear();

    if (!_isResetting) RefreshCameraComponents(true);
  }
}

void Runtime::RefreshMultipassRendering() {
  auto mainCam = UnityEngine::Camera::get_main();
  auto mainCamGO = IsAlive(mainCam.unsafePtr()) ? mainCam->get_gameObject().unsafePtr() : nullptr;
  RefreshMultipassRendering(mainCamGO);
  RefreshLoadedMaterialStereoKeywords();
}

void Runtime::RefreshIsolationSettings() {
  if (GetDisableAllBlits()) {
    if ((!_preEffects.empty() || !_postEffects.empty()) && GetVivifyDebugLogging()) {
      PaperLogger.info("Vivify isolation: clearing active blits pre={} post={}", _preEffects.size(), _postEffects.size());
    }
    _preEffects.clear();
    _postEffects.clear();
    for (auto& bucket : _midEffects) bucket.clear();
    ReleaseMidRenderTextures();
    ReleaseCachedBlitTextures();
    DestroyCameraApplier();
  } else if (GetDisableBeat0FilmgrainBlit()) {
    UpdateBlitEffects();
  }

  if (GetDisableCreateCameraDepth()) {
    if (!_secondaryCameras.empty() && GetVivifyDebugLogging()) {
      PaperLogger.info("Vivify isolation: releasing secondary cameras count={}", _secondaryCameras.size());
    }
    for (auto& [name, camera] : _secondaryCameras) {
      ReleaseSecondaryCameraData(camera);
    }
    _secondaryCameras.clear();
    for (auto it = _cameraProperties.begin(); it != _cameraProperties.end();) {
      if (it->first != kMainCameraId) {
        it = _cameraProperties.erase(it);
      } else {
        it++;
      }
    }
  }

  RefreshCameraComponents(_currentBeatmapData != nullptr && !_isResetting && !_pauseMenuActive);
}

void Runtime::ResetRuntime() {
  VIVIFY_DEBUG("Vivify ResetRuntime: livePrefabs={} preloaded={} synced={} secondaryCameras={} declaredTextures={} assets={}",
               _livePrefabs.size(), _instantiatePrefabs.size(), _syncedObjects.size(),
               _secondaryCameras.size(), _declaredTextures.size(), _assets.size());
  _isResetting = true;
  DestroyCameraApplier();
  RestoreGlobalProperties();
  RestoreAllVisualReplacements();
  RestoreOverlayRenderState();
  RestoreMainCameraOriginals();
  std::unordered_set<UnityEngine::GameObject*> destroyed;
  for (auto& [id, prefab] : _livePrefabs) {
    if (prefab.gameObject == nullptr) {
      continue;
    }
    for (auto const& track : prefab.tracks) {
      track.UnregisterGameObject(prefab.gameObject);
    }
    if (destroyed.emplace(prefab.gameObject).second) {
      if (UnityEngine::Object::op_Implicit_bool(prefab.gameObject)) {
        UnityEngine::Object::Destroy(prefab.gameObject);
      }
    }
  }
  for (auto& [eventData, instantiate] : _instantiatePrefabs) {
    if (instantiate.instance != nullptr && destroyed.emplace(instantiate.instance).second) {
      if (UnityEngine::Object::op_Implicit_bool(instantiate.instance)) {
        UnityEngine::Object::Destroy(instantiate.instance);
      }
    }
    instantiate.instance = nullptr;
  }
  _livePrefabs.clear();
  _instantiatePrefabs.clear();
  _syncedObjects.clear();
  _blitMaterialValidCache.clear();
  _cachedBlitWidth = 0;
  _cachedBlitHeight = 0;
  _cachedBlitFormat = 0;
  _materialAnimations.clear();
  _globalAnimations.clear();
  _animatorAnimations.clear();
  _pausedMaterialAnimations.clear();
  _pausedGlobalAnimations.clear();
  _pausedAnimatorAnimations.clear();
  _pausedRenderSettingAnimations.clear();
  _savedGlobalProperties.clear();
  _savedGlobalKeywords.clear();
  _currentGlobalProperties.clear();
  _currentGlobalKeywords.clear();
  _repairedMaterials.clear();
  _assets.clear();
  _assetsByName.clear();
  _supportedShadersByName.clear();
  _catchUpAppliedCustomEvents.clear();
  for (auto& [name, dt] : _declaredTextures) ReleaseDeclaredTextureData(dt);
  _declaredTextures.clear();
  for (auto& [name, cam] : _secondaryCameras) ReleaseSecondaryCameraData(cam);
  _secondaryCameras.clear();
  _preEffects.clear();
  _postEffects.clear();
  for (auto& bucket : _midEffects) bucket.clear();
  RemoveMidRenderCommandBuffers();
  ReleaseMidRenderTextures();
  _hasMainDescriptor = false;
  ReleaseCachedBlitTextures();
  RestoreRenderSettings();
  _renderSettingAnimations.clear();
  _savedRenderSettings.clear();
  _currentRenderSettings.clear();
  _assignedPrefabs.clear();
  _activeDebrisPrefabStack.clear();
  _lastCutDebrisPrefabs.clear();
  _cameraProperties.clear();
  _mainCameraPropsDirty = false;

  if (_mainBundle != nullptr && !UnityEngine::Object::op_Implicit_bool(_mainBundle)) {
    _mainBundle = nullptr;
    _preloadedBundlePath.clear();
  }
  _currentBeatmapData = nullptr;
  _beatmapAD = nullptr;
  _audioTimeSyncController = nullptr;
  _lastSongTime = -1.0f;
  _lastSyncSongTime = -1.0f;
  _songTimeCacheFrame = -1;
  _songTimeCache = 0.0f;
  _pauseMenuActive = false;
  _reduceDebrisCached = false;
  _reduceDebris = false;
  _isResetting = false;
}

}
