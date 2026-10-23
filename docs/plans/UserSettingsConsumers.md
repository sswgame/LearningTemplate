# 사용자 설정 값을 읽는 쪽 잇기

> 상태: 측정대기 · 선행: 없음 · 영역: gameplay · 옛번호: D4-2 · 진행: 4/4

옵션 메뉴 백엔드(`Source/Engine/UserSettings/`, 메뉴는 `Engine/UI/Screen/OptionsMenuScreen`)의 그래픽 · 접근성 값에 읽는 쪽을 이었다. 읽는 곳은 `UserSettingsVariables.h` 각 줄에 있다.

- 렌더 스케일(`gv_renderScale`): 화면 출력 뷰(주 시점 · 화면 사각형)의 내부 해상도에 곱하고 Present 가 출력 크기로 키운다(`RenderViewCollector::applyUserRenderScale`). 렌더 텍스처 뷰와 정수 업스케일 뷰는 곱하지 않는다.
  키우는 필터는 업스케일러(`gv_upscaler`)다 — off 선형, spatial FSR 1(`FrameRendererUpscale.cpp`, [ThirdPartyGaps](ThirdPartyGaps.md) F2). 업스케일러가 배율을 정하지 않는다(배율 항목의 `enabledWhen` 을 뺐다).
- 시야 거리(`gv_viewDistanceScale`): 원근 카메라의 먼 평면에 곱한다(`CameraComponent::getProjectionMatrix`) — 절두체 컬링이 같은 평면으로 자른다.
- 후처리 품질(`gv_postQuality`): 파이프라인 패스의 `_minPostQuality` 보다 낮으면 후처리를 끈 뷰와 같은 길로 건너뛴다(`FrameRenderer::executePass`). 건너뛸 수 있는 타입(`kSkipsWithoutPostProcess`)만 받는다(검증). 디퍼드: SSAO 2 · PostBloom 1.
- 텍스처 품질(`gv_textureMipDrop`, 스키마 선택지의 `targetValue`): 읽을 때 위쪽 밉을 버린다(`Texture2D::computeMipDrop`, 밉 하나뿐인 텍스처 · 64 미만 · 블록 배수가 아닌 크기는 줄이지 않는다).
- 이펙트 품질(`gv_effectsSpawnScale`, `targetValue`, 정의는 Vfx 층 `VfxComponent.cpp`): 효과를 읽을 때 생성률 · 버스트 수에 곱한다(`VfxCPUSimulation` 생성자 인자).
- 색각 보정 톤맵 쪽(`gv_colorVisionMode`): C++ 는 화면 · 호스트 출력 뷰의 `g_CameraFrame.z` 에 방식을 싣는다(렌더 텍스처는 0). 셰이더는 U3.
- 머리 흔들림: Shooter3D(`arena.scene.xml`) · VoxelCraft(`island.scene.xml`) 1인칭 카메라에 `_headBob` 진폭을 넣었다.

## U3 모션 블러와 톤맵 색각 보정 셰이더 (들어감)

셰이더(`motionblur.hlsl` · `colorvision.hlsli` · `postgrade.hlsli` · `taa.hlsl`)와 쿠킹 바이너리는 트리에 들어 있다(셰이더 병합 · 재쿠킹 끝).

- `RenderPassType::MotionBlur`(파이프라인 타입, 원본 · 깊이 입력, `kSkipsWithoutPostProcess`) · `EngineDefaultAssets::_shaderMotionBlur` · PassCB `g_PrevViewProj`(`ViewTarget::_prevViewProj` · `_prevViewID` · `_bHasPrevViewProj` 를 `applyViewProjection` 이 뷰마다 갱신, 처음 · 다른 카메라 · 컷은 이번 것).
- `gv_motionBlur` 를 끄면 `executePass` 가 후처리 품질로 건너뛸 때와 같은 길로 보낸다. 디퍼드에 `MotionBlur` 패스(TaaColor + SceneDepth → MotionBlurColor, `_minPostQuality` 1)를 Tonemap 앞에 둔다. 포워드 파이프라인 넷(forward · staged · prepass · toon)에도 있다 — 피사계 심도 뒤(toon 은 장면 색 뒤) · 후처리 체인 앞, `_minPostQuality` 1.
- `postgrade.hlsli` 가 `colorvision.hlsli` 를 include 해 `CameraGrade::apply` 안에서 `g_CameraFrame.z` 로 보정한다(tonemap.hlsl · postchain.hlsl 공통). C++ 는 화면 · 호스트 출력 뷰의 `g_CameraFrame.z` 에 방식을 싣는다(렌더 텍스처는 0).
- TAA 는 기록을 깊이로 재투영하고 이번 원본의 3x3 이웃으로 잘라 섞는다(계약에 `SceneDepth` 필수, `RenderPassGPUTest.TaaLeavesNoGhostOfAMovedObject`).

## U4 화면 확인과 남은 것

- 확인 대기: 매니페스트 id `usersettings-scenario`, `usersettings-visual-review`, `usersettings-render-tests`.
- `usersettings-scenario`(`game/empty/automation/useroptions.scenario.xml`, 무대 `maps/rendertest3d` 디퍼드)는 렌더 스케일 0.5 · 시야 거리 0.05 · 후처리 품질 0 · 색각 보정 1 을 하나씩 바꿔 사진을 기준과 견준다. 문턱은 측정 전 추정이라 첫 캡처로 맞춘다.
- `usersettings-visual-review`(사람 눈): 렌더 스케일 0.5 에서 장면만 흐리고 UI 는 그대로인지, 텍스처 품질 low 에서 흐려지는지(재시작), 이펙트 품질 low 에서 입자 수가 주는지, 색각 보정이 장면과 UI 에 한 번씩만 걸리는지(렌더 텍스처 모니터가 두 번 보정되지 않음). 근거: 이 무대에는 UI · 렌더 텍스처 모니터 · 입자가 없다.
- 모션 블러는 확인함(2026-10-22): `game/empty/automation/motionblur.scenario.xml`(디퍼드, 무대 `maps/rendertest3d`)이 카메라를 순간이동으로 옮겨 켬 · 끔 사진을 같은 카메라 · 지난 카메라로 견준다 — 움직인 프레임은 다르고(0.0067) 멈춘 카메라는 같다(0.00003). 네 백엔드가 같은 값이고 `AppScenarioTest`(hostgpu)가 돈다. 포워드 넷의 모션 블러와 TAA 이웃 클램프 화면 확인(카메라를 돌릴 때 앞 그림이 겹치지 않음)은 위 `usersettings-visual-review` 에 더한다.
- 머리 흔들림 진폭은 데이터라 화면을 보고 고친다(Shooter3D 0.03 · 0.015, VoxelCraft 0.025 · 0.012).
- 움직이는 물체의 속도 버퍼(오브젝트 모션 블러)는 없다 — 지난 프레임 월드 행렬을 GPUScene 이 가져야 한다.
- X11 `setDisplayMode`(EWMH 전체 화면)는 리눅스 실기 미확인(리눅스 실기 환경이 없어 풀리지 않는다).
