#include "VivifyRuntimeInternal.hpp"
#include "VivifyComponents.hpp"
#include "UnityEngine/Renderer.hpp"
#include "UnityEngine/Matrix4x4.hpp"

DEFINE_TYPE(Vivify, OffsetBladeMovementData);
DEFINE_TYPE(Vivify, FollowedSaberTrail);
DEFINE_TYPE(Vivify, RuntimeBehaviour);
DEFINE_TYPE(Vivify, MultipassKeywordController);
DEFINE_TYPE(Vivify, CameraApplier);
DEFINE_TYPE(Vivify, CullingCameraController);
DEFINE_TYPE(Vivify, SecondaryCameraController);

namespace Vivify {

namespace {
inline int StereoActiveEyePropertyId() {

  static int id = UnityEngine::Shader::PropertyToID(u"_StereoActiveEye");
  return id;
}
}

void SetMultipassShaderState(bool enabled, UnityEngine::XR::XRSettings_StereoRenderingMode stereoMode, int eye) {
  static int lastEye = -2;
  if (lastEye == eye) return;
  lastEye = eye;

  (void)enabled;
  (void)stereoMode;
  UnityEngine::Shader::SetGlobalInt(StereoActiveEyePropertyId(), eye);
}

void SetMultipassShaderStateForCamera(UnityEngine::Camera* camera) {
  bool const hasCamera = IsManagedAlive(camera);
  auto const stereoMode = UnityEngine::XR::XRSettings::get_stereoRenderingMode();
  bool const isStereoCamera = hasCamera && UnityEngine::XR::XRSettings::get_enabled() &&
                              camera->get_stereoTargetEye().value__ != UnityEngine::StereoTargetEyeMask::None.value__;
  int const activeEye = hasCamera ? camera->get_stereoActiveEye().value__ : 0;
  SetMultipassShaderState(isStereoCamera, stereoMode, activeEye);
}

void OffsetBladeMovementData::Init(GlobalNamespace::IBladeMovementData* followed, UnityEngine::Transform* parent,
                                   UnityEngine::Vector3 topPos, UnityEngine::Vector3 bottomPos) {
  _followed = followed;
  _parent = parent;
  _topPos = topPos;
  _bottomPos = bottomPos;
}

float_t OffsetBladeMovementData::get_bladeSpeed() {
  return 0.0f;
}

GlobalNamespace::BladeMovementDataElement OffsetBladeMovementData::get_lastAddedData() {
  if (_followed == nullptr) return {};
  return Modify(_followed->get_lastAddedData());
}

GlobalNamespace::BladeMovementDataElement OffsetBladeMovementData::get_prevAddedData() {
  if (_followed == nullptr) return {};
  return Modify(_followed->get_prevAddedData());
}

GlobalNamespace::BladeMovementDataElement OffsetBladeMovementData::Modify(GlobalNamespace::BladeMovementDataElement original) {
  if (!IsManagedAlive(_parent)) return original;
  return GlobalNamespace::BladeMovementDataElement(
      original.time,
      original.segmentAngle,
      AddVectors(original.bottomPos, _parent->TransformVector(_topPos)),
      AddVectors(original.bottomPos, _parent->TransformVector(_bottomPos)),
      original.segmentNormal);
}

void FollowedSaberTrail::Awake() {}

void FollowedSaberTrail::InitFollowed(GlobalNamespace::SaberTrail* followed, UnityEngine::Transform* parent,
                                      UnityEngine::Material* material, UnityEngine::Vector3 topPos,
                                      UnityEngine::Vector3 bottomPos, float_t duration,
                                      int32_t samplingFrequency, int32_t granularity) {
  if (!IsManagedAlive(followed) || !IsManagedAlive(parent) || !IsManagedAlive(material)) return;
  auto* followedRenderer = followed->____trailRenderer.unsafePtr();
  auto* rendererPrefab = followed->____trailRendererPrefab.unsafePtr();
  if (!IsManagedAlive(followedRenderer) || !IsManagedAlive(rendererPrefab)) return;

  _followed = followed;
  if (_offsetMovementData == nullptr) {
    _offsetMovementData = OffsetBladeMovementData::New_ctor();
  }
  _offsetMovementData->Init(followed->____movementData, parent, topPos, bottomPos);

  ____trailDuration = duration;
  ____samplingFrequency = samplingFrequency;
  ____granularity = granularity;
  ____whiteSectionMaxDuration = followed->____whiteSectionMaxDuration;
  ____movementData = reinterpret_cast<GlobalNamespace::IBladeMovementData*>(_offsetMovementData);
  ____color = followed->____color;

  auto* trailRenderer = ____trailRenderer.unsafePtr();
  if (!IsManagedAlive(trailRenderer)) {
    trailRenderer = UnityEngine::Object::Instantiate<GlobalNamespace::SaberTrailRenderer*>(
        rendererPrefab, UnityEngine::Vector3(0.0f, 0.0f, 0.0f), UnityEngine::Quaternion::get_identity());
    ____trailRenderer = trailRenderer;
    if (!IsManagedAlive(trailRenderer)) return;
    auto sourceParent = followedRenderer->get_transform()->get_parent();
    if (IsManagedAlive(sourceParent.unsafePtr())) {
      trailRenderer->get_transform()->SetParent(sourceParent.unsafePtr());
    }
  }

  if (trailRenderer->____meshRenderer) {
    trailRenderer->____meshRenderer->set_material(material);
  }

  Init();
  Update();
  auto gameObject = get_gameObject();
  if (IsManagedAlive(gameObject.unsafePtr())) {
    gameObject->SetActive(true);
  }
}

void FollowedSaberTrail::Update() {
  if (!IsManagedAlive(_followed)) return;
  auto* trailRenderer = ____trailRenderer.unsafePtr();
  if (!IsManagedAlive(trailRenderer) || !trailRenderer->____meshRenderer) return;
  ____color = _followed->____color;
  if (_hasLastAppliedColor && NearlySameColor(____color, _lastAppliedColor)) return;

  auto* block = UnityEngine::MaterialPropertyBlock::New_ctor();
  if (block == nullptr) return;
  block->SetColor(ColorPropertyId(), ____color);
  trailRenderer->____meshRenderer->SetPropertyBlock(block);
  _lastAppliedColor = ____color;
  _hasLastAppliedColor = true;
}

void FollowedSaberTrail::LateUpdate() {
  auto* trailRenderer = ____trailRenderer.unsafePtr();
  auto* meshRenderer = IsManagedAlive(trailRenderer) ? trailRenderer->____meshRenderer.unsafePtr() : nullptr;
  if (!IsManagedAlive(_followed) || ____movementData == nullptr || !IsManagedAlive(meshRenderer)) {
    Cleanup();
    auto gameObject = get_gameObject();
    if (IsManagedAlive(gameObject.unsafePtr())) {
      gameObject->SetActive(false);
    }
    return;
  }
  static_cast<GlobalNamespace::SaberTrail*>(this)->LateUpdate();
}

void FollowedSaberTrail::Cleanup() {
  auto* trailRenderer = ____trailRenderer.unsafePtr();
  if (IsManagedAlive(trailRenderer)) {
    UnityEngine::Object::Destroy(trailRenderer->get_gameObject());
  }
  ____trailRenderer = nullptr;
  _followed = nullptr;
  _hasLastAppliedColor = false;
}

namespace {

constexpr int kCullingLayer = 22;

bool HasSecondaryCameraController(UnityEngine::MonoBehaviour* self) {
  auto go = self->get_gameObject();
  if (!IsManagedAlive(go.unsafePtr())) return false;
  return go->GetComponent<SecondaryCameraController*>() != nullptr;
}
}

void CullingCameraController::OnPreCull() {
  if (HasSecondaryCameraController(this)) return;
  CullingPreCull();
}

void CullingCameraController::OnPostRender() {
  if (HasSecondaryCameraController(this)) return;
  CullingPostRender();
}

void CullingCameraController::CullingPreCull() {
  if (_camera == nullptr) _camera = GetComponent<UnityEngine::Camera*>();
  if (!IsManagedAlive(_camera)) return;
  if (!_hasCullingData) return;

  CullingPostRender();

  if (_whitelist) {
    _cachedMask = _camera->get_cullingMask();
    _camera->set_cullingMask(1 << kCullingLayer);
  }

  std::unordered_set<UnityEngine::GameObject*> seen;
  int trackedGameObjects = 0;
  for (auto const& track : _tracks) {
    if (!track) continue;
    for (auto* go : track.GetGameObjects()) {
      if (!IsManagedAlive(go)) continue;
      trackedGameObjects++;
      auto renderers = go->GetComponentsInChildren<UnityEngine::Renderer*>(true);
      if (!renderers) continue;
      for (auto* renderer : renderers) {
        if (!IsManagedAlive(renderer)) continue;
        auto renderGo = renderer->get_gameObject();
        auto* renderGoPtr = renderGo.unsafePtr();
        if (!IsManagedAlive(renderGoPtr)) continue;
        if (!seen.emplace(renderGoPtr).second) continue;
        _cachedLayers.emplace_back(renderGoPtr, renderGoPtr->get_layer());
        renderGoPtr->set_layer(kCullingLayer);
      }
    }
  }

  if (_diagLogCount < 3 && GetVivifyDebugLogging()) {
    _diagLogCount++;
    auto go = get_gameObject();
    PaperLogger.info("Vivify culling pass on '{}': whitelist={} tracks={} trackedGOs={} movedGOs={} camMask=0x{:08x}",
                     IsManagedAlive(go.unsafePtr()) ? ToStdString(go->get_name()) : std::string("?"),
                     BoolText(_whitelist), _tracks.size(), trackedGameObjects, _cachedLayers.size(),
                     static_cast<uint32_t>(_camera->get_cullingMask()));
  }
}

void CullingCameraController::CullingPostRender() {
  if (_camera != nullptr && UnityEngine::Object::op_Implicit_bool(_camera) && _cachedMask.has_value()) {
    _camera->set_cullingMask(_cachedMask.value());
    _cachedMask.reset();
  }
  for (auto const& [go, layer] : _cachedLayers) {
    if (IsManagedAlive(go)) {
      go->set_layer(layer);
    }
  }
  _cachedLayers.clear();
}

void SecondaryCameraController::OnPreCull() {
  auto* camera = GetCamera();
  if (!IsManagedAlive(camera)) return;
  auto mainCam = UnityEngine::Camera::get_main();
  auto* other = mainCam.unsafePtr();
  if (IsManagedAlive(other) && other != camera) {

    if (camera->get_cullingMask() != other->get_cullingMask() ||
        camera->get_fieldOfView() != other->get_fieldOfView() ||
        camera->get_nearClipPlane() != other->get_nearClipPlane() ||
        camera->get_farClipPlane() != other->get_farClipPlane()) {
      camera->set_stereoTargetEye(other->get_stereoTargetEye());
      camera->set_fieldOfView(other->get_fieldOfView());
      camera->set_aspect(other->get_aspect());
      camera->set_depth(other->get_depth() - 1.0f);
      camera->set_nearClipPlane(other->get_nearClipPlane());
      camera->set_farClipPlane(other->get_farClipPlane());
      camera->set_layerCullDistances(other->get_layerCullDistances());
      camera->set_targetTexture(nullptr);
      camera->set_cullingMask(other->get_cullingMask());
    }
    auto transform = get_transform();
    if (IsManagedAlive(transform.unsafePtr())) {
      transform->set_localPosition(UnityEngine::Vector3::get_zero());
      transform->set_localRotation(UnityEngine::Quaternion::get_identity());
    }
    camera->set_cullingMatrix(
        UnityEngine::Matrix4x4::op_Multiply(other->get_projectionMatrix(), other->get_worldToCameraMatrix()));
    camera->set_projectionMatrix(other->get_projectionMatrix());
    camera->set_nonJitteredProjectionMatrix(other->get_nonJitteredProjectionMatrix());
    camera->set_worldToCameraMatrix(other->get_worldToCameraMatrix());
  }

  auto* culling = GetComponent<CullingCameraController*>();
  if (IsManagedAlive(culling)) {
    culling->CullingPreCull();
  }
}

void SecondaryCameraController::OnPostRender() {
  auto* culling = GetComponent<CullingCameraController*>();
  if (IsManagedAlive(culling)) {
    culling->CullingPostRender();
  }
}

UnityEngine::Camera* SecondaryCameraController::GetCamera() {
  if (!IsManagedAlive(_camera)) {
    auto go = get_gameObject();
    _camera = IsManagedAlive(go.unsafePtr()) ? go->GetComponent<UnityEngine::Camera*>() : nullptr;
  }
  return _camera;
}

void SecondaryCameraController::OnRenderImage(UnityEngine::RenderTexture* src, UnityEngine::RenderTexture* dest) {
  auto* camera = GetCamera();
  int const stereoActiveEye = IsManagedAlive(camera) ? camera->get_stereoActiveEye().value__ : 0;

  if (colorKey.has_value() && IsManagedAlive(src)) {
    auto it = colorTextures.find(stereoActiveEye);
    UnityEngine::RenderTexture* tex = (it != colorTextures.end()) ? it->second : nullptr;
    bool const mismatch = !IsManagedAlive(tex) || tex->get_width() != src->get_width() ||
                          tex->get_height() != src->get_height() || tex->get_vrUsage() != src->get_vrUsage();
    if (mismatch) {
      if (IsManagedAlive(tex)) {
        tex->Release();
        UnityEngine::Object::Destroy(tex);
      }
      auto descriptor = src->get_descriptor();
      descriptor.set_msaaSamples(1);
      tex = UnityEngine::RenderTexture::New_ctor(descriptor);
      if (IsManagedAlive(tex)) {
        tex->Create();
        colorTextures[stereoActiveEye] = tex;
      }
    }
    if (IsManagedAlive(tex)) {
      UnityEngine::Graphics::Blit(src, tex);
    }
  }

  UnityEngine::Graphics::Blit(src, dest);
}

void SecondaryCameraController::OnDestroy() {
  for (auto& [eye, tex] : colorTextures) {
    if (IsManagedAlive(tex)) {
      tex->Release();
      UnityEngine::Object::Destroy(tex);
    }
  }
  colorTextures.clear();
}

void MultipassKeywordController::Awake() {
  auto gameObject = get_gameObject();
  if (IsManagedAlive(gameObject.unsafePtr())) {
    _camera = gameObject->GetComponent<UnityEngine::Camera*>();
  }
}

void MultipassKeywordController::OnPreRender() {
  if (!IsManagedAlive(_camera)) {
    auto gameObject = get_gameObject();
    _camera = IsManagedAlive(gameObject.unsafePtr()) ? gameObject->GetComponent<UnityEngine::Camera*>() : nullptr;
  }
  SetMultipassShaderStateForCamera(_camera);
}

void MultipassKeywordController::OnDisable() {
  SetMultipassShaderState(false);
}

void RuntimeBehaviour::Update() {
  Runtime::Instance().Update();
}

void RuntimeBehaviour::OnDestroy() {
  Runtime::Instance().OnBehaviourDestroyed(this);
}

void CameraApplier::OnPreCull() {

  Runtime::Instance().RestoreSecondaryCullingLayers();
}

void CameraApplier::OnPreRender() {

  auto& runtime = Runtime::Instance();
  runtime.BindSecondaryCameraTextures();
  runtime.ApplySecondaryCameraMainEffects(mainEffectController);

  auto go = get_gameObject();
  auto* camera = IsManagedAlive(go.unsafePtr()) ? go->GetComponent<UnityEngine::Camera*>() : nullptr;
  runtime.AddMidRenderCommandBuffers(camera);
}

void CameraApplier::OnPostRender() {
  Runtime::Instance().RemoveMidRenderCommandBuffers();
}

void CameraApplier::OnRenderImage(UnityEngine::RenderTexture* src, UnityEngine::RenderTexture* dest) {
  auto& runtime = Runtime::Instance();
  runtime.CacheMainRenderDescriptor(src);
  if (hasMainEffect && IsManagedAlive(mainEffectController) &&
      mainEffectController->_mainEffectContainer.ptr() != nullptr &&
      IsManagedAlive(mainEffectController->_mainEffectContainer)) {
    auto mainEffectSO = mainEffectController->_mainEffectContainer->get_mainEffect();
    if (mainEffectSO.ptr() != nullptr && IsManagedAlive(mainEffectSO) && mainEffectSO->get_hasPostProcessEffect()) {
      if (runtime.IsResetting() || GetDisableAllBlits() ||
          (runtime.GetPreEffectsEmpty() && runtime.GetPostEffectsEmpty())) {
        mainEffectController->OnPreRender();
        mainEffectSO->Render(src, dest, mainEffectController->____fadeValue);
        mainEffectController->OnPostRender();
        return;
      }
      auto desc = src->get_descriptor();
      desc.set_msaaSamples(1);
      desc.set_depthBufferBits(0);
      auto temp = UnityEngine::RenderTexture::GetTemporary(desc);
      auto temp2 = UnityEngine::RenderTexture::GetTemporary(desc);

      bool const tempValid = IsManagedAlive(temp.unsafePtr());
      bool const temp2Valid = IsManagedAlive(temp2.unsafePtr());

      if (tempValid && temp2Valid) {
        runtime.ApplyBlits(src, temp.unsafePtr(), cameraName, 1);
        mainEffectController->OnPreRender();
        mainEffectSO->Render(temp.unsafePtr(), temp2.unsafePtr(), mainEffectController->____fadeValue);
        mainEffectController->OnPostRender();
        runtime.ApplyBlits(temp2.unsafePtr(), dest, cameraName, 2);
      } else if (tempValid) {
        runtime.ApplyBlits(src, temp.unsafePtr(), cameraName, 1);
        mainEffectController->OnPreRender();
        mainEffectSO->Render(temp.unsafePtr(), dest, mainEffectController->____fadeValue);
        mainEffectController->OnPostRender();
        runtime.ApplyBlits(dest, dest, cameraName, 2);
      } else {
        mainEffectController->OnPreRender();
        mainEffectSO->Render(src, dest, mainEffectController->____fadeValue);
        mainEffectController->OnPostRender();
        runtime.ApplyBlits(dest, dest, cameraName, 2);
      }

      if (tempValid) {
        UnityEngine::RenderTexture::ReleaseTemporary(temp.unsafePtr());
      }
      if (temp2Valid) {
        UnityEngine::RenderTexture::ReleaseTemporary(temp2.unsafePtr());
      }
      return;
    }
  }
  runtime.ApplyBlits(src, dest, cameraName, 0);
}

}
