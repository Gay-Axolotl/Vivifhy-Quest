#include "VivifyRuntimeInternal.hpp"
#include "VivifyComponents.hpp"

namespace Vivify {

rapidjson::Value const* Runtime::GetEventJson(CustomJSONData::CustomEventData* customEventData) const {
  if (customEventData->customData == nullptr || !customEventData->customData->value.has_value()) {
    return nullptr;
  }
  return &customEventData->customData->value.value().get();
}

std::optional<PointDefinitionW> Runtime::MakePointDefinition(rapidjson::Value const& object,
                                                             std::string_view key,
                                                             Tracks::ffi::WrapBaseValueType type) {
  auto* value = ReadValuePtr(object, key);
  if (value == nullptr) {
    return std::nullopt;
  }
  if (!value->IsArray() && !value->IsString()) {
    return std::nullopt;
  }
  if (_beatmapAD != nullptr) {
    return _beatmapAD->getPointDefinition(object, key, type);
  }
  if (!value->IsArray()) {
    return std::nullopt;
  }
  return PointDefinitionW(*value, type, _fallbackBeatmapAD.GetBaseProviderContext());
}

std::vector<TrackW> Runtime::ReadTracks(rapidjson::Value const& object, bool v2) {
  if (_beatmapAD == nullptr) {
    return {};
  }
  auto trackKey = v2 ? TracksAD::Constants::V2_TRACK : TracksAD::Constants::TRACK;
  auto tracks = NEJSON::ReadOptionalTracks(object, trackKey, *_beatmapAD);
  if (!tracks.has_value()) {
    return {};
  }
  return {tracks->begin(), tracks->end()};
}

CustomJSONData::CustomBeatmapData* Runtime::GetCustomBeatmapData(GlobalNamespace::BeatmapCallbacksController* callbackController) {
  auto* result = il2cpp_utils::try_cast<CustomJSONData::CustomBeatmapData>(callbackController->_beatmapData).value_or(nullptr);
  if (result == nullptr && GetVivifyDebugLogging()) {
    PaperLogger.warn("Vivify GetCustomBeatmapData: _beatmapData is not CustomBeatmapData, Vivify events will be ignored");
  }
  return result;
}

bool Runtime::EnsureBeatmapPrepared(GlobalNamespace::BeatmapCallbacksController* callbackController, float triggerTime) {
  auto* customBeatmapData = GetCustomBeatmapData(callbackController);
  if (customBeatmapData == nullptr) return false;
  if (_currentBeatmapData == customBeatmapData) return true;
  PrepareBeatmap(customBeatmapData, triggerTime);
  return _currentBeatmapData == customBeatmapData;
}

void Runtime::PrepareBeatmap(CustomJSONData::CustomBeatmapData* beatmapData, float triggerTime) {
  ResetRuntime();
  _currentBeatmapData = beatmapData;
  _beatmapAD = nullptr;
  _audioTimeSyncController = UnityEngine::Object::FindObjectOfType<GlobalNamespace::AudioTimeSyncController*>();
  if (beatmapData->customData != nullptr) {
    try {
      TracksAD::readBeatmapDataAD(beatmapData);
      _beatmapAD = &TracksAD::getBeatmapAD(beatmapData->customData);
    } catch (std::exception const& ex) {
      PaperLogger.warn("Vivify PrepareBeatmap: readBeatmapDataAD threw: {}", ex.what());
    }
  }
  VIVIFY_DEBUG("Vivify PrepareBeatmap: v2={} customEvents={} triggerTime={} beatmapAD={}",
               beatmapData->v2orEarlier, beatmapData->customEventDatas.size(),
               triggerTime, _beatmapAD != nullptr);
  LoadMainBundle();
  PreloadInstantiatePrefabs();
  ApplyMissedVivifyEvents(triggerTime);
  _lastSongTime = CurrentSongTime();
  VIVIFY_DEBUG("Vivify PrepareBeatmap done: assets={} livePrefabs={} preloadedPrefabs={}",
               _assets.size(), _livePrefabs.size(), _instantiatePrefabs.size());
}

void Runtime::DispatchEvent(std::string_view type, CustomJSONData::CustomEventData* customEventData,
                            rapidjson::Value const& json) {

  float const eventTime = customEventData != nullptr ? customEventData->time : -1.0f;
  try {
    if (type == kInstantiatePrefabEvent) {
      InstantiatePrefab(customEventData, json);
    } else if (type == kDestroyObjectEvent) {
      DestroyObjects(json);
    } else if (type == kSetMaterialPropertyEvent) {
      HandleSetMaterialProperty(customEventData, json);
    } else if (type == kSetAnimatorPropertyEvent) {
      HandleSetAnimatorProperty(customEventData, json);
    } else if (type == kSetGlobalPropertyEvent) {
      HandleSetGlobalProperty(customEventData, json);
    } else if (type == kBlitEvent) {
      HandleBlit(customEventData, json);
    } else if (type == kCreateCameraEvent) {
      HandleCreateCamera(json);
    } else if (type == kCreateScreenTextureEvent) {
      HandleCreateScreenTexture(json);
    } else if (type == kSetCameraPropertyEvent) {
      HandleSetCameraProperty(json);
    } else if (type == kSetRenderingSettingsEvent) {
      HandleSetRenderingSettings(customEventData, json);
    } else if (type == kAssignObjectPrefabEvent) {
      HandleAssignObjectPrefab(customEventData, json);
    }
  } catch (std::exception const& ex) {
    PaperLogger.error("Vivify event '{}' (time={}) threw and was skipped: {}",
                      std::string(type), eventTime, ex.what());
  } catch (...) {
    PaperLogger.error("Vivify event '{}' (time={}) threw a non-std exception and was skipped",
                      std::string(type), eventTime);
  }
}

void Runtime::HandleCustomEvent(GlobalNamespace::BeatmapCallbacksController* callbackController,
                                CustomJSONData::CustomEventData* customEventData) {
  if (callbackController == nullptr || customEventData == nullptr) return;

  if (!_selectedMapHasVivifyRequirement) return;

  if (!EnsureBeatmapPrepared(callbackController, customEventData->time)) return;
  std::string_view type = customEventData->type;
  if (!IsVivifyEvent(type)) return;

  if (_catchUpAppliedCustomEvents.erase(customEventData) > 0) return;
  auto* json = GetEventJson(customEventData);
  if (json == nullptr) return;
  if (GetVivifyDebugLogging()) {
    PaperLogger.info("Vivify custom event: type='{}' time={} songTime={}",
                     std::string(type), customEventData->time, CurrentSongTime());
  }
  DispatchEvent(type, customEventData, *json);
}

void Runtime::ApplyMissedVivifyEvents(float upToTime) {
  if (_currentBeatmapData == nullptr) return;
  _catchUpAppliedCustomEvents.clear();
  int applied = 0;
  for (auto* customEventData : _currentBeatmapData->customEventDatas) {
    if (customEventData == nullptr) continue;
    if (customEventData->time > upToTime) continue;
    std::string_view type = customEventData->type;
    if (!IsVivifyEvent(type)) continue;
    auto* json = GetEventJson(customEventData);
    if (json == nullptr) continue;
    VIVIFY_DEBUG("Vivify catch-up event: type='{}' time={}", std::string(type), customEventData->time);
    DispatchEvent(type, customEventData, *json);

    _catchUpAppliedCustomEvents.emplace(customEventData);
    applied++;
  }
  VIVIFY_DEBUG("Vivify catch-up: replayed {} event(s) up to time={}", applied, upToTime);
}

}
