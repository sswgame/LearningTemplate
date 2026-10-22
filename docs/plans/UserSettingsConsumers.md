# 사용자 설정 값을 읽는 쪽 잇기

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: D4-2 · 진행: 2/4

옵션 메뉴 백엔드(`Source/Engine/UserSettings/`, 메뉴는 `Engine/UI/Screen/OptionsMenuScreen`)의 그래픽 · 접근성 값에 읽는 쪽을 이었다. 읽는 곳은 `UserSettingsVariables.h` 각 줄에 있다.

- 렌더 스케일(`gv_renderScale`): 화면 출력 뷰(주 시점 · 화면 사각형)의 내부 해상도에 곱하고 Present 가 출력 크기로 키운다(`RenderViewCollector::applyUserRenderScale`). 렌더 텍스처 뷰와 정수 업스케일 뷰는 곱하지 않는다.
  키우는 필터는 업스케일러(`gv_upscaler`)다 — off 선형, spatial FSR 1(`FrameRendererUpscale.cpp`, [ThirdPartyGaps](ThirdPartyGaps.md) F2). 업스케일러가 배율을 정하지 않는다(배율 항목의 `enabledWhen` 을 뺐다).
- 시야 거리(`gv_viewDistanceScale`): 원근 카메라의 먼 평면에 곱한다(`CameraComponent::getProjectionMatrix`) — 절두체 컬링이 같은 평면으로 자른다.
- 후처리 품질(`gv_postQuality`): 파이프라인 패스의 `_minPostQuality` 보다 낮으면 후처리를 끈 뷰와 같은 길로 건너뛴다(`FrameRenderer::executePass`). 건너뛸 수 있는 타입(`kSkipsWithoutPostProcess`)만 받는다(검증). 디퍼드: SSAO 2 · PostBloom 1.
- 텍스처 품질(`gv_textureMipDrop`, 스키마 선택지의 `targetValue`): 읽을 때 위쪽 밉을 버린다(`Texture2D::computeMipDrop`, 밉 하나뿐인 텍스처 · 64 미만 · 블록 배수가 아닌 크기는 줄이지 않는다).
- 이펙트 품질(`gv_effectsSpawnScale`, `targetValue`, 정의는 Vfx 층 `VfxComponent.cpp`): 효과를 읽을 때 생성률 · 버스트 수에 곱한다(`VfxCPUSimulation` 생성자 인자).
- 색각 보정 톤맵 쪽(`gv_colorVisionMode`): C++ 는 화면 · 호스트 출력 뷰의 `g_CameraFrame.z` 에 방식을 싣는다(렌더 텍스처는 0). 셰이더는 U3.
- 머리 흔들림: Shooter3D(`arena.scene.xml`) · VoxelCraft(`island.scene.xml`) 1인칭 카메라에 `_headBob` 진폭을 넣었다.

## U3 셰이더 쿠킹 — 모션 블러와 톤맵 색각 보정

셰이더를 바꾸는 몫은 커밋 훅(`CheckShaderStamps`)이 쿠킹 없이 막아 패치로 남겼다(작업자 보고에 경로가 있다). 패치 내용:

- `RenderPassType::MotionBlur`(파이프라인 타입, `isPipelinePassType` 상한) · 타입 표 줄(원본 · 깊이 입력, `kSkipsWithoutPostProcess`) · `EngineDefaultAssets::_shaderMotionBlur` + `enginedefaultassets.xml` · 새 `motionblur.hlsl`(깊이 → 월드 → 지난 뷰-투영, 8 표본, 길이 상한).
- PassCB `g_PrevViewProj`(`binding.hlsli`, `PassConstantNames::_prevViewProj`) — `ViewTarget::_prevViewProj` · `_prevViewID` · `_bHasPrevViewProj` 를 `applyViewProjection` 이 뷰마다 갱신한다(처음 · 다른 카메라 · 컷은 이번 것). PassCB 는 약 700 바이트로 `PassConstantRing::kSlotBytes` 768 안이다.
- `gv_motionBlur` 를 끄면 `executePass` 가 후처리 품질로 건너뛸 때와 같은 길로 보낸다. 디퍼드 파이프라인에 `MotionBlur` 패스(TaaColor + SceneDepth → MotionBlurColor, `_minPostQuality` 1)를 Tonemap 앞에 둔다.
- `postgrade.hlsli` 가 `colorvision.hlsli` 를 include 해 `swApplyCameraGrade` 안에서 `g_CameraFrame.z` 로 보정한다(tonemap.hlsl · postchain.hlsl 공통).

적용 순서: 패치를 적용하고 빌드한 뒤 `App.exe --cook-shaders` 로 네 백엔드를 다시 쿠킹하고(PassCB 가 바뀌어 모든 셰이더가 낡는다) `ShaderCookRequestTest` 가 0 이 될 때까지 지운 다음 바이너리 · `cook.stamp` · `reflection.manifest` 와 함께 커밋한다.

## U4 화면 확인과 남은 것

- `gpu-verifier` 로 네 백엔드 캡처: 렌더 스케일 0.5 에서 장면이 흐려지되 UI 는 그대로, 시야 거리 0.25 에서 먼 물체가 잘림, 후처리 low 에서 SSAO · 블룸이 빠짐, 텍스처 low 에서 흐려짐(재시작),
  이펙트 low 에서 입자 수 감소, 모션 블러가 카메라를 돌릴 때만 흐림(서 있으면 원본), 색각 보정이 장면과 UI 에 한 번씩만 걸림(렌더 텍스처 모니터가 두 번 보정되지 않음).
- 머리 흔들림 진폭은 데이터라 화면을 보고 고친다(Shooter3D 0.03 · 0.015, VoxelCraft 0.025 · 0.012).
- 포워드 파이프라인(`forwardpipelinestaged.xml` 의 블룸)에는 `_minPostQuality` 를 아직 두지 않았다. 움직이는 물체의 속도 버퍼(오브젝트 모션 블러)는 없다 — 지난 프레임 월드 행렬을 GPUScene 이 가져야 한다.
- X11 `setDisplayMode`(EWMH 전체 화면)는 리눅스 실기 미확인.
