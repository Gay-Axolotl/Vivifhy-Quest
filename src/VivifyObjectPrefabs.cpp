#include "VivifyRuntimeInternal.hpp"
#include "VivifyComponents.hpp"
#include "GlobalNamespace/ColorNoteVisuals.hpp"

namespace Vivify {

std::vector<AssignedPrefabInfo*> Runtime::FindAssignedPrefabs(std::string_view objectType,
                                                              GlobalNamespace::NoteData* noteData,
                                                              AssignedPrefabKind kind) {
  std::vector<AssignedPrefabInfo*> result;
  for (auto& info : _assignedPrefabs) {
    if (info.objectType != objectType || info.kind != kind) continue;
    if (AssignmentMatchesTracks(info, noteData)) {
      result.emplace_back(&info);
    }
  }
  return result;
}

std::vector<AssignedPrefabInfo*> Runtime::FindAssignedSaberPrefabs(int type) {
  std::vector<AssignedPrefabInfo*> result;
  for (auto& info : _assignedPrefabs) {
    if (info.objectType == "saber" && info.kind == AssignedPrefabKind::Object &&
        info.saberType.has_value() && info.saberType.value() == type) {
      result.emplace_back(&info);
    }
  }
  return result;
}

std::vector<AssignedPrefabInfo*> Runtime::FindAssignedSaberTrailPrefabs(int type) {
  std::vector<AssignedPrefabInfo*> result;
  for (auto& info : _assignedPrefabs) {
    if (info.objectType == "saber" && info.kind == AssignedPrefabKind::Trail &&
        info.saberType.has_value() && info.saberType.value() == type) {
      result.emplace_back(&info);
    }
  }
  return result;
}

std::vector<AssignedPrefabInfo*> Runtime::FindAssignedDebrisPrefabs(GlobalNamespace::NoteData* noteData) {
  if (noteData == nullptr) return {};
  auto gameplayType = noteData->get_gameplayType();
  if (gameplayType == GlobalNamespace::NoteData_GameplayType::Normal) {
    return FindAssignedPrefabs("colorNotes", noteData, AssignedPrefabKind::Debris);
  }
  if (gameplayType == GlobalNamespace::NoteData_GameplayType::BurstSliderHead) {
    return FindAssignedPrefabs("burstSliders", noteData, AssignedPrefabKind::Debris);
  }
  if (gameplayType == GlobalNamespace::NoteData_GameplayType::BurstSliderElement) {
    return FindAssignedPrefabs("burstSliderElements", noteData, AssignedPrefabKind::Debris);
  }
  return {};
}

bool Runtime::AssignmentMatchesTracks(AssignedPrefabInfo const& info, GlobalNamespace::NoteData* noteData) {
  if (info.tracks.empty()) return true;
  if (noteData == nullptr) return false;
  auto* customNoteData = il2cpp_utils::try_cast<CustomJSONData::CustomNoteData>(noteData).value_or(nullptr);
  if (customNoteData == nullptr || customNoteData->customData == nullptr) return false;
  auto& ad = TracksAD::getAD(customNoteData->customData);

  auto matches = [&](auto const& noteTracks) {
    for (auto const& noteTrack : noteTracks) {
      for (auto const& assigned : info.tracks) {
        if (assigned == noteTrack) return true;
      }
    }
    return false;
  };
  if (matches(ad.tracks)) return true;

  if (ad.tracks.empty() && customNoteData->customData->value.has_value()) {
    bool const v2 = _currentBeatmapData != nullptr && _currentBeatmapData->v2orEarlier;
    auto parsedTracks = ReadTracks(customNoteData->customData->value.value().get(), v2);
    if (matches(parsedTracks)) return true;
  }
  return false;
}

void Runtime::AddAssignedPrefab(std::string_view objectType, AssignedPrefabKind kind, std::string asset,
                                std::vector<TrackW> tracks, bool additive, std::optional<int> saberType) {
  if (asset.empty()) return;

  AssignedPrefabInfo info;
  info.asset = std::move(asset);
  info.tracks = std::move(tracks);
  info.objectType = std::string(objectType);
  info.saberType = saberType;
  info.kind = kind;
  info.additive = additive;
  VIVIFY_DEBUG("Vivify AssignObjectPrefab: objectType='{}' kind={} asset='{}' tracks={} additive={} saberType={}",
               std::string(objectType), static_cast<int>(kind), info.asset, info.tracks.size(), additive,
               saberType.has_value() ? std::to_string(*saberType) : std::string("-"));
  _assignedPrefabs.push_back(std::move(info));
}

void Runtime::AddAssignedTrail(std::string asset, bool additive, int saberType,
                               std::optional<UnityEngine::Vector3> topPos,
                               std::optional<UnityEngine::Vector3> bottomPos,
                               std::optional<float> duration, std::optional<int> samplingFrequency,
                               std::optional<int> granularity) {
  if (asset.empty()) return;
  AssignedPrefabInfo info;
  info.asset = std::move(asset);
  info.objectType = "saber";
  info.saberType = saberType;
  info.kind = AssignedPrefabKind::Trail;
  info.additive = additive;
  info.trailTopPos = topPos;
  info.trailBottomPos = bottomPos;
  info.trailDuration = duration;
  info.trailSamplingFrequency = samplingFrequency;
  info.trailGranularity = granularity;
  VIVIFY_DEBUG("Vivify AssignObjectPrefab(trail): asset='{}' saberType={} additive={}", info.asset, saberType, additive);
  _assignedPrefabs.push_back(std::move(info));
}

bool Runtime::TracksOverlap(std::vector<TrackW> const& left, std::vector<TrackW> const& right) const {
  if (left.empty() || right.empty()) return false;
  for (auto const& leftTrack : left) {
    for (auto const& rightTrack : right) {
      if (leftTrack == rightTrack) return true;
    }
  }
  return false;
}

void Runtime::ClearAssignedPrefabs(std::string_view objectType, std::optional<AssignedPrefabKind> kind,
                                   std::optional<int> saberType, std::vector<TrackW> const* tracks) {
  _assignedPrefabs.erase(std::remove_if(_assignedPrefabs.begin(), _assignedPrefabs.end(),
      [this, objectType, kind, saberType, tracks](AssignedPrefabInfo const& info) {
        if (info.objectType != objectType) return false;
        if (kind.has_value() && info.kind != kind.value()) return false;
        if (saberType.has_value() && (!info.saberType.has_value() || info.saberType.value() != saberType.value())) return false;
        if (tracks != nullptr && !tracks->empty() && !info.tracks.empty() && !TracksOverlap(info.tracks, *tracks)) {
          return false;
        }
        return true;
      }),
      _assignedPrefabs.end());
}

std::vector<AssignedPrefabInfo*> Runtime::GetValidPrefabInfos(std::vector<AssignedPrefabInfo*> const& infos) {
  std::vector<AssignedPrefabInfo*> result;
  result.reserve(infos.size());
  for (auto* info : infos) {
    if (info == nullptr) continue;
    bool valid = false;
    if (info->kind == AssignedPrefabKind::Trail) {
      auto* material = GetAssetAs<UnityEngine::Material>(info->asset);
      valid = IsManagedAlive(material);
    } else {
      auto* prefab = GetAssetAs<UnityEngine::GameObject>(info->asset);
      valid = IsManagedAlive(prefab);
    }
    if (valid) {
      result.emplace_back(info);
    }
  }
  return result;
}

bool Runtime::ShouldHideOriginal(std::vector<AssignedPrefabInfo*> const& infos) const {
  return std::any_of(infos.begin(), infos.end(), [](AssignedPrefabInfo const* info) {
    return info != nullptr && !info->additive;
  });
}

void Runtime::HandleAssignObjectPrefab(CustomJSONData::CustomEventData*, rapidjson::Value const& json) {
  bool const v2 = _currentBeatmapData != nullptr && _currentBeatmapData->v2orEarlier;
  bool const additive = [&json]() {
    auto loadMode = ReadStringView(json, "loadMode");
    return loadMode.has_value() && NormalizeAssetKey(*loadMode) == "additive";
  }();

  auto applyAsset = [this, additive](rapidjson::Value const& objVal,
                                     std::string_view objType,
                                     AssignedPrefabKind kind,
                                     std::string_view key,
                                     std::vector<TrackW> tracks,
                                     std::optional<int> saberType = std::nullopt) {
    auto asset = ReadAssetValue(objVal, key);
    if (asset.state == AssetValueState::Missing) return;
    if (asset.state == AssetValueState::Null) {
      ClearAssignedPrefabs(objType, kind, saberType, &tracks);
      return;
    }
    if (!additive) {
      ClearAssignedPrefabs(objType, kind, saberType, &tracks);
    }
    AddAssignedPrefab(objType, kind, std::move(asset.value), std::move(tracks), additive, saberType);
  };
  auto applyTrail = [this, additive](rapidjson::Value const& objVal, int saberType) {
    auto asset = ReadAssetValue(objVal, "trailAsset");
    if (asset.state == AssetValueState::Missing) return;
    if (asset.state == AssetValueState::Null) {
      ClearAssignedPrefabs("saber", AssignedPrefabKind::Trail, saberType);
      return;
    }
    if (!additive) {
      ClearAssignedPrefabs("saber", AssignedPrefabKind::Trail, saberType);
    }
    AddAssignedTrail(std::move(asset.value),
                     additive,
                     saberType,
                     ReadVector3(objVal, "trailTopPos"),
                     ReadVector3(objVal, "trailBottomPos"),
                     ReadFloat(objVal, "trailDuration"),
                     ReadInt(objVal, "trailSamplingFrequency"),
                     ReadInt(objVal, "trailGranularity"));
  };

  bool notesChanged = false;
  auto processTrackedObject = [&](std::string_view objType) {
    auto* objVal = ReadValuePtr(json, objType);
    if (objVal == nullptr || !objVal->IsObject()) return;
    notesChanged = true;
    auto tracks = ReadTracks(*objVal, v2);
    applyAsset(*objVal, objType, AssignedPrefabKind::Object, "asset", tracks);
    applyAsset(*objVal, objType, AssignedPrefabKind::Debris, "debrisAsset", tracks);
  };

  if (auto* colorNotes = ReadValuePtr(json, "colorNotes"); colorNotes != nullptr && colorNotes->IsObject()) {
    notesChanged = true;
    auto tracks = ReadTracks(*colorNotes, v2);
    applyAsset(*colorNotes, "colorNotes", AssignedPrefabKind::Object, "asset", tracks);
    applyAsset(*colorNotes, "colorNotes", AssignedPrefabKind::AnyDirectionObject, "anyDirectionAsset", tracks);
    applyAsset(*colorNotes, "colorNotes", AssignedPrefabKind::Debris, "debrisAsset", tracks);
  }
  processTrackedObject("bombNotes");
  processTrackedObject("burstSliders");
  processTrackedObject("burstSliderElements");

  bool saberChanged = false;
  if (auto* saber = ReadValuePtr(json, "saber"); saber != nullptr && saber->IsObject()) {
    auto type = ReadStringView(*saber, "type");
    std::vector<int> saberTypes;
    std::string normalizedType = type.has_value() ? NormalizeAssetKey(*type) : "both";
    if (normalizedType == "both") {
      saberTypes = {0, 1};
    } else if (normalizedType == "left" || normalizedType == "sabera") {
      saberTypes = {0};
    } else if (normalizedType == "right" || normalizedType == "saberb") {
      saberTypes = {1};
    }

    for (int saberType : saberTypes) {
      applyAsset(*saber, "saber", AssignedPrefabKind::Object, "asset", {}, saberType);
      applyTrail(*saber, saberType);
      saberChanged = true;
    }
  }
  if (saberChanged) {
    ApplySaberVisualsToActive();
  }

  if (notesChanged) {
    RefreshActiveNoteVisuals();
  }
}

std::string Runtime::ComputePrefabFingerprint(std::vector<AssignedPrefabInfo*> const& infos) const {
  std::string fingerprint;
  for (auto const* info : infos) {
    if (info == nullptr) continue;
    fingerprint += info->asset;
    fingerprint += '\x1f';
    fingerprint += info->additive ? '1' : '0';
    fingerprint += static_cast<char>('0' + static_cast<int>(info->kind));
    fingerprint += '\x1e';
  }
  return fingerprint;
}

std::string Runtime::ComputeSaberFingerprint(std::vector<AssignedPrefabInfo*> const& modelInfos,
                                             std::vector<AssignedPrefabInfo*> const& trailInfos) const {
  std::string fingerprint = ComputePrefabFingerprint(modelInfos);
  fingerprint += '\x1d';
  auto formatVector = [](std::optional<UnityEngine::Vector3> const& value) {
    return value.has_value() ? fmt::format("{:.4f},{:.4f},{:.4f}", value->x, value->y, value->z)
                             : std::string("-");
  };
  for (auto const* info : trailInfos) {
    if (info == nullptr) continue;
    fingerprint += info->asset;
    fingerprint += '\x1f';
    fingerprint += info->additive ? '1' : '0';
    fingerprint += formatVector(info->trailTopPos);
    fingerprint += '|';
    fingerprint += formatVector(info->trailBottomPos);
    fingerprint += '|';
    fingerprint += info->trailDuration.has_value() ? fmt::format("{:.4f}", *info->trailDuration) : std::string("-");
    fingerprint += '|';
    fingerprint += info->trailSamplingFrequency.has_value() ? std::to_string(*info->trailSamplingFrequency)
                                                            : std::string("-");
    fingerprint += '|';
    fingerprint += info->trailGranularity.has_value() ? std::to_string(*info->trailGranularity) : std::string("-");
    fingerprint += '\x1e';
  }
  return fingerprint;
}

bool Runtime::ReplacementIntact(VisualReplacement const& replacement) const {
  for (auto* spawned : replacement.spawnedObjects) {
    if (!IsAlive(spawned)) return false;
  }
  for (auto* followedTrail : replacement.followedTrails) {
    if (!IsAlive(followedTrail) || !IsAlive(followedTrail->____trailRenderer.unsafePtr())) return false;
  }
  return true;
}

void Runtime::ReassertNoteReplacement(GlobalNamespace::NoteController* noteController,
                                      VisualReplacement& replacement) {

  if (!replacement.hideOriginal || !IsAlive(noteController)) return;
  std::unordered_set<UnityEngine::Renderer*> ours(replacement.replacementRenderers.begin(),
                                                  replacement.replacementRenderers.end());
  std::unordered_set<UnityEngine::Renderer*> recorded(replacement.disabledRenderers.begin(),
                                                      replacement.disabledRenderers.end());
  auto renderers = noteController->GetComponentsInChildren<UnityEngine::Renderer*>(true);
  for (int i = 0; i < renderers.size(); i++) {
    auto* renderer = renderers[i];
    if (!IsAlive(renderer) || !renderer->get_enabled() || ours.contains(renderer)) continue;
    renderer->set_enabled(false);
    if (!recorded.contains(renderer)) {
      replacement.disabledRenderers.emplace_back(renderer);
    }
  }
}

void Runtime::ApplyNotePrefabFor(GlobalNamespace::NoteController* noteController) {
  if (!IsAlive(noteController) || _currentBeatmapData == nullptr || _isResetting) return;
  auto* noteData = noteController->get_noteData();
  if (noteData == nullptr) {
    RestoreNoteVisuals(noteController);
    return;
  }
  auto gameplayType = noteData->get_gameplayType();
  std::string_view objectType;
  AssignedPrefabKind kind = AssignedPrefabKind::Object;
  if (gameplayType == GlobalNamespace::NoteData_GameplayType::Normal) {
    objectType = "colorNotes";
    kind = noteData->get_cutDirection().value__ == GlobalNamespace::NoteCutDirection::Any.value__
               ? AssignedPrefabKind::AnyDirectionObject
               : AssignedPrefabKind::Object;
  } else if (gameplayType == GlobalNamespace::NoteData_GameplayType::Bomb) {
    objectType = "bombNotes";
  } else if (gameplayType == GlobalNamespace::NoteData_GameplayType::BurstSliderHead) {
    objectType = "burstSliders";
  } else if (gameplayType == GlobalNamespace::NoteData_GameplayType::BurstSliderElement) {
    objectType = "burstSliderElements";
  } else {
    RestoreNoteVisuals(noteController);
    return;
  }
  auto infos = FindAssignedPrefabs(objectType, noteData, kind);
  if (infos.empty()) {

    RestoreNoteVisuals(noteController);
    return;
  }
  auto existing = _noteReplacements.find(noteController);
  if (existing != _noteReplacements.end() &&
      existing->second.appliedFingerprint == ComputePrefabFingerprint(infos) &&
      ReplacementIntact(existing->second)) {
    ReassertNoteReplacement(noteController, existing->second);
    return;
  }
  ReplaceNoteVisuals(noteController, infos);
}

void Runtime::RefreshActiveNoteVisuals() {
  if (_currentBeatmapData == nullptr || _isResetting || _noteReplacements.empty()) return;

  std::vector<GlobalNamespace::NoteController*> active;
  active.reserve(_noteReplacements.size());
  for (auto const& entry : _noteReplacements) {
    if (IsAlive(entry.first)) active.push_back(entry.first);
  }
  for (auto* noteController : active) {
    ApplyNotePrefabFor(noteController);
  }
}

void Runtime::CleanCustomObject(UnityEngine::GameObject* go) {
  if (!IsAlive(go)) return;

  auto rigidbodies = go->GetComponentsInChildren<UnityEngine::Rigidbody*>(true);
  for (int i = 0; i < rigidbodies.size(); i++) {
    rigidbodies[i]->set_isKinematic(true);
    rigidbodies[i]->set_useGravity(false);
  }
  auto colliders = go->GetComponentsInChildren<UnityEngine::Collider*>(true);
  for (int i = 0; i < colliders.size(); i++) {
    colliders[i]->set_enabled(false);
  }
}

void Runtime::PreloadInstantiatePrefabs() {
  if (_currentBeatmapData == nullptr) {
    return;
  }
  bool const v2 = _currentBeatmapData->v2orEarlier;

  std::unordered_set<std::string> loadedIds;
  for (auto* customEventData : _currentBeatmapData->customEventDatas) {
    if (customEventData == nullptr) continue;
    if (customEventData->type == kDestroyObjectEvent) {
      if (auto* json = GetEventJson(customEventData); json != nullptr) {
        for (auto const& id : ReadStringListOrSingle(*json, "id")) {
          loadedIds.erase(id);
        }
      }
      continue;
    }
    if (customEventData->type != kInstantiatePrefabEvent) {
      continue;
    }
    auto* json = GetEventJson(customEventData);
    if (json == nullptr) {
      continue;
    }
    auto asset = ReadStringView(*json, "asset");
    if (!asset.has_value()) {
      continue;
    }
    InstantiatePrefabData data;
    data.asset = std::string(*asset);
    if (auto id = ReadStringView(*json, "id"); id.has_value()) {
      if (!loadedIds.emplace(std::string(*id)).second) {
        PaperLogger.warn("Vivify InstantiatePrefab: id '{}' already loaded at time {} — event dropped (PC parity)",
                         std::string(*id), customEventData->time);
        continue;
      }
      data.id = std::string(*id);
    }
    data.transformData = Tracks::TransformData(*json, v2);
    data.tracks = ReadTracks(*json, v2);
    bool prefabFound = false;
    if (auto* prefab = GetAssetAs<UnityEngine::GameObject>(data.asset); IsAlive(prefab)) {
      prefabFound = true;
      auto* instance = UnityEngine::Object::Instantiate(prefab);
      if (IsAlive(instance)) {
        instance->SetActive(false);
        data.instance = instance;
      }
    }
    if (!prefabFound) {
      PaperLogger.warn("Vivify PreloadInstantiatePrefabs: asset '{}' (id '{}') not found in bundle — "
                       "this InstantiatePrefab will do nothing",
                       data.asset, data.id.value_or("<none>"));
    } else {
      VIVIFY_DEBUG("Vivify preload prefab: asset='{}' id='{}' instance={}",
                   data.asset, data.id.value_or("<none>"), data.instance != nullptr);
    }
    _instantiatePrefabs.emplace(customEventData, std::move(data));
  }
  VIVIFY_DEBUG("Vivify PreloadInstantiatePrefabs: {} prefab event(s) preloaded", _instantiatePrefabs.size());
}

std::string Runtime::GetPrefabStorageId(CustomJSONData::CustomEventData* customEventData,
                                        InstantiatePrefabData const& data) const {
  if (data.id.has_value()) {
    return *data.id;
  }
  return "vivify_prefab_" + std::to_string(reinterpret_cast<std::uintptr_t>(customEventData));
}

void Runtime::InstantiatePrefab(CustomJSONData::CustomEventData* customEventData, rapidjson::Value const&) {
  auto it = _instantiatePrefabs.find(customEventData);
  if (it == _instantiatePrefabs.end()) {
    PaperLogger.warn("Vivify InstantiatePrefab: event not found in preload table (was the bundle loaded?)");
    return;
  }
  auto& data = it->second;
  std::string storageId = GetPrefabStorageId(customEventData, data);
  VIVIFY_DEBUG("Vivify InstantiatePrefab: asset='{}' id='{}' alreadyLive={}",
               data.asset, storageId, _livePrefabs.contains(storageId));
  if (_livePrefabs.contains(storageId)) {

    DestroyPrefabById(storageId);
  }
  if (!IsAlive(data.instance)) {
    auto* prefab = GetAssetAs<UnityEngine::GameObject>(data.asset);
    if (!IsAlive(prefab)) {
      PaperLogger.warn("Vivify InstantiatePrefab: asset '{}' (id '{}') missing at spawn time — nothing spawned",
                       data.asset, storageId);
      return;
    }
    data.instance = UnityEngine::Object::Instantiate(prefab);
    if (!IsAlive(data.instance)) {
      data.instance = nullptr;
      PaperLogger.warn("Vivify InstantiatePrefab: Instantiate of '{}' returned null", data.asset);
      return;
    }
    data.instance->SetActive(false);
  }
  auto* instance = data.instance;
  instance->SetActive(true);
  auto transform = instance->get_transform();
  bool const v2 = _currentBeatmapData != nullptr && _currentBeatmapData->v2orEarlier;
  data.transformData.Apply(transform, false, v2);
  if (!data.tracks.empty()) {
    for (auto const& track : data.tracks) {
      track.RegisterGameObject(instance);
    }
    auto tracksSpan = std::span<TrackW const>(data.tracks.data(), data.tracks.size());
    Tracks::GameObjectTrackController::HandleTrackData(
        instance,
        tracksSpan,
        GlobalNamespace::StaticBeatmapObjectSpawnMovementData::kNoteLinesDistance,
        v2,
        false);
  }
  auto animatorArray = instance->GetComponentsInChildren<UnityEngine::Animator*>(true);
  std::vector<UnityEngine::Animator*> animators;
  animators.reserve(animatorArray.size());
  for (auto animator : animatorArray) {
    if (animator != nullptr) {
      animators.emplace_back(animator);
    }
  }

  auto particleArray = instance->GetComponentsInChildren<UnityEngine::ParticleSystem*>(true);
  std::vector<UnityEngine::ParticleSystem*> particleSystems;
  particleSystems.reserve(particleArray.size());
  for (auto particleSystem : particleArray) {
    if (particleSystem != nullptr) {
      particleSystems.emplace_back(particleSystem);
    }
  }

  VIVIFY_DEBUG("Vivify InstantiatePrefab spawned: id='{}' asset='{}' tracks={} animators={}",
               storageId, data.asset, data.tracks.size(), animators.size());
  _livePrefabs[storageId] = LivePrefab{
      .gameObject = instance,
      .tracks = data.tracks,
      .animators = std::move(animators),
      .particleSystems = std::move(particleSystems),
  };
}

bool Runtime::DestroyPrefabById(std::string const& id) {
  auto it = _livePrefabs.find(id);
  if (it == _livePrefabs.end()) {
    return false;
  }
  VIVIFY_DEBUG("Vivify DestroyObject: destroying live prefab id='{}'", id);
  auto prefab = std::move(it->second);
  _livePrefabs.erase(it);
  if (prefab.gameObject != nullptr) {
    UnregisterSyncedObject(prefab.gameObject);
    for (auto const& track : prefab.tracks) {
      track.UnregisterGameObject(prefab.gameObject);
    }
    UnityEngine::Object::Destroy(prefab.gameObject);
  }
  return true;
}

void Runtime::DestroyObjects(rapidjson::Value const& json) {
  for (auto const& id : ReadStringListOrSingle(json, "id")) {

    if (DestroySecondaryCameraById(id)) continue;
    if (DestroyDeclaredTextureById(id)) continue;
    if (DestroyPrefabById(id)) continue;
    if (GetVivifyDebugLogging()) {
      PaperLogger.warn("Vivify DestroyObject: could not find '{}'", id);
    }
  }
}

void Runtime::PushActiveDebrisPrefabs(std::vector<AssignedPrefabInfo*> infos) {
  _lastCutDebrisPrefabs = infos;
  _activeDebrisPrefabStack.emplace_back(std::move(infos));
}

void Runtime::PopActiveDebrisPrefabs() {
  if (!_activeDebrisPrefabStack.empty()) {
    _activeDebrisPrefabStack.pop_back();
  }

}

void Runtime::RestoreNoteVisuals(GlobalNamespace::NoteController* noteController) {
  auto it = _noteReplacements.find(noteController);
  if (it == _noteReplacements.end()) return;
  RestoreReplacementData(it->second);
  _noteReplacements.erase(it);
}

void Runtime::RestoreDebrisVisuals(GlobalNamespace::NoteDebris* debris) {
  auto it = _debrisReplacements.find(debris);
  if (it == _debrisReplacements.end()) return;
  RestoreReplacementData(it->second);
  _debrisReplacements.erase(it);
}

void Runtime::RestoreSaberVisuals(GlobalNamespace::SaberModelController* smc) {
  auto it = _saberReplacements.find(smc);
  if (it == _saberReplacements.end()) return;
  RestoreReplacementData(it->second);
  _saberReplacements.erase(it);
}

void Runtime::DisableOriginalRenderers(UnityEngine::GameObject* gameObject, VisualReplacement& replacement) {
  if (!IsAlive(gameObject)) return;
  auto renderers = gameObject->GetComponentsInChildren<UnityEngine::Renderer*>(true);
  for (int i = 0; i < renderers.size(); i++) {
    auto* renderer = renderers[i];
    if (IsAlive(renderer) && renderer->get_enabled()) {
      renderer->set_enabled(false);
      replacement.disabledRenderers.emplace_back(renderer);
    }
  }
}

void Runtime::DisableOriginalRenderers(ArrayW<UnityEngine::Renderer*, Array<UnityEngine::Renderer*>*> const& renderers,
                                       VisualReplacement& replacement) {
  if (!renderers) return;
  for (int i = 0; i < renderers.size(); i++) {
    auto* renderer = renderers[i];
    if (IsAlive(renderer) && renderer->get_enabled()) {
      renderer->set_enabled(false);
      replacement.disabledRenderers.emplace_back(renderer);
    }
  }
}

UnityEngine::Transform* Runtime::GetReplacementParent(GlobalNamespace::NoteController* noteController) {
  if (!IsAlive(noteController)) return nullptr;

  auto* noteTransform = noteController->____noteTransform.unsafePtr();
  if (IsAlive(noteTransform)) return noteTransform;

  return noteController->get_transform().unsafePtr();
}

GlobalNamespace::MaterialPropertyBlockController* Runtime::GetReplacementMaterialPropertyBlockController(
    GlobalNamespace::NoteController* noteController, UnityEngine::Transform* replacementParent) {
  if (!IsAlive(noteController)) return nullptr;
  if (IsAlive(replacementParent)) {
    auto parentObject = replacementParent->get_gameObject();
    if (IsAlive(parentObject.unsafePtr())) {
      auto* childMpb = parentObject->GetComponent<GlobalNamespace::MaterialPropertyBlockController*>();
      if (IsAlive(childMpb)) return childMpb;
    }
  }
  auto* controllerMpb = noteController->GetComponent<GlobalNamespace::MaterialPropertyBlockController*>();
  if (IsAlive(controllerMpb)) return controllerMpb;
  return noteController->GetComponentInChildren<GlobalNamespace::MaterialPropertyBlockController*>(true);
}

void Runtime::RestoreReplacementData(VisualReplacement& replacement) {
  if (replacement.hasOriginalMaterialBlockRenderers && IsAlive(replacement.materialPropertyBlockController)) {
    if (replacement.originalMaterialBlockRenderers) {
      std::vector<UnityEngine::Renderer*> aliveRenderers;
      aliveRenderers.reserve(replacement.originalMaterialBlockRenderers.size());
      for (int i = 0; i < replacement.originalMaterialBlockRenderers.size(); i++) {
        auto* renderer = replacement.originalMaterialBlockRenderers[i].unsafePtr();
        if (IsAlive(renderer)) {
          aliveRenderers.emplace_back(renderer);
        }
      }
      auto restoredRenderers = ArrayW<UnityW<UnityEngine::Renderer>>(aliveRenderers.size());
      for (size_t i = 0; i < aliveRenderers.size(); i++) {
        restoredRenderers[i] = aliveRenderers[i];
      }
      replacement.materialPropertyBlockController->____renderers = restoredRenderers;
    } else {
      replacement.materialPropertyBlockController->____renderers =
          ArrayW<UnityW<UnityEngine::Renderer>>(static_cast<il2cpp_array_size_t>(0));
    }
    replacement.materialPropertyBlockController->ApplyChanges();
  }
  for (auto* followedTrail : replacement.followedTrails) {
    if (IsAlive(followedTrail)) {
      followedTrail->Cleanup();
    }
  }
  for (auto* renderer : replacement.disabledRenderers) {
    if (IsAlive(renderer)) {
      renderer->set_enabled(true);
    }
  }
  for (auto* spawned : replacement.spawnedObjects) {
    if (IsAlive(spawned)) {
      UnregisterSyncedObject(spawned);
      UnityEngine::Object::Destroy(spawned);
    }
  }
  replacement.disabledRenderers.clear();
  replacement.spawnedObjects.clear();
  replacement.replacementRenderers.clear();
  replacement.followedTrails.clear();
  replacement.materialPropertyBlockController = nullptr;
  replacement.originalMaterialBlockRenderers = nullptr;
  replacement.hasOriginalMaterialBlockRenderers = false;
  replacement.hasLastSaberColor = false;
  replacement.appliedFingerprint.clear();
  replacement.hideOriginal = false;
}

void Runtime::CacheReplacementRenderers(UnityEngine::GameObject* spawned, VisualReplacement& replacement) {
  if (!IsAlive(spawned)) return;
  auto renderers = spawned->GetComponentsInChildren<UnityEngine::Renderer*>(true);
  replacement.replacementRenderers.reserve(replacement.replacementRenderers.size() + renderers.size());
  for (int i = 0; i < renderers.size(); i++) {
    auto* renderer = renderers[i];
    if (IsAlive(renderer)) {

      replacement.replacementRenderers.emplace_back(renderer);
    }
  }
}

void Runtime::InstantiateReplacementPrefab(AssignedPrefabInfo const& info,
                                           UnityEngine::Transform* parent,
                                           VisualReplacement& replacement) {
  if (!IsAlive(parent)) return;
  auto* prefab = GetAssetAs<UnityEngine::GameObject>(info.asset);
  if (!IsManagedAlive(prefab)) return;
  auto* spawned = UnityEngine::Object::Instantiate(prefab);
  if (!IsAlive(spawned)) return;
  CleanCustomObject(spawned);
  RepairGameObjectMaterials(spawned, info.asset);
  spawned->get_transform()->SetParent(parent, false);

  auto animators = spawned->GetComponentsInChildren<UnityEngine::Animator*>(true);
  for (int i = 0; i < animators.size(); i++) {
    auto* animator = animators[i];
    if (!IsAlive(animator)) continue;
    animator->Rebind();
    animator->Update(0.01f);
  }

  RegisterSyncedObject(spawned, CurrentSongTime());
  replacement.spawnedObjects.emplace_back(spawned);
  CacheReplacementRenderers(spawned, replacement);
}

UnityEngine::Color Runtime::GetSaberColor(GlobalNamespace::SaberModelController* smc, GlobalNamespace::Saber* saber) {
  if (IsAlive(smc) && smc->____colorManager != nullptr) {
    return smc->____colorManager->ColorForSaberType(saber->get_saberType());
  }
  return saber != nullptr && saber->get_saberType().value__ == GlobalNamespace::SaberType::SaberA.value__
      ? UnityEngine::Color(1.0f, 0.0f, 0.0f, 1.0f)
      : UnityEngine::Color(0.0f, 0.55f, 1.0f, 1.0f);
}

UnityEngine::Color Runtime::GetNoteColor(GlobalNamespace::NoteController* noteController) {
  if (IsAlive(noteController)) {
    auto* noteData = noteController->get_noteData();
    if (noteData != nullptr) {
      auto* visuals = noteController->GetComponentInChildren<GlobalNamespace::ColorNoteVisuals*>();
      if (IsAlive(visuals) && visuals->____colorManager != nullptr) {
        return visuals->____colorManager->ColorForType(noteData->get_colorType());
      }
      return noteData->get_colorType().value__ == GlobalNamespace::ColorType::ColorA.value__
          ? UnityEngine::Color(1.0f, 0.0f, 0.0f, 1.0f)
          : UnityEngine::Color(0.0f, 0.55f, 1.0f, 1.0f);
    }
  }
  return UnityEngine::Color(1.0f, 1.0f, 1.0f, 1.0f);
}

void Runtime::ApplyColorToRenderers(std::vector<UnityEngine::Renderer*> const& renderers, UnityEngine::Color color) {
  if (renderers.empty()) return;

  auto* block = UnityEngine::MaterialPropertyBlock::New_ctor();
  if (block == nullptr) return;
  block->SetColor(ColorPropertyId(), color);
  for (auto* renderer : renderers) {
    if (IsAlive(renderer)) {
      renderer->SetPropertyBlock(block);
    }
  }
}

void Runtime::ApplySaberReplacementColor(GlobalNamespace::SaberModelController* smc,
                                         GlobalNamespace::Saber* saber,
                                         VisualReplacement& replacement,
                                         bool force) {
  if (!IsAlive(smc) || !IsAlive(saber) || replacement.spawnedObjects.empty()) return;
  auto color = GetSaberColor(smc, saber);
  if (!force && replacement.hasLastSaberColor && NearlySameColor(color, replacement.lastSaberColor)) {
    return;
  }
  ApplyColorToRenderers(replacement.replacementRenderers, color);
  replacement.lastSaberColor = color;
  replacement.hasLastSaberColor = true;
}

void Runtime::UpdateSaberReplacementColors() {
  if (_saberReplacements.empty() || _currentBeatmapData == nullptr || _isResetting) return;
  PurgeInvalidActiveSabers();
  for (auto const& target : _activeSabers) {
    auto it = _saberReplacements.find(target.controller);
    if (it != _saberReplacements.end()) {
      ApplySaberReplacementColor(target.controller, target.saber, it->second);
    }
  }
}

void Runtime::ReplaceNoteVisuals(GlobalNamespace::NoteController* noteController,
                                 std::vector<AssignedPrefabInfo*> const& infos) {
  RestoreNoteVisuals(noteController);
  if (!IsAlive(noteController) || infos.empty()) return;
  UnityEngine::Transform* replacementParent = GetReplacementParent(noteController);
  if (!IsAlive(replacementParent)) return;
  std::vector<AssignedPrefabInfo*> validInfos = GetValidPrefabInfos(infos);
  if (validInfos.empty()) {

    VIVIFY_DEBUG("Vivify note replace: {} assignment(s) matched but no valid prefab asset loaded "
                 "(note shows default)", infos.size());
    return;
  }

  VisualReplacement replacement;
  bool const hideOriginal = ShouldHideOriginal(validInfos);
  auto originalRenderers = noteController->GetComponentsInChildren<UnityEngine::Renderer*>(true);
  for (auto* info : validInfos) {
    InstantiateReplacementPrefab(*info, replacementParent, replacement);
  }
  if (replacement.spawnedObjects.empty() && replacement.disabledRenderers.empty()) {
    VIVIFY_DEBUG("Vivify note replace: {} valid prefab(s) but nothing spawned/hidden", validInfos.size());
    return;
  }

  auto* mpb = GetReplacementMaterialPropertyBlockController(noteController, replacementParent);
  if (IsAlive(mpb)) {
    ApplyReplacementRenderersToMaterialBlock(mpb, replacement, hideOriginal);
  }
  if (hideOriginal) {
    DisableOriginalRenderers(originalRenderers, replacement);
  }
  VIVIFY_DEBUG("Vivify note replaced: valid={} spawned={} replacementRenderers={} hideOriginal={} hiddenOriginals={}",
               validInfos.size(), replacement.spawnedObjects.size(), replacement.replacementRenderers.size(),
               hideOriginal, replacement.disabledRenderers.size());
  replacement.hideOriginal = hideOriginal;
  replacement.appliedFingerprint = ComputePrefabFingerprint(infos);
  _noteReplacements[noteController] = std::move(replacement);
}

void Runtime::TrackSaberModel(GlobalNamespace::SaberModelController* smc, GlobalNamespace::Saber* saber,
                              UnityEngine::Transform* initParent) {

  if (!_selectedMapHasVivifyRequirement) return;
  if (!IsAlive(smc) || !IsAlive(saber)) return;

  UnityEngine::Transform* parent = smc->get_transform().unsafePtr();
  if (!IsAlive(parent)) parent = saber->get_transform().unsafePtr();
  if (!IsAlive(parent) && IsAlive(initParent)) parent = initParent;
  if (!IsAlive(parent)) return;
  if (GetVivifyDebugLogging()) {

    auto dumpChain = [](char const* label, UnityEngine::Transform* t) {
      std::string chain;
      int depth = 0;
      while (IsManagedAlive(t) && depth < 8) {
        auto go = t->get_gameObject();
        auto pos = t->get_position();
        auto lpos = t->get_localPosition();
        chain += fmt::format("{}[w=({:.2f},{:.2f},{:.2f}) l=({:.2f},{:.2f},{:.2f})] <- ",
                             IsManagedAlive(go.unsafePtr()) ? ToStdString(go->get_name()) : std::string("?"),
                             pos.x, pos.y, pos.z, lpos.x, lpos.y, lpos.z);
        t = t->get_parent().unsafePtr();
        depth++;
      }
      PaperLogger.info("Vivify saber chain {}: {}", label, chain);
    };
    dumpChain("modelNode(smc)", parent);
    auto saberNode = saber->get_transform();
    if (IsAlive(saberNode.unsafePtr()) && saberNode.unsafePtr() != parent) dumpChain("saberNode", saberNode.unsafePtr());
    if (IsAlive(initParent) && initParent != parent) dumpChain("initParent", initParent);
  }
  PurgeInvalidActiveSabers();
  auto existing = std::find_if(_activeSabers.begin(), _activeSabers.end(), [smc](ActiveSaberVisual const& target) {
    return target.controller == smc;
  });
  if (existing == _activeSabers.end()) {
    _activeSabers.push_back(ActiveSaberVisual{.controller = smc, .saber = saber, .parent = parent});
  } else {
    existing->saber = saber;
    existing->parent = parent;
  }
  ApplySaberVisuals(smc, saber, parent);
}

void Runtime::ApplySaberVisualsToActive() {
  PurgeInvalidActiveSabers();
  for (auto const& target : _activeSabers) {
    ApplySaberVisuals(target.controller, target.saber, target.parent);
  }
}

void Runtime::ApplySaberVisuals(GlobalNamespace::SaberModelController* smc, GlobalNamespace::Saber* saber,
                                UnityEngine::Transform* parent) {
  if (!IsAlive(smc) || !IsAlive(saber) || !IsAlive(parent) || _currentBeatmapData == nullptr || _isResetting) {
    RestoreSaberVisuals(smc);
    return;
  }
  int type = (int)saber->get_saberType();
  auto modelInfos = FindAssignedSaberPrefabs(type);
  auto trailInfos = FindAssignedSaberTrailPrefabs(type);
  auto existing = _saberReplacements.find(smc);
  if (existing != _saberReplacements.end() &&
      existing->second.appliedFingerprint == ComputeSaberFingerprint(modelInfos, trailInfos) &&
      ReplacementIntact(existing->second)) {
    ApplySaberReplacementColor(smc, saber, existing->second, true);
    return;
  }
  RestoreSaberVisuals(smc);
  auto validModelInfos = GetValidPrefabInfos(modelInfos);
  auto validTrailInfos = GetValidPrefabInfos(trailInfos);
  if (validModelInfos.empty() && validTrailInfos.empty()) {
    if (!modelInfos.empty() || !trailInfos.empty()) {
      VIVIFY_DEBUG("Vivify saber replace (type {}): {} model + {} trail assignment(s) matched but no asset "
                   "loaded (saber shows default)", type, modelInfos.size(), trailInfos.size());
    }
    return;
  }

  VisualReplacement replacement;
  if (ShouldHideOriginal(validModelInfos)) {
    DisableOriginalRenderers(smc->get_gameObject(), replacement);
  }
  for (auto* info : validModelInfos) {
    InstantiateReplacementPrefab(*info, parent, replacement);
  }
  ApplySaberTrailVisuals(smc, validTrailInfos, replacement);
  ApplySaberReplacementColor(smc, saber, replacement, true);
  VIVIFY_DEBUG("Vivify saber replaced (type {}): models={} trails={} spawned={} renderers={} hiddenOriginals={}",
               type, validModelInfos.size(), validTrailInfos.size(), replacement.spawnedObjects.size(),
               replacement.replacementRenderers.size(), replacement.disabledRenderers.size());
  if (!replacement.spawnedObjects.empty() || !replacement.disabledRenderers.empty()) {
    replacement.appliedFingerprint = ComputeSaberFingerprint(modelInfos, trailInfos);
    _saberReplacements[smc] = std::move(replacement);
  }
}

void Runtime::ApplySaberTrailVisuals(GlobalNamespace::SaberModelController* smc,
                                     std::vector<AssignedPrefabInfo*> const& infos,
                                     VisualReplacement& replacement) {
  if (infos.empty() || !IsAlive(smc)) return;
  auto* sourceTrail = smc->____saberTrail.unsafePtr();
  if (!IsAlive(sourceTrail) || !sourceTrail->____trailRendererPrefab) return;
  UnityEngine::Transform* parent = nullptr;
  auto existing = std::find_if(_activeSabers.begin(), _activeSabers.end(), [smc](ActiveSaberVisual const& target) {
    return target.controller == smc;
  });
  if (existing != _activeSabers.end()) {
    parent = existing->parent;
  }
  if (!IsAlive(parent)) {
    parent = smc->get_transform().unsafePtr();
  }
  if (!IsAlive(parent)) return;

  auto const defaultTop = UnityEngine::Vector3(0.0f, 0.0f, 1.0f);
  auto const defaultBottom = UnityEngine::Vector3(0.0f, 0.0f, 0.0f);
  for (auto* info : infos) {
    if (info == nullptr) continue;
    auto* material = GetAssetAs<UnityEngine::Material>(info->asset);
    if (!IsManagedAlive(material)) continue;
    RepairMaterialShader(material, info->asset);

    auto top = info->trailTopPos.value_or(defaultTop);
    auto bottom = info->trailBottomPos.value_or(defaultBottom);
    float duration = info->trailDuration.value_or(0.4f);
    if (!std::isfinite(duration)) duration = 0.4f;
    duration = std::clamp(duration, 0.01f, kMaxTrailDuration);
    int samplingFrequency = std::clamp(info->trailSamplingFrequency.value_or(50), 1, kMaxTrailSamplingFrequency);
    int granularity = std::clamp(info->trailGranularity.value_or(60), 1, kMaxTrailGranularity);

    auto* trailObject = UnityEngine::GameObject::New_ctor(u"VivifyFollowedSaberTrail");
    if (!IsAlive(trailObject)) continue;
    trailObject->get_transform()->SetParent(parent, false);
    auto* followedTrail = trailObject->AddComponent<FollowedSaberTrail*>();
    if (!IsAlive(followedTrail)) {
      UnityEngine::Object::Destroy(trailObject);
      continue;
    }
    followedTrail->InitFollowed(sourceTrail, parent, material, top, bottom, duration, samplingFrequency, granularity);
    if (!IsAlive(followedTrail->____trailRenderer.unsafePtr())) {
      UnityEngine::Object::Destroy(trailObject);
      continue;
    }
    replacement.spawnedObjects.emplace_back(trailObject);
    replacement.followedTrails.emplace_back(followedTrail);
  }

  if (!replacement.followedTrails.empty() && ShouldHideOriginal(infos)) {
    auto* sourceRenderer = sourceTrail->____trailRenderer.unsafePtr();
    if (IsAlive(sourceRenderer) && sourceRenderer->____meshRenderer && sourceRenderer->____meshRenderer->get_enabled()) {
      sourceRenderer->____meshRenderer->set_enabled(false);
      replacement.disabledRenderers.emplace_back(sourceRenderer->____meshRenderer);
    }
  }
}

void Runtime::ReplaceDebrisVisuals(GlobalNamespace::NoteDebris* debris) {
  RestoreDebrisVisuals(debris);
  if (!IsAlive(debris)) return;
  auto const& infos = _activeDebrisPrefabStack.empty() ? _lastCutDebrisPrefabs : _activeDebrisPrefabStack.back();
  if (infos.empty()) return;
  auto validInfos = GetValidPrefabInfos(infos);
  if (validInfos.empty()) return;
  UnityEngine::Transform* parent = debris->get_transform();
  if (!IsAlive(parent)) return;

  VisualReplacement replacement;
  bool const hideOriginal = ShouldHideOriginal(validInfos);
  if (hideOriginal) {
    DisableOriginalRenderers(debris->get_gameObject(), replacement);
  }
  for (auto* info : validInfos) {
    InstantiateReplacementPrefab(*info, parent, replacement);
  }
  ApplyReplacementRenderersToMaterialBlock(debris->get_gameObject(), replacement, hideOriginal);
  if (!replacement.spawnedObjects.empty() || !replacement.disabledRenderers.empty()) {
    _debrisReplacements[debris] = std::move(replacement);
  }
}

void Runtime::ApplyReplacementRenderersToMaterialBlock(GlobalNamespace::MaterialPropertyBlockController* mpb,
                                                       VisualReplacement& replacement, bool hideOriginal) {
  if (!IsAlive(mpb)) return;
  std::vector<UnityEngine::Renderer*> replacementRenderers;
  replacementRenderers.reserve(replacement.replacementRenderers.size());
  for (auto* renderer : replacement.replacementRenderers) {
    if (IsAlive(renderer)) {
      replacementRenderers.emplace_back(renderer);
    }
  }
  if (replacementRenderers.empty()) return;
  if (!replacement.hasOriginalMaterialBlockRenderers) {
    replacement.materialPropertyBlockController = mpb;
    replacement.originalMaterialBlockRenderers = mpb->____renderers;
    replacement.hasOriginalMaterialBlockRenderers = true;
  }

  std::vector<UnityEngine::Renderer*> originalRenderers;
  if (!hideOriginal && replacement.originalMaterialBlockRenderers) {
    originalRenderers.reserve(replacement.originalMaterialBlockRenderers.size());
    for (int i = 0; i < replacement.originalMaterialBlockRenderers.size(); i++) {
      auto* renderer = replacement.originalMaterialBlockRenderers[i].unsafePtr();
      if (IsAlive(renderer)) {
        originalRenderers.emplace_back(renderer);
      }
    }
  }
  auto convertedRenderers = ArrayW<UnityW<UnityEngine::Renderer>>(originalRenderers.size() + replacementRenderers.size());
  for (size_t i = 0; i < originalRenderers.size(); i++) {
    convertedRenderers[i] = originalRenderers[i];
  }
  for (size_t i = 0; i < replacementRenderers.size(); i++) {
    convertedRenderers[originalRenderers.size() + i] = replacementRenderers[i];
  }
  mpb->____renderers = convertedRenderers;
  mpb->ApplyChanges();
}

void Runtime::ApplyReplacementRenderersToMaterialBlock(UnityEngine::GameObject* gameObject,
                                                       VisualReplacement& replacement, bool hideOriginal) {
  if (!IsAlive(gameObject)) return;
  auto* mpb = gameObject->GetComponentInChildren<GlobalNamespace::MaterialPropertyBlockController*>(true);
  if (IsAlive(mpb)) {
    ApplyReplacementRenderersToMaterialBlock(mpb, replacement, hideOriginal);
  }
}

void Runtime::RestoreAllVisualReplacements() {
  for (auto& [_, replacement] : _noteReplacements) RestoreReplacementData(replacement);
  _noteReplacements.clear();
  for (auto& [_, replacement] : _saberReplacements) RestoreReplacementData(replacement);
  _saberReplacements.clear();
  for (auto& [_, replacement] : _debrisReplacements) RestoreReplacementData(replacement);
  _debrisReplacements.clear();
}

void Runtime::PurgeInvalidActiveSabers() {
  _activeSabers.erase(std::remove_if(_activeSabers.begin(), _activeSabers.end(), [this](ActiveSaberVisual const& target) {
    return !IsAlive(target.controller) || !IsAlive(target.saber) || !IsAlive(target.parent);
  }), _activeSabers.end());
}

void Runtime::ForceGameObjectRenderersOnTop(UnityEngine::GameObject* gameObject) {
  if (!IsAlive(gameObject) || _currentBeatmapData == nullptr || _isResetting) return;
  auto renderers = gameObject->GetComponentsInChildren<UnityEngine::Renderer*>(true);
  _overlayRendererSortingOrders.reserve(_overlayRendererSortingOrders.size() + renderers.size());
  for (int i = 0; i < renderers.size(); i++) {
    ForceRendererOnTop(renderers[i]);
  }
}

void Runtime::ForceRendererOnTop(UnityEngine::Renderer* renderer) {
  if (!IsAlive(renderer)) return;
  if (!_overlayRendererSortingOrders.contains(renderer)) {
    _overlayRendererSortingOrders.emplace(renderer, renderer->get_sortingOrder());
  }
  renderer->set_sortingOrder(kGameplayOverlaySortingOrder);
}

void Runtime::RestoreOverlayRenderState() {
  for (auto const& [renderer, sortingOrder] : _overlayRendererSortingOrders) {
    if (IsAlive(renderer)) {
      renderer->set_sortingOrder(sortingOrder);
    }
  }
  _overlayRendererSortingOrders.clear();
}

void Runtime::SetLayerRecursively(UnityEngine::GameObject* gameObject, int layer) {
  if (!IsAlive(gameObject) || layer < 0 || layer > 31) return;
  gameObject->set_layer(layer);
  auto transform = gameObject->get_transform();
  if (!IsAlive(transform)) return;
  int const childCount = transform->get_childCount();
  for (int i = 0; i < childCount; i++) {
    auto* child = transform->GetChild(i).unsafePtr();
    if (!IsAlive(child)) continue;
    SetLayerRecursively(child->get_gameObject().unsafePtr(), layer);
  }
}

}
