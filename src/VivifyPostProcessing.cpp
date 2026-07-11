#include "VivifyRuntimeInternal.hpp"
#include "VivifyComponents.hpp"
#include "UnityEngine/Rendering/CameraEvent.hpp"

namespace Vivify {

namespace {
UnityEngine::Rendering::RenderTargetIdentifier ToTargetId(UnityEngine::Texture* texture) {
  return UnityEngine::Rendering::RenderTargetIdentifier::op_Implicit___UnityEngine__Rendering__RenderTargetIdentifier(texture);
}
UnityEngine::Rendering::RenderTargetIdentifier CameraTargetId() {
  return UnityEngine::Rendering::RenderTargetIdentifier::op_Implicit___UnityEngine__Rendering__RenderTargetIdentifier(
      UnityEngine::Rendering::BuiltinRenderTextureType::CameraTarget);
}
}

void Runtime::ApplyBlits(UnityEngine::RenderTexture* src, UnityEngine::RenderTexture* dest,
                         std::string const& cameraName, int phase) {
  if (!IsAlive(src) || (dest != nullptr && !IsAlive(dest))) return;
  bool const passthroughOnly = GetDisableAllBlits() || _isResetting || _pauseMenuActive ||
                               _currentBeatmapData == nullptr ||
                               (phase == 1 ? _preEffects.empty()
                                           : (phase == 2 ? _postEffects.empty()
                                                         : (_preEffects.empty() && _postEffects.empty())));
  auto destId = (dest != nullptr) ? ToTargetId(dest) : CameraTargetId();
  if (passthroughOnly) {
    auto* cb = UnityEngine::Rendering::CommandBuffer::New_ctor();
    cb->Blit(ToTargetId(src), destId);
    UnityEngine::Graphics::ExecuteCommandBuffer(cb);
    cb->ReleaseBuffer();
    cb->Dispose();
    return;
  }
  auto* main = EnsureCachedBlitTexture(_mainBlitTexture, src);
  auto* scratch = EnsureCachedBlitTexture(_scratchBlitTexture, src);
  if (!IsAlive(main) || !IsAlive(scratch)) {
    auto* cb = UnityEngine::Rendering::CommandBuffer::New_ctor();
    cb->Blit(ToTargetId(src), destId);
    UnityEngine::Graphics::ExecuteCommandBuffer(cb);
    cb->ReleaseBuffer();
    cb->Dispose();
    return;
  }

  auto* cb = UnityEngine::Rendering::CommandBuffer::New_ctor();

  std::vector<UnityEngine::RenderTexture*> selfBlitTemps;

  cb->Blit(ToTargetId(src), ToTargetId(main));

  auto* mainCurrent = main;
  auto* mainScratch = scratch;

  auto emitBlit = [&](UnityEngine::Rendering::RenderTargetIdentifier srcId,
                      UnityEngine::Rendering::RenderTargetIdentifier dstId,
                      UnityEngine::Material* material, int pass) {
    if (material == nullptr) {
      cb->Blit(srcId, dstId);
    } else if (pass >= 0) {
      cb->Blit(srcId, dstId, material, pass);
    } else {
      cb->Blit(srcId, dstId, material);
    }
  };
  auto renderEffects = [&](std::vector<ActiveBlitEffect> const& effects) {

    for (auto it = effects.rbegin(); it != effects.rend(); ++it) {
      auto const& data = it->data;
      if (data.material != nullptr && !CanUseBlitMaterial(data.material, data.pass)) continue;
      bool const sourceIsMain = data.source == kMainCameraId;
      UnityEngine::RenderTexture* blitSrc = nullptr;
      if (sourceIsMain) {
        blitSrc = mainCurrent;
      } else if (auto found = _declaredTextures.find(data.source); found != _declaredTextures.end()) {
        blitSrc = found->second.texture;
      } else if (auto found = _secondaryCameras.find(data.source); found != _secondaryCameras.end()) {
        blitSrc = SecondaryCameraColorRT(found->second);
      }
      if (!IsAlive(blitSrc)) continue;
      auto blitSrcId = ToTargetId(blitSrc);
      for (auto const& targetName : data.targets) {
        if (targetName == kMainCameraId) {
          if (sourceIsMain) {
            emitBlit(blitSrcId, ToTargetId(mainScratch), data.material, data.pass);
            std::swap(mainCurrent, mainScratch);

            blitSrc = mainCurrent;
            blitSrcId = ToTargetId(mainCurrent);
          } else {

            emitBlit(blitSrcId, ToTargetId(mainCurrent), data.material, data.pass);
          }
        } else if (auto found = _declaredTextures.find(targetName); found != _declaredTextures.end()) {
          auto* target = found->second.texture;
          if (!IsAlive(target)) continue;
          if (target == blitSrc) {

            auto desc = target->get_descriptor();
            auto temp = UnityEngine::RenderTexture::GetTemporary(desc);
            auto* tempPtr = temp.unsafePtr();
            if (!IsManagedAlive(tempPtr)) continue;
            selfBlitTemps.emplace_back(tempPtr);
            emitBlit(blitSrcId, ToTargetId(tempPtr), data.material, data.pass);
            cb->Blit(ToTargetId(tempPtr), ToTargetId(target));
          } else {
            emitBlit(blitSrcId, ToTargetId(target), data.material, data.pass);
          }
        }
      }
    }
  };
  if (phase == 0 || phase == 1) {
    renderEffects(_preEffects);
  }
  if (phase == 0 || phase == 2) {
    renderEffects(_postEffects);
  }

  cb->Blit(ToTargetId(mainCurrent), destId);
  UnityEngine::Graphics::ExecuteCommandBuffer(cb);
  cb->ReleaseBuffer();
  cb->Dispose();
  for (auto* temp : selfBlitTemps) {
    UnityEngine::RenderTexture::ReleaseTemporary(temp);
  }
}

void Runtime::CacheMainRenderDescriptor(UnityEngine::RenderTexture* src) {
  if (!IsAlive(src)) return;
  auto desc = src->get_descriptor();

  desc.set_msaaSamples(1);
  desc.set_depthBufferBits(0);
  _cachedMainDescriptor = desc;
  _cachedMainVrUsage = src->get_vrUsage().value__;
  _hasMainDescriptor = true;
}

bool Runtime::EnsureMidRenderTextures() {
  if (!_hasMainDescriptor) return false;
  bool const mismatch = !IsAlive(_midMainRT) || !IsAlive(_midScratchRT) ||
                        _midMainRT->get_width() != _cachedMainDescriptor.get_width() ||
                        _midMainRT->get_height() != _cachedMainDescriptor.get_height() ||
                        _midMainRT->get_vrUsage().value__ != _cachedMainVrUsage;
  if (!mismatch) return true;
  ReleaseMidRenderTextures();
  _midMainRT = UnityEngine::RenderTexture::New_ctor(_cachedMainDescriptor);
  _midScratchRT = UnityEngine::RenderTexture::New_ctor(_cachedMainDescriptor);
  if (!IsAlive(_midMainRT) || !IsAlive(_midScratchRT) || !_midMainRT->Create() || !_midScratchRT->Create()) {
    ReleaseMidRenderTextures();
    return false;
  }
  ClearRenderTexture(_midMainRT);
  ClearRenderTexture(_midScratchRT);
  return true;
}

void Runtime::ReleaseMidRenderTextures() {
  if (IsAlive(_midMainRT)) {
    _midMainRT->Release();
    UnityEngine::Object::Destroy(_midMainRT);
  }
  if (IsAlive(_midScratchRT)) {
    _midScratchRT->Release();
    UnityEngine::Object::Destroy(_midScratchRT);
  }
  _midMainRT = nullptr;
  _midScratchRT = nullptr;
}

UnityEngine::Rendering::CommandBuffer* Runtime::BuildMidRenderCommandBuffer(
    std::vector<ActiveBlitEffect> const& effects) {
  using RTI = UnityEngine::Rendering::RenderTargetIdentifier;
  auto* cb = UnityEngine::Rendering::CommandBuffer::New_ctor();
  RTI srcId{};
  srcId._ctor(UnityEngine::Rendering::BuiltinRenderTextureType::CurrentActive);
  RTI dstId{};
  dstId._ctor(UnityEngine::Rendering::BuiltinRenderTextureType::CameraTarget);
  cb->Blit(srcId, ToTargetId(_midMainRT));

  auto* mainCurrent = _midMainRT;
  auto* mainScratch = _midScratchRT;

  auto emitBlit = [&](RTI blitSrc, RTI blitDst, UnityEngine::Material* material, int pass) {
    if (material == nullptr) {
      cb->Blit(blitSrc, blitDst);
    } else if (pass >= 0) {
      cb->Blit(blitSrc, blitDst, material, pass);
    } else {
      cb->Blit(blitSrc, blitDst, material);
    }
  };

  for (auto it = effects.rbegin(); it != effects.rend(); ++it) {
    auto const& data = it->data;
    if (data.material != nullptr && !CanUseBlitMaterial(data.material, data.pass)) continue;
    bool const sourceIsMain = data.source == kMainCameraId;
    UnityEngine::RenderTexture* blitSrcTexture = nullptr;
    if (sourceIsMain) {
      blitSrcTexture = mainCurrent;
    } else if (auto found = _declaredTextures.find(data.source); found != _declaredTextures.end()) {
      blitSrcTexture = found->second.texture;
    } else if (auto found = _secondaryCameras.find(data.source); found != _secondaryCameras.end()) {
      blitSrcTexture = SecondaryCameraColorRT(found->second);
    }
    if (!IsAlive(blitSrcTexture)) continue;
    auto blitSrcId = ToTargetId(blitSrcTexture);
    for (auto const& targetName : data.targets) {
      if (targetName == kMainCameraId) {
        if (sourceIsMain) {
          emitBlit(blitSrcId, ToTargetId(mainScratch), data.material, data.pass);
          std::swap(mainCurrent, mainScratch);
          blitSrcTexture = mainCurrent;
          blitSrcId = ToTargetId(mainCurrent);
        } else {

          emitBlit(blitSrcId, ToTargetId(mainCurrent), data.material, data.pass);
        }
      } else if (auto found = _declaredTextures.find(targetName); found != _declaredTextures.end()) {
        auto* target = found->second.texture;
        if (!IsAlive(target)) continue;
        if (target == blitSrcTexture) {

          auto temp = UnityEngine::RenderTexture::GetTemporary(target->get_descriptor());
          auto* tempPtr = temp.unsafePtr();
          if (!IsManagedAlive(tempPtr)) continue;
          _midSelfBlitTemps.emplace_back(tempPtr);
          emitBlit(blitSrcId, ToTargetId(tempPtr), data.material, data.pass);
          cb->Blit(ToTargetId(tempPtr), ToTargetId(target));
        } else {
          emitBlit(blitSrcId, ToTargetId(target), data.material, data.pass);
        }
      }
    }
  }

  cb->Blit(ToTargetId(mainCurrent), dstId);
  return cb;
}

void Runtime::AddMidRenderCommandBuffers(UnityEngine::Camera* camera) {

  RemoveMidRenderCommandBuffers();
  if (!IsAlive(camera) || GetDisableAllBlits() || _isResetting || _pauseMenuActive ||
      _currentBeatmapData == nullptr || !HasMidRenderEffects()) {
    return;
  }

  if (!EnsureMidRenderTextures()) return;

  static constexpr std::array<int32_t, kMidRenderOrderCount> kOrderEvents = {
      static_cast<int32_t>(UnityEngine::Rendering::CameraEvent::__CameraEvent_Unwrapped::__E_BeforeSkybox),
      static_cast<int32_t>(UnityEngine::Rendering::CameraEvent::__CameraEvent_Unwrapped::__E_AfterSkybox),
      static_cast<int32_t>(UnityEngine::Rendering::CameraEvent::__CameraEvent_Unwrapped::__E_BeforeForwardOpaque),
      static_cast<int32_t>(UnityEngine::Rendering::CameraEvent::__CameraEvent_Unwrapped::__E_AfterForwardOpaque),
      static_cast<int32_t>(UnityEngine::Rendering::CameraEvent::__CameraEvent_Unwrapped::__E_BeforeForwardAlpha),
      static_cast<int32_t>(UnityEngine::Rendering::CameraEvent::__CameraEvent_Unwrapped::__E_AfterForwardAlpha),
  };
  for (int bucket = 0; bucket < kMidRenderOrderCount; bucket++) {
    auto const& effects = _midEffects[bucket];
    if (effects.empty()) continue;
    auto* cb = BuildMidRenderCommandBuffer(effects);
    if (cb == nullptr) continue;
    camera->AddCommandBuffer(UnityEngine::Rendering::CameraEvent(kOrderEvents[bucket]), cb);

    _activeMidCommandBuffers.emplace_back(kOrderEvents[bucket],
                                          SafePtr<UnityEngine::Rendering::CommandBuffer>(cb));
  }
  _midCommandBufferCamera = camera;
}

void Runtime::RemoveMidRenderCommandBuffers() {
  for (auto& [cameraEvent, cb] : _activeMidCommandBuffers) {
    if (!cb) continue;
    if (IsAlive(_midCommandBufferCamera)) {
      _midCommandBufferCamera->RemoveCommandBuffer(UnityEngine::Rendering::CameraEvent(cameraEvent), cb.ptr());
    }
    cb->Dispose();
  }
  _activeMidCommandBuffers.clear();
  _midCommandBufferCamera = nullptr;

  for (auto* temp : _midSelfBlitTemps) {
    if (IsAlive(temp)) UnityEngine::RenderTexture::ReleaseTemporary(temp);
  }
  _midSelfBlitTemps.clear();
}

UnityEngine::RenderTexture* Runtime::EnsureCachedBlitTexture(UnityEngine::RenderTexture*& texture,
                                                             UnityEngine::RenderTexture* src) {
  auto desc = src->get_descriptor();
  desc.set_msaaSamples(1);
  desc.set_depthBufferBits(0);
  int const width = desc.get_width();
  int const height = desc.get_height();
  int const format = desc.get_graphicsFormat().value__;
  if (IsAlive(texture)) {
    if (_cachedBlitWidth == width &&
        _cachedBlitHeight == height &&
        _cachedBlitFormat == format &&
        texture->IsCreated()) {
      return texture;
    }
    ReleaseRenderTexture(texture);
  }
  texture = UnityEngine::RenderTexture::New_ctor(desc);
  if (!IsAlive(texture)) { texture = nullptr; return nullptr; }
  if (!texture->Create()) { ReleaseRenderTexture(texture); return nullptr; }
  _cachedBlitWidth = width;
  _cachedBlitHeight = height;
  _cachedBlitFormat = format;
  ClearRenderTexture(texture);
  return texture;
}

void Runtime::ReleaseRenderTexture(UnityEngine::RenderTexture*& texture) {
  if (IsAlive(texture)) {
    texture->Release();
    UnityEngine::Object::Destroy(texture);
  }
  texture = nullptr;
}

void Runtime::ReleaseCachedBlitTextures() {
  ReleaseRenderTexture(_mainBlitTexture);
  ReleaseRenderTexture(_scratchBlitTexture);
}

bool Runtime::CanUseBlitMaterial(UnityEngine::Material* material, int pass) const {
  if (!IsAlive(material)) {
    return false;
  }
  if (auto it = _blitMaterialValidCache.find(material); it != _blitMaterialValidCache.end()) {
    return it->second;
  }
  auto shader = material->get_shader();
  auto* rawShader = shader.unsafePtr();
  auto shaderName = ShaderNameForLog(rawShader);
  if (!IsAlive(rawShader) || !rawShader->get_isSupported() || IsInternalErrorShaderName(shaderName)) {
    if (GetVivifyDebugLogging()) {
      PaperLogger.warn("Vivify blit material invalid: material='{}' shader='{}' supported={} internalError={} pass={}",
                       ToStdString(material->get_name()), shaderName,
                       BoolText(IsAlive(rawShader) && rawShader->get_isSupported()),
                       BoolText(IsInternalErrorShaderName(shaderName)), pass);
    }
    _blitMaterialValidCache[material] = false;
    return false;
  }
  int const passCount = material->get_passCount();
  if (passCount <= 0 || (pass >= 0 && pass >= passCount)) {
    if (GetVivifyDebugLogging()) {
      PaperLogger.warn("Vivify blit material invalid pass: material='{}' pass={} passCount={}",
                       ToStdString(material->get_name()), pass, passCount);
    }
    _blitMaterialValidCache[material] = false;
    return false;
  }
  _blitMaterialValidCache[material] = true;
  return true;
}

bool Runtime::SameBlitData(BlitMaterialData const& left, BlitMaterialData const& right) {
  return left.material == right.material &&
         left.priority == right.priority &&
         left.source == right.source &&
         left.targets == right.targets &&
         left.pass == right.pass;
}

PostProcessingOrder Runtime::ParsePostProcessingOrder(rapidjson::Value const& json) {
  auto order = ReadStringView(json, "order");
  if (!order.has_value()) return PostProcessingOrder::AfterMainEffect;
  std::string normalized = NormalizeAssetKey(*order);
  if (normalized == "beforemaineffect") return PostProcessingOrder::BeforeMainEffect;
  if (normalized == "beforeskybox") return PostProcessingOrder::BeforeSkybox;
  if (normalized == "afterskybox") return PostProcessingOrder::AfterSkybox;
  if (normalized == "beforeopaque") return PostProcessingOrder::BeforeOpaque;
  if (normalized == "afteropaque") return PostProcessingOrder::AfterOpaque;
  if (normalized == "beforealpha") return PostProcessingOrder::BeforeAlpha;
  if (normalized == "afteralpha") return PostProcessingOrder::AfterAlpha;
  return PostProcessingOrder::AfterMainEffect;
}

void Runtime::AddOrUpdateBlitEffect(std::vector<ActiveBlitEffect>& effects, ActiveBlitEffect effect) {
  auto existing = std::find_if(effects.begin(), effects.end(), [&](ActiveBlitEffect const& active) {
    return SameBlitData(active.data, effect.data);
  });
  if (existing != effects.end()) {
    if (effect.data.frame.has_value()) {
      existing->data.frame = effect.data.frame;
      existing->expireTime = 0.0f;
    } else {
      existing->data.frame.reset();
      existing->expireTime = effect.expireTime <= 0.0f ? 0.0f : std::max(existing->expireTime, effect.expireTime);
    }
  } else {
    effects.emplace_back(std::move(effect));
  }
  std::stable_sort(effects.begin(), effects.end(), [](ActiveBlitEffect const& left, ActiveBlitEffect const& right) {
    return left.data.priority < right.data.priority;
  });
  while (effects.size() > kMaxActiveBlitEffects) {
    effects.erase(effects.begin());
  }
}

void Runtime::HandleBlit(CustomJSONData::CustomEventData* customEventData, rapidjson::Value const& json) {
  auto asset = ReadStringView(json, "asset");
  std::string normalizedAsset = asset.has_value() ? NormalizeAssetKey(*asset) : std::string();
  if (GetDisableAllBlits()) {
    if (GetVivifyDebugLogging()) {
      PaperLogger.info("Vivify Blit skipped at time {}: Disable All Blits enabled asset='{}'",
                       customEventData->time, asset.has_value() ? std::string(*asset) : std::string("<none>"));
    }
    return;
  }
  if (asset.has_value() && GetDisableBeat0FilmgrainBlit() && std::fabs(customEventData->time) < 0.01f &&
      normalizedAsset.find("filmgrain") != std::string::npos) {
    if (GetVivifyDebugLogging()) {
      PaperLogger.info("Vivify Blit skipped at time {}: beat-0 filmgrain isolation asset='{}'",
                       customEventData->time, std::string(*asset));
    }
    return;
  }
  float const durationBeats = ReadFloat(json, "duration").value_or(0.0f);
  float const duration = DurationBeatsToSeconds(durationBeats);
  UnityEngine::Material* material = nullptr;
  if (asset.has_value()) {
    material = GetAssetAs<UnityEngine::Material>(*asset);
    if (material == nullptr) {
      if (GetVivifyDebugLogging()) {
        PaperLogger.warn("Vivify Blit material load failed at time {}: asset='{}'",
                         customEventData->time, std::string(*asset));
      }
      return;
    }
    RepairMaterialShader(material, *asset);
    LogMaterialShader("Blit", *asset, material);
    auto properties = ParseMaterialProperties(json);
    if (!properties.empty()) {
      Functions easing = ParseEasing(ReadStringView(json, "easing").value_or("easeLinear"));
      float const startTime = customEventData->time;
      float const currentSongTime = CurrentSongTime();
      bool const completed = duration <= 0.0f || startTime + duration <= currentSongTime;
      float const initialProgress = completed ? 1.0f : 0.0f;
      for (auto const& property : properties) ApplyMaterialProperty(material, property, initialProgress);
      if (!completed) {
        QueueMaterialPropertyAnimation(material, properties, startTime, duration, easing);
      }
    }
  }
  int const priority = ReadInt(json, "priority").value_or(0);
  int const pass = ReadInt(json, "pass").value_or(-1);
  std::string sourceStr{ReadStringView(json, "source").value_or(kMainCameraId)};
  std::vector<std::string> targets = ReadStringListOrSingle(json, "destination");
  if (targets.empty()) targets.emplace_back(kMainCameraId);
  PostProcessingOrder const order = ParsePostProcessingOrder(json);
  auto& effects = [&]() -> std::vector<ActiveBlitEffect>& {
    switch (order) {
      case PostProcessingOrder::BeforeMainEffect: return _preEffects;
      case PostProcessingOrder::AfterMainEffect: return _postEffects;
      default: return _midEffects[static_cast<int>(order)];
    }
  }();
  BlitMaterialData bd{material, priority, std::move(sourceStr), std::move(targets), pass,
                      std::nullopt, std::move(normalizedAsset), customEventData->time};
  if (GetVivifyDebugLogging()) {
    PaperLogger.info("Vivify Blit event: time={} asset='{}' source='{}' targets={} priority={} pass={} order={} durationBeats={}",
                     customEventData->time,
                     asset.has_value() ? std::string(*asset) : std::string("<none>"),
                     bd.source, bd.targets.size(), priority, pass,
                     static_cast<int>(order), durationBeats);
  }
  float const songTime = CurrentSongTime();
  if (durationBeats == 0.0f) {

    bd.frame = static_cast<int>(UnityEngine::Time::get_frameCount());
    AddOrUpdateBlitEffect(effects, ActiveBlitEffect{std::move(bd), 0.0f});
  } else if (durationBeats > 0.0f && songTime <= customEventData->time + duration) {
    AddOrUpdateBlitEffect(effects, ActiveBlitEffect{std::move(bd), customEventData->time + duration});
  }

}

void Runtime::UpdateBlitEffects() {
  if (GetDisableAllBlits()) {
    if ((!_preEffects.empty() || !_postEffects.empty() || HasMidRenderEffects()) && GetVivifyDebugLogging()) {
      PaperLogger.info("Vivify Blit effects cleared: Disable All Blits enabled pre={} post={}",
                       _preEffects.size(), _postEffects.size());
    }
    _preEffects.clear();
    _postEffects.clear();
    for (auto& bucket : _midEffects) bucket.clear();
    return;
  }
  float const songTime = CurrentSongTime();
  int const frame = static_cast<int>(UnityEngine::Time::get_frameCount());
  bool const dropBeat0Filmgrain = GetDisableBeat0FilmgrainBlit();
  auto cleanup = [&](std::vector<ActiveBlitEffect>& effects) {
    effects.erase(std::remove_if(effects.begin(), effects.end(), [&](ActiveBlitEffect const& e) {
      if (dropBeat0Filmgrain && std::fabs(e.data.eventBeat) < 0.01f &&
          e.data.asset.find("filmgrain") != std::string::npos) {
        return true;
      }
      if (e.data.frame.has_value()) return e.data.frame.value() != frame;
      return e.expireTime > 0.0f && songTime >= e.expireTime;
    }), effects.end());
  };
  cleanup(_preEffects);
  cleanup(_postEffects);
  for (auto& bucket : _midEffects) cleanup(bucket);
}

void Runtime::HandleCreateScreenTexture(rapidjson::Value const& json) {
  auto id = ReadStringView(json, "id");
  if (!id.has_value()) return;
  std::string name(*id);
  DestroyDeclaredTextureById(name);
  DeclaredTextureData dt;
  dt.name = name;
  dt.propertyId = UnityEngine::Shader::PropertyToID(StringW(name));
  dt.xRatio = ReadFloat(json, "xRatio").value_or(1.0f);
  dt.yRatio = ReadFloat(json, "yRatio").value_or(1.0f);
  if (auto w = ReadInt(json, "width")) dt.width = *w;
  if (auto h = ReadInt(json, "height")) dt.height = *h;
  if (auto fmt = ReadStringView(json, "colorFormat"))
    dt.format = [&]() -> std::optional<UnityEngine::RenderTextureFormat> {
      auto s = *fmt;
      if (s == "ARGB32") return UnityEngine::RenderTextureFormat::ARGB32;
      if (s == "Depth") return UnityEngine::RenderTextureFormat::Depth;
      if (s == "ARGBHalf") return UnityEngine::RenderTextureFormat::ARGBHalf;
      if (s == "Shadowmap") return UnityEngine::RenderTextureFormat::Shadowmap;
      if (s == "RGB565") return UnityEngine::RenderTextureFormat::RGB565;
      if (s == "ARGB4444") return UnityEngine::RenderTextureFormat::ARGB4444;
      if (s == "ARGB1555") return UnityEngine::RenderTextureFormat::ARGB1555;
      if (s == "Default") return UnityEngine::RenderTextureFormat::Default;
      if (s == "ARGBFloat") return UnityEngine::RenderTextureFormat::ARGBFloat;
      if (s == "RGFloat") return UnityEngine::RenderTextureFormat::RGFloat;
      if (s == "RFloat") return UnityEngine::RenderTextureFormat::RFloat;
      if (s == "RHalf") return UnityEngine::RenderTextureFormat::RHalf;
      if (s == "R8") return UnityEngine::RenderTextureFormat::R8;
      if (s == "DefaultHDR") return UnityEngine::RenderTextureFormat::DefaultHDR;
      return std::nullopt;
    }();
  if (auto fm = ReadStringView(json, "filterMode"))
    dt.filterMode = [&]() -> std::optional<UnityEngine::FilterMode> {
      auto s = *fm;
      if (s == "Point") return UnityEngine::FilterMode::Point;
      if (s == "Bilinear") return UnityEngine::FilterMode::Bilinear;
      if (s == "Trilinear") return UnityEngine::FilterMode::Trilinear;
      return std::nullopt;
    }();
  if (dt.xRatio <= 0.0f) dt.xRatio = 1.0f;
  if (dt.yRatio <= 0.0f) dt.yRatio = 1.0f;
  int baseW = 1024;
  int baseH = 512;
  auto mainCam = UnityEngine::Camera::get_main();
  if (IsAlive(mainCam.unsafePtr())) {
    baseW = std::max(1, mainCam->get_pixelWidth());
    baseH = std::max(1, mainCam->get_pixelHeight());
  }
  int w = std::clamp(dt.width.value_or(baseW), 1, kMaxRenderTextureSize);
  int h = std::clamp(dt.height.value_or(baseH), 1, kMaxRenderTextureSize);
  w = std::clamp(static_cast<int>(w / dt.xRatio), 1, kMaxRenderTextureSize);
  h = std::clamp(static_cast<int>(h / dt.yRatio), 1, kMaxRenderTextureSize);
  auto requestedFmt = dt.format.value_or(UnityEngine::RenderTextureFormat::ARGB32);
  auto fmt = SupportedRenderTextureFormat(requestedFmt, "CreateScreenTexture:" + name);
  if (GetVivifyDebugLogging()) {
    PaperLogger.info("Vivify CreateScreenTexture: id='{}' size={}x{} requestedFormat={} finalFormat={} filter={} propertyId={}",
                     name, w, h, requestedFmt.value__, fmt.value__,
                     dt.filterMode.has_value() ? dt.filterMode->value__ : -1, dt.propertyId);
  }

  UnityEngine::RenderTextureDescriptor desc{};
  desc._ctor(w, h, fmt, 0);
  if (IsAlive(mainCam.unsafePtr()) && mainCam->get_stereoEnabled()) {
    desc.set_vrUsage(UnityEngine::VRTextureUsage::TwoEyes);
  }
  dt.texture = UnityEngine::RenderTexture::New_ctor(desc);
  if (IsAlive(dt.texture) && dt.filterMode.has_value()) dt.texture->set_filterMode(dt.filterMode.value());
  if (!IsAlive(dt.texture) || !dt.texture->Create()) {
    if (GetVivifyDebugLogging()) {
      PaperLogger.warn("Vivify CreateScreenTexture failed: id='{}' size={}x{} format={}", name, w, h, fmt.value__);
    }
    ReleaseDeclaredTextureData(dt);
    return;
  }
  ClearRenderTexture(dt.texture);
  UnityEngine::Shader::SetGlobalTexture(dt.propertyId, static_cast<UnityEngine::Texture*>(dt.texture));
  _declaredTextures[name] = std::move(dt);
}

void Runtime::ReleaseDeclaredTextureData(DeclaredTextureData& data) {

  if (data.propertyId != 0) {
    UnityEngine::Shader::SetGlobalTexture(data.propertyId, static_cast<UnityEngine::Texture*>(nullptr));
  }
  if (IsAlive(data.texture)) {
    data.texture->Release();
    UnityEngine::Object::Destroy(data.texture);
  }
  data.texture = nullptr;
}

bool Runtime::DestroyDeclaredTextureById(std::string const& id) {
  auto it = _declaredTextures.find(id);
  if (it == _declaredTextures.end()) return false;
  ReleaseDeclaredTextureData(it->second);
  _declaredTextures.erase(it);
  return true;
}

void Runtime::HandleCreateCamera(rapidjson::Value const& json) {
  auto id = ReadStringView(json, "id");
  if (!id.has_value()) return;
  std::string name(*id);
  DestroySecondaryCameraById(name);
  if (GetDisableCreateCameraDepth()) {
    if (GetVivifyDebugLogging()) {
      PaperLogger.info("Vivify CreateCamera skipped: isolation toggle enabled id='{}'", name);
    }
    return;
  }
  SecondaryCameraData cam;
  cam.name = name;
  if (auto tex = ReadStringView(json, "texture")) {
    cam.textureName = std::string(*tex);
    cam.texturePropertyId = UnityEngine::Shader::PropertyToID(StringW(*tex));
  }
  if (auto dtex = ReadStringView(json, "depthTexture")) {
    cam.depthTextureName = std::string(*dtex);
    cam.depthTexturePropertyId = UnityEngine::Shader::PropertyToID(StringW(*dtex));
  }

  if (auto stored = _cameraProperties.find(name); stored != _cameraProperties.end()) {
    cam.properties = stored->second;
  }
  auto* propsVal = ReadValuePtr(json, "properties");
  if (propsVal != nullptr && propsVal->IsObject()) {
    ParseCameraPropertyData(*propsVal, cam.properties);
  }
  _cameraProperties[name] = cam.properties;
  auto mainCam = UnityEngine::Camera::get_main();
  auto* mainCamPtr = mainCam.unsafePtr();
  auto* go = UnityEngine::GameObject::New_ctor(StringW("VivifyCamera_" + name));
  if (IsAlive(mainCamPtr)) {
    go->get_transform()->SetParent(mainCam->get_transform(), false);
  }
  cam.camera = go->AddComponent<UnityEngine::Camera*>();
  if (IsAlive(mainCamPtr)) cam.camera->CopyFrom(mainCamPtr);
  cam.camera->set_depth(IsAlive(mainCamPtr) ? mainCam->get_depth() - 1 : -2.0f);
  EnsureMultipassKeywordController(go);
  int w = 1024, h = 512;
  if (IsAlive(mainCamPtr)) {
    w = std::max(1, mainCam->get_pixelWidth());
    h = std::max(1, mainCam->get_pixelHeight());
  }
  bool const wantColor = cam.texturePropertyId.has_value();
  bool const wantDepth = cam.depthTexturePropertyId.has_value() &&
                         UnityEngine::SystemInfo::SupportsRenderTextureFormat(UnityEngine::RenderTextureFormat::Depth);
  if (cam.depthTexturePropertyId.has_value() && !wantDepth && GetVivifyDebugLogging()) {
    PaperLogger.warn("Vivify CreateCamera depth RT skipped: Depth RenderTextureFormat unsupported id='{}'", name);
  }
  if (!wantColor && !wantDepth) {

    cam.camera->set_enabled(false);
  } else if (wantColor) {

    UnityEngine::RenderTextureDescriptor desc{};
    desc._ctor(w, h, SupportedRenderTextureFormat(UnityEngine::RenderTextureFormat::ARGB32,
                                                  "CreateCamera:color:" + name), wantDepth ? 0 : 24);
    bool const stereo = IsAlive(mainCamPtr) && mainCamPtr->get_stereoEnabled();
    if (stereo) {
      desc.set_vrUsage(UnityEngine::VRTextureUsage::TwoEyes);
    }
    cam.colorRT = UnityEngine::RenderTexture::New_ctor(desc);
    if (wantDepth) {
      UnityEngine::RenderTextureDescriptor depthDesc{};
      depthDesc._ctor(w, h, UnityEngine::RenderTextureFormat::Depth, 24);
      if (stereo) {
        depthDesc.set_vrUsage(UnityEngine::VRTextureUsage::TwoEyes);
      }
      cam.depthRT = UnityEngine::RenderTexture::New_ctor(depthDesc);
    }
    bool const ok = IsAlive(cam.colorRT) && cam.colorRT->Create() &&
                    (!wantDepth || (IsAlive(cam.depthRT) && cam.depthRT->Create()));
    if (!ok) {
      if (GetVivifyDebugLogging()) {
        PaperLogger.warn("Vivify CreateCamera color/depth RT failed: id='{}' size={}x{} stereo={} wantDepth={}",
                         name, w, h, BoolText(stereo), BoolText(wantDepth));
      }
      ReleaseSecondaryCameraData(cam);
      return;
    }
    ClearRenderTexture(cam.colorRT);
    if (wantDepth) {
      ClearRenderTexture(cam.depthRT);
      cam.camera->SetTargetBuffers(cam.colorRT->get_colorBuffer(), cam.depthRT->get_depthBuffer());
      UnityEngine::Shader::SetGlobalTexture(cam.depthTexturePropertyId.value(),
                                            static_cast<UnityEngine::Texture*>(cam.depthRT));
    } else {
      cam.camera->set_targetTexture(cam.colorRT);
    }
    cam.camera->set_enabled(true);
    UnityEngine::Shader::SetGlobalTexture(cam.texturePropertyId.value(),
                                          static_cast<UnityEngine::Texture*>(cam.colorRT));

    auto* controller = go->AddComponent<SecondaryCameraController*>();
    if (IsAlive(controller) && GetVivifyDebugLogging()) {
      PaperLogger.info("Vivify CreateCamera '{}': stereo={} colorVrUsage={} depthVrUsage={} size={}x{} depth={}",
                       name, BoolText(stereo),
                       IsAlive(cam.colorRT) ? cam.colorRT->get_vrUsage().value__ : -1,
                       IsAlive(cam.depthRT) ? cam.depthRT->get_vrUsage().value__ : -1,
                       w, h, BoolText(wantDepth));
    }
  } else {

    auto colorFormat = SupportedRenderTextureFormat(UnityEngine::RenderTextureFormat::ARGB32, "CreateCamera:color:" + name);
    cam.colorRT = UnityEngine::RenderTexture::New_ctor(w, h, 0, colorFormat);
    cam.depthRT = UnityEngine::RenderTexture::New_ctor(w, h, 24, UnityEngine::RenderTextureFormat::Depth);
    bool const ok = IsAlive(cam.colorRT) && cam.colorRT->Create() && IsAlive(cam.depthRT) && cam.depthRT->Create();
    if (!ok) {
      if (GetVivifyDebugLogging()) {
        PaperLogger.warn("Vivify CreateCamera depth RT failed: id='{}' size={}x{}", name, w, h);
      }
      ReleaseSecondaryCameraData(cam);
      return;
    }
    ClearRenderTexture(cam.colorRT);
    ClearRenderTexture(cam.depthRT);
    cam.camera->SetTargetBuffers(cam.colorRT->get_colorBuffer(), cam.depthRT->get_depthBuffer());
    UnityEngine::Shader::SetGlobalTexture(cam.depthTexturePropertyId.value(),
                                          static_cast<UnityEngine::Texture*>(cam.depthRT));
  }
  if (GetVivifyDebugLogging()) {
    PaperLogger.info("Vivify CreateCamera: id='{}' colorTexture='{}' depthTexture='{}' size={}x{} mainEffect={}",
                     name, cam.textureName.value_or("<none>"), cam.depthTextureName.value_or("<none>"),
                     w, h, BoolText(cam.properties.mainEffect.value_or(false)));
  }
  ApplyCameraProperties(cam.camera, cam.properties);
  ApplyCameraGameObjectProperties(cam.camera, cam.properties);
  _secondaryCameras[name] = std::move(cam);
}

void Runtime::ReleaseSecondaryCameraData(SecondaryCameraData& data) {
  if (data.texturePropertyId.has_value()) {
    UnityEngine::Shader::SetGlobalTexture(data.texturePropertyId.value(), static_cast<UnityEngine::Texture*>(nullptr));
  }
  if (data.depthTexturePropertyId.has_value()) {
    UnityEngine::Shader::SetGlobalTexture(data.depthTexturePropertyId.value(), static_cast<UnityEngine::Texture*>(nullptr));
  }
  if (IsAlive(data.colorRT)) {
    data.colorRT->Release();
    UnityEngine::Object::Destroy(data.colorRT);
  }
  if (IsAlive(data.depthRT)) {
    data.depthRT->Release();
    UnityEngine::Object::Destroy(data.depthRT);
  }
  if (IsAlive(data.camera)) {
    UnityEngine::Object::Destroy(data.camera->get_gameObject());
  }
  data.colorRT = nullptr;
  data.depthRT = nullptr;
  data.camera = nullptr;
}

UnityEngine::RenderTexture* Runtime::SecondaryCameraColorRT(SecondaryCameraData const& cam) {
  if (IsAlive(cam.colorRT)) return cam.colorRT;
  if (!IsAlive(cam.camera)) return nullptr;
  auto* go = cam.camera->get_gameObject().unsafePtr();
  if (!IsAlive(go)) return nullptr;
  auto* controller = go->GetComponent<SecondaryCameraController*>();
  if (!IsAlive(controller) || controller->colorTextures.empty()) return nullptr;
  int eye = 0;
  if (auto* mainCam = IsAlive(_lastMainCameraGO) ? _lastMainCameraGO->GetComponent<UnityEngine::Camera*>() : nullptr;
      IsAlive(mainCam)) {
    eye = mainCam->get_stereoActiveEye().value__;
  }
  if (auto it = controller->colorTextures.find(eye); it != controller->colorTextures.end() && IsAlive(it->second)) {
    return it->second;
  }
  return controller->colorTextures.begin()->second;
}

void Runtime::BindSecondaryCameraTextures() {
  if (_secondaryCameras.empty()) return;
  auto* mainCam = IsAlive(_lastMainCameraGO) ? _lastMainCameraGO->GetComponent<UnityEngine::Camera*>() : nullptr;
  int const eye = IsAlive(mainCam) ? mainCam->get_stereoActiveEye().value__ : 0;
  for (auto& [n, cam] : _secondaryCameras) {
    if (!cam.texturePropertyId.has_value() || !IsAlive(cam.camera)) continue;

    if (IsAlive(cam.colorRT)) {
      UnityEngine::Shader::SetGlobalTexture(cam.texturePropertyId.value(),
                                            static_cast<UnityEngine::Texture*>(cam.colorRT));
      if (cam.depthTexturePropertyId.has_value() && IsAlive(cam.depthRT)) {
        UnityEngine::Shader::SetGlobalTexture(cam.depthTexturePropertyId.value(),
                                              static_cast<UnityEngine::Texture*>(cam.depthRT));
      }
      continue;
    }

    auto* go = cam.camera->get_gameObject().unsafePtr();
    if (!IsAlive(go)) continue;
    auto* controller = go->GetComponent<SecondaryCameraController*>();
    if (!IsAlive(controller)) continue;
    if (auto it = controller->colorTextures.find(eye); it != controller->colorTextures.end() && IsAlive(it->second)) {
      UnityEngine::Shader::SetGlobalTexture(cam.texturePropertyId.value(),
                                            static_cast<UnityEngine::Texture*>(it->second));
    }
  }
}

bool Runtime::DestroySecondaryCameraById(std::string const& id) {
  auto it = _secondaryCameras.find(id);
  if (it == _secondaryCameras.end()) return false;
  ReleaseSecondaryCameraData(it->second);
  _secondaryCameras.erase(it);
  _cameraProperties.erase(id);
  return true;
}

void Runtime::ApplySecondaryCameraMainEffects(GlobalNamespace::MainEffectController* mainEffectController) {
  if (_secondaryCameras.empty() || _isResetting || _pauseMenuActive || _currentBeatmapData == nullptr) return;
  if (!IsManagedAlive(mainEffectController) ||
      mainEffectController->_mainEffectContainer.ptr() == nullptr ||
      !IsManagedAlive(mainEffectController->_mainEffectContainer)) {
    return;
  }
  auto mainEffectSO = mainEffectController->_mainEffectContainer->get_mainEffect();
  if (mainEffectSO.ptr() == nullptr || !IsManagedAlive(mainEffectSO) || !mainEffectSO->get_hasPostProcessEffect()) {
    return;
  }
  for (auto& [name, cam] : _secondaryCameras) {

    if (!cam.properties.mainEffect.value_or(false)) continue;
    if (!IsAlive(cam.colorRT)) continue;
    auto desc = cam.colorRT->get_descriptor();
    desc.set_msaaSamples(1);
    desc.set_depthBufferBits(0);
    auto temp = UnityEngine::RenderTexture::GetTemporary(desc);
    auto* tempPtr = temp.unsafePtr();
    if (!IsManagedAlive(tempPtr)) continue;
    mainEffectController->OnPreRender();
    mainEffectSO->Render(cam.colorRT, tempPtr, mainEffectController->____fadeValue);
    mainEffectController->OnPostRender();
    UnityEngine::Graphics::Blit(static_cast<UnityEngine::Texture*>(tempPtr), cam.colorRT);
    UnityEngine::RenderTexture::ReleaseTemporary(tempPtr);
  }
}

std::optional<UnityEngine::DepthTextureMode> Runtime::ParseDepthTextureModeName(std::string_view s) {
  if (s == "None") return UnityEngine::DepthTextureMode::None;
  if (s == "Depth") return UnityEngine::DepthTextureMode::Depth;
  if (s == "DepthNormals") return UnityEngine::DepthTextureMode::DepthNormals;
  if (s == "MotionVectors") return UnityEngine::DepthTextureMode::MotionVectors;
  return std::nullopt;
}

std::optional<UnityEngine::DepthTextureMode> Runtime::ParseDepthTextureModeValue(rapidjson::Value const& value) {
  if (value.IsString()) {
    return ParseDepthTextureModeName(std::string_view(value.GetString(), value.GetStringLength()));
  }
  if (!value.IsArray()) {
    return std::nullopt;
  }
  int flags = 0;
  bool parsedAny = false;
  for (auto const& item : value.GetArray()) {
    if (!item.IsString()) continue;
    auto mode = ParseDepthTextureModeName(std::string_view(item.GetString(), item.GetStringLength()));
    if (!mode.has_value()) continue;
    flags |= mode->value__;
    parsedAny = true;
  }
  if (!parsedAny) return std::nullopt;
  return UnityEngine::DepthTextureMode(flags);
}

void Runtime::ParseCameraPropertyData(rapidjson::Value const& json, CameraPropertyData& out) {
  if (auto* dtm = ReadValuePtr(json, "depthTextureMode")) {
    out.depthTextureMode = dtm->IsNull() ? std::nullopt : ParseDepthTextureModeValue(*dtm);
    if (GetDisableCreateCameraDepth()) out.depthTextureMode.reset();
  }
  if (auto* cf = ReadValuePtr(json, "clearFlags")) {
    out.clearFlags = std::nullopt;
    if (cf->IsString()) {
      std::string_view s(cf->GetString(), cf->GetStringLength());
      if (s == "Skybox") out.clearFlags = UnityEngine::CameraClearFlags::Skybox;
      else if (s == "Color" || s == "SolidColor") out.clearFlags = UnityEngine::CameraClearFlags::Color;
      else if (s == "Depth") out.clearFlags = UnityEngine::CameraClearFlags::Depth;
      else if (s == "Nothing") out.clearFlags = UnityEngine::CameraClearFlags::Nothing;
    }
  }
  if (auto* bgVal = ReadValuePtr(json, "backgroundColor")) {
    out.backgroundColor = bgVal->IsNull() ? std::nullopt : ReadColorArray(*bgVal);
  }
  if (auto* cullingVal = ReadValuePtr(json, "culling")) {
    if (cullingVal->IsObject()) {
      CullingMaskData culling;
      bool const v2 = _currentBeatmapData != nullptr && _currentBeatmapData->v2orEarlier;
      culling.tracks = ReadTracks(*cullingVal, v2);
      culling.whitelist = ReadBool(*cullingVal, "whitelist").value_or(false);
      if (culling.tracks.empty()) {
        out.culling.reset();
      } else {
        out.culling = std::move(culling);
      }
    } else {
      out.culling.reset();
    }
  }
  if (auto* bp = ReadValuePtr(json, "bloomPrePass")) {
    out.bloomPrePass = bp->IsBool() ? std::optional(bp->GetBool()) : std::nullopt;
  }
  if (auto* me = ReadValuePtr(json, "mainEffect")) {
    out.mainEffect = me->IsBool() ? std::optional(me->GetBool()) : std::nullopt;
  }
}

void Runtime::ApplyCameraProperties(UnityEngine::Camera* camera, CameraPropertyData const& props) {
  if (!IsAlive(camera)) return;
  bool const isMainCamera = camera->get_gameObject().unsafePtr() == _lastMainCameraGO;
  if (props.depthTextureMode.has_value()) {
    int mode = props.depthTextureMode->value__;

    if (isMainCamera && _mainCamOriginalDepthMode.has_value()) {
      mode |= _mainCamOriginalDepthMode.value();
    }
    camera->set_depthTextureMode(UnityEngine::DepthTextureMode(mode));
  } else if (isMainCamera && _mainCamOriginalDepthMode.has_value()) {
    camera->set_depthTextureMode(UnityEngine::DepthTextureMode(_mainCamOriginalDepthMode.value()));
  }
  if (props.clearFlags.has_value()) {
    camera->set_clearFlags(props.clearFlags.value());
  } else if (isMainCamera && _mainCamOriginalClearFlags.has_value()) {
    camera->set_clearFlags(UnityEngine::CameraClearFlags(_mainCamOriginalClearFlags.value()));
  }
  if (props.backgroundColor.has_value()) {
    camera->set_backgroundColor(props.backgroundColor.value());
  } else if (isMainCamera && _mainCamOriginalBackgroundColor.has_value()) {
    camera->set_backgroundColor(_mainCamOriginalBackgroundColor.value());
  }
}

void Runtime::ApplyCameraGameObjectProperties(UnityEngine::Camera* camera, CameraPropertyData const& props) {
  if (!IsAlive(camera)) return;
  auto* cameraGO = camera->get_gameObject().unsafePtr();
  if (!IsAlive(cameraGO)) return;
  ApplyCullingProperty(cameraGO, props.culling);
  auto* bloomPrePass = cameraGO->GetComponent<GlobalNamespace::BloomPrePass*>();
  if (IsAlive(bloomPrePass)) {
    bloomPrePass->set_enabled(props.bloomPrePass.value_or(true));
  }
}

CullingCameraController* Runtime::EnsureCullingController(UnityEngine::GameObject* gameObject) {
  if (!IsAlive(gameObject)) return nullptr;
  auto* controller = gameObject->GetComponent<CullingCameraController*>();
  if (!IsAlive(controller)) {
    controller = gameObject->AddComponent<CullingCameraController*>();
  }
  return controller;
}

void Runtime::ApplyCullingProperty(UnityEngine::GameObject* cameraGO, std::optional<CullingMaskData> const& culling) {
  if (!IsAlive(cameraGO)) return;
  if (culling.has_value()) {
    auto* controller = EnsureCullingController(cameraGO);
    if (controller != nullptr) {
      controller->SetCullingData(culling->whitelist, culling->tracks);
      controller->set_enabled(true);
    }
  } else {
    auto* controller = cameraGO->GetComponent<CullingCameraController*>();
    if (IsAlive(controller)) {
      controller->ClearCullingData();
      controller->set_enabled(false);
    }
  }
}

void Runtime::HandleSetCameraProperty(rapidjson::Value const& json) {
  auto id = ReadStringView(json, "id");
  std::string camId = id.has_value() ? std::string(*id) : std::string(kMainCameraId);
  auto* propsVal = ReadValuePtr(json, "properties");
  if (propsVal == nullptr || !propsVal->IsObject()) return;
  if (GetDisableCreateCameraDepth() && camId != kMainCameraId) {
    if (GetVivifyDebugLogging()) {
      PaperLogger.info("Vivify SetCameraProperty skipped: CreateCamera/Depth isolation camera='{}'", camId);
    }
    return;
  }

  auto& stored = _cameraProperties[camId];
  ParseCameraPropertyData(*propsVal, stored);
  if (GetVivifyDebugLogging()) {
    PaperLogger.info("Vivify SetCameraProperty: camera='{}' depthTextureMode={} clearFlags={} hasBackground={} culling={} bloomPrePass={} mainEffect={}",
                     camId,
                     stored.depthTextureMode.has_value() ? stored.depthTextureMode->value__ : -1,
                     stored.clearFlags.has_value() ? stored.clearFlags->value__ : -1,
                     BoolText(stored.backgroundColor.has_value()),
                     BoolText(stored.culling.has_value()),
                     stored.bloomPrePass.has_value() ? BoolText(*stored.bloomPrePass) : "unset",
                     stored.mainEffect.has_value() ? BoolText(*stored.mainEffect) : "unset");
  }
  if (camId == kMainCameraId) {
    _mainCameraPropsDirty = true;
    auto mainCam = UnityEngine::Camera::get_main();
    auto* mainCamPtr = mainCam.unsafePtr();
    if (IsAlive(mainCamPtr)) {
      CaptureMainCameraOriginals(mainCamPtr, mainCamPtr->get_gameObject().unsafePtr());
      ApplyCameraProperties(mainCamPtr, stored);
      ApplyCameraGameObjectProperties(mainCamPtr, stored);
    }
  } else {
    auto it = _secondaryCameras.find(camId);
    if (it != _secondaryCameras.end()) {

      ParseCameraPropertyData(*propsVal, it->second.properties);
      ApplyCameraProperties(it->second.camera, it->second.properties);
      ApplyCameraGameObjectProperties(it->second.camera, it->second.properties);
    }
  }
}

void Runtime::CaptureMainCameraOriginals(UnityEngine::Camera* mainCam, UnityEngine::GameObject* mainCamGO) {
  if (!IsAlive(mainCam) || mainCamGO != _lastMainCameraGO) return;
  if (!_mainCamOriginalDepthMode.has_value()) {
    _mainCamOriginalDepthMode = mainCam->get_depthTextureMode().value__;
  }
  if (!_mainCamOriginalClearFlags.has_value()) {
    _mainCamOriginalClearFlags = mainCam->get_clearFlags().value__;
  }
  if (!_mainCamOriginalBackgroundColor.has_value()) {
    _mainCamOriginalBackgroundColor = mainCam->get_backgroundColor();
  }
}

void Runtime::RestoreMainCameraOriginals() {
  if (IsAlive(_lastMainCameraGO)) {
    auto* mainCam = _lastMainCameraGO->GetComponent<UnityEngine::Camera*>();
    if (IsAlive(mainCam)) {
      if (_mainCamOriginalDepthMode.has_value()) {
        mainCam->set_depthTextureMode(UnityEngine::DepthTextureMode(_mainCamOriginalDepthMode.value()));
      }
      if (_mainCamOriginalClearFlags.has_value()) {
        mainCam->set_clearFlags(UnityEngine::CameraClearFlags(_mainCamOriginalClearFlags.value()));
      }
      if (_mainCamOriginalBackgroundColor.has_value()) {
        mainCam->set_backgroundColor(_mainCamOriginalBackgroundColor.value());
      }
    }
    ApplyCullingProperty(_lastMainCameraGO, std::nullopt);
    auto* bloomPrePass = _lastMainCameraGO->GetComponent<GlobalNamespace::BloomPrePass*>();
    if (IsAlive(bloomPrePass)) {
      bloomPrePass->set_enabled(true);
    }
  }
  _mainCamOriginalDepthMode.reset();
  _mainCamOriginalClearFlags.reset();
  _mainCamOriginalBackgroundColor.reset();
}

void Runtime::RefreshCameraComponents(bool allowCameraApplier) {
  auto mainCam = UnityEngine::Camera::get_main();
  auto* mainCamPtr = mainCam.unsafePtr();
  auto* mainCamGO = IsAlive(mainCamPtr) ? mainCam->get_gameObject().unsafePtr() : nullptr;
  bool const cameraChanged = mainCamGO != _lastMainCameraGO;
  if (cameraChanged) {
    _lastMainCameraGO = mainCamGO;
    _mainCamOriginalDepthMode.reset();
    _mainCamOriginalClearFlags.reset();
    _mainCamOriginalBackgroundColor.reset();
    if (IsAlive(mainCamPtr)) {
      auto* camTransform = mainCamPtr->get_transform().unsafePtr();
      auto pos = IsAlive(camTransform) ? camTransform->get_position() : UnityEngine::Vector3(0.0f, 0.0f, 0.0f);
      auto fwd = IsAlive(camTransform) ? camTransform->get_forward() : UnityEngine::Vector3(0.0f, 0.0f, 0.0f);
      PaperLogger.info("Vivify main camera: name='{}' cullingMask=0x{:08x} stereoMode={} multipassSetting={} pos=({:.2f},{:.2f},{:.2f}) fwd=({:.2f},{:.2f},{:.2f})",
                       IsAlive(mainCamGO) ? ToStdString(mainCamGO->get_name()) : std::string("?"),
                       static_cast<uint32_t>(mainCamPtr->get_cullingMask()),
                       static_cast<int>(UnityEngine::XR::XRSettings::get_stereoRenderingMode().value__),
                       BoolText(GetMultipassRenderingEnabled()),
                       pos.x, pos.y, pos.z, fwd.x, fwd.y, fwd.z);
    }
  }

  if (IsAlive(mainCamPtr)) {
    static int sLastLoggedCullingMask = -2;
    int const mask = mainCamPtr->get_cullingMask();
    if (mask != sLastLoggedCullingMask) {
      sLastLoggedCullingMask = mask;
      PaperLogger.info("Vivify main camera cullingMask is now 0x{:08x}", static_cast<uint32_t>(mask));
    }
  }

  RefreshMultipassRendering(mainCamGO);
  if (IsAlive(mainCamPtr)) {
    if (auto props = _cameraProperties.find(std::string(kMainCameraId)); props != _cameraProperties.end()) {
      CaptureMainCameraOriginals(mainCamPtr, mainCamGO);
      ApplyCameraProperties(mainCamPtr, props->second);
      ApplyCameraGameObjectProperties(mainCamPtr, props->second);
    }
  }
  _mainCameraPropsDirty = false;
  RefreshCameraApplier(mainCamGO, allowCameraApplier);
}

void Runtime::RefreshMultipassRendering(UnityEngine::GameObject* mainCamGO) {

  if (IsAlive(mainCamGO) && _currentBeatmapData != nullptr && !_isResetting) {
    _multipassController = EnsureMultipassKeywordController(mainCamGO);
    return;
  }
  if (_multipassController != nullptr && UnityEngine::Object::op_Implicit_bool(_multipassController)) {
    _multipassController->set_enabled(false);
    UnityEngine::Object::Destroy(_multipassController);
  }
  _multipassController = nullptr;
  SetMultipassShaderState(false);
}

MultipassKeywordController* Runtime::EnsureMultipassKeywordController(UnityEngine::GameObject* gameObject) {
  if (!IsAlive(gameObject)) return nullptr;
  auto* controller = gameObject->GetComponent<MultipassKeywordController*>();
  if (!IsAlive(controller)) {
    controller = gameObject->AddComponent<MultipassKeywordController*>();
  }
  if (IsAlive(controller)) {
    bool const enabled = GetMultipassRenderingEnabled();
    if (controller->get_enabled() != enabled) {
      controller->set_enabled(enabled);
    }
    if (!enabled) {
      SetMultipassShaderState(false);
    }
  }
  return controller;
}

void Runtime::RefreshCameraApplier(UnityEngine::GameObject* mainCamGO, bool allowCameraApplier) {
  if (_pauseMenuActive) allowCameraApplier = false;
  if (!allowCameraApplier) {
    if (_cameraApplier != nullptr && UnityEngine::Object::op_Implicit_bool(_cameraApplier) &&
        _cameraApplier->get_enabled()) {
      _cameraApplier->set_enabled(false);
    }
    return;
  }
  if (!IsAlive(mainCamGO)) {
    DestroyCameraApplier();
    return;
  }
  if (_cameraApplier == nullptr || !UnityEngine::Object::op_Implicit_bool(_cameraApplier) ||
      _cameraApplierGO != mainCamGO) {
    DestroyCameraApplier();
    _cameraApplier = mainCamGO->AddComponent<CameraApplier*>();
    _cameraApplier->set_enabled(false);
    _cameraApplierGO = mainCamGO;

    _cameraApplier->mainEffectController = nullptr;
    _cameraApplier->hasMainEffect = false;
  }

  bool const needsBlit = !GetDisableAllBlits() &&
                         (!_preEffects.empty() || !_postEffects.empty() || HasMidRenderEffects() ||
                          !_secondaryCameras.empty());
  if (_cameraApplier->get_enabled() != needsBlit) _cameraApplier->set_enabled(needsBlit);
}

void Runtime::DestroyCameraApplier() {
  RemoveMidRenderCommandBuffers();
  if (_cameraApplier != nullptr && UnityEngine::Object::op_Implicit_bool(_cameraApplier)) {
    _cameraApplier->set_enabled(false);
    UnityEngine::Object::Destroy(_cameraApplier);
  }
  _cameraApplier = nullptr;
  _cameraApplierGO = nullptr;
}

bool Runtime::ShouldBypassImageEffect(GlobalNamespace::ImageEffectController* controller) {
  if (_cameraApplier == nullptr || !UnityEngine::Object::op_Implicit_bool(_cameraApplier) ||
      !_cameraApplier->get_enabled() || !_cameraApplier->hasMainEffect) {
    return false;
  }
  if (!IsManagedAlive(controller)) return false;
  return controller->get_gameObject().unsafePtr() == _cameraApplierGO;
}

}
