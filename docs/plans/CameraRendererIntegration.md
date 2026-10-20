# 카메라의 렌더러 쪽 연결과 다중 뷰 검증

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: D4-5 · 진행: 1/4

프리셋 · 블렌드 스택 · 디렉터 · 모드 · 다중 뷰 · 시퀀서 카메라 신호 · 렌즈 값 · 돌리 레일, 다중 뷰의 화면 크기 보임 판정과 뷰 수 · 픽셀 예산(`RenderViewCollector` · `RenderViewScheduler` 헤더), 에디터 게임 뷰의 화면 사각형 뷰 시나리오(`screenrectview`)는 들어갔다(`Source/GameFramework/` "카메라" 절).
카메라별 후처리 값은 렌더러가 뷰마다 읽는다 — `CameraPostProcessValues`(엔진, `Engine/Object/Component/CameraPostProcessValues.h`)를 카메라 컴포넌트가 갖고, 수집기가 `RenderViewSettings::_postProcess` 에 싣고, `FrameRenderer::applyViewPassConstants` 가 `g_CameraGrade` · `g_CameraFrame` · 블룸 세기로 채우며, `postgrade.hlsli` 가 톤맵 앞뒤에서 적용한다.
남은 단위는 아래 셋이다. 단위가 끝나면 그 절을 지우고 교훈은 코드의 헤더 주석으로 옮긴다.

## 1. 단위

| 단위 | 내용 | 선행 |
|---|---|---|
| S2 | 피사계 심도 패스 | 없음 |
| S3 | 컷 신호를 시간 누적 효과에 잇기 | 렌더러의 모션 벡터 · 자동 노출 · 모션 블러 |
| S4 | 다중 뷰 보임의 가려짐 | HZB 가림 컬링([FeatureQueue](Candidates-gameplay.md#featurequeue) "중간" 목록) |

### S2 피사계 심도 패스

- 렌즈 값(`CameraPose::_focusDistance` · `_aperture`)은 포즈가 나르기만 하고 카메라 컴포넌트에 쓰지 않는다. 후처리 값과 같은 길로 간다: 카메라 컴포넌트 프로퍼티 → `RenderViewSettings` → 패스 상수.
- 새 패스 종류 `DepthOfField`(`RenderPassType`, `RenderPassTypeInfo` 표, 엔진 기본 셰이더)를 TAA 와 Tonemap 사이에 둔다. 입력은 HDR 색과 `SceneDepth`, 출력은 같은 형식의 색이다. 네 파이프라인 XML(디퍼드 · 포워드 · 포워드 단계 · 프리패스)에 넣는다.
- 착란원(CoC)은 초점 거리와 조리개(f 값)에서 픽셀 반지름으로 구하고 상한을 둔다. 조리개가 0 이거나 패스를 끈 뷰(`_bPostProcess` 끔, 표면 값 보기 모드)는 원본을 넘긴다(값 선택, early-return 아님).
- 확인: 네 백엔드 스크린샷(초점 앞 · 뒤가 흐리고 초점면이 선명), 골든 이미지를 다시 찍을 범위, `TestRenderPassGPU` 의 패스 표 테스트.

### S3 컷 신호를 시간 누적 효과에 잇기

- 컷 신호는 카메라 쪽이 끝났다(디렉터 `consumeCut` → `CameraComponent::markCut` → `RenderViewSettings::_bCut`, 테스트 `CameraModeTest.DirectorComponentMarksTheCameraCutOnce` · `RenderViewCollectorTest.CameraPostProcessAndCutReachTheViewSettings`).
- 지금 `_bCut` 은 TAA 기록만 리셋한다. 렌더러에 모션 벡터 · 자동 노출 · 모션 블러가 생기면 같은 신호로 모션 벡터 0 · 노출 즉시 맞춤 · 모션 블러 끔을 붙인다.
- 대기 뷰 신호(`CameraManagerComponent::setPendingView`)를 듣는 쪽은 [PreloadAndLod](Candidates-gameplay.md#preloadandlod).

### S4 다중 뷰 보임의 가려짐

- 렌더러에 가림 컬링 결과가 없다. HZB 가림 컬링이 들어오면 모니터가 지난 프레임에 그려졌는지(언리얼 `WasRecentlyRendered`)로 판정을 바꾼다(`RenderViewCollector` 헤더).

## 2. 끝난 단위의 확인 목록

S1(카메라별 후처리 값)은 빌드 · 화면 확인 전이다. 통합 때 아래를 돌린다.

- 셰이더 재베이크(`App.exe --cook-shaders`) — `binding.hlsli` 의 PassCB 에 `g_CameraGrade` · `g_CameraFrame` 이 늘었다. 굽기 전에는 `ShaderBindingValidatorTest.EveryBoundNameIsInCookedReflection` 이 두 이름을 모른다고 실패한다.
- `EngineTest --test_filter=RenderViewCollectorTest.*`, 카메라 테스트(`CameraModeTest.*` · `CameraBlendTest.*`).
- 골든 이미지 테스트 — 중립 값이면 결과가 같아야 한다(채도 `lerp` · 대비 식의 부동소수 오차만).
- 네 백엔드 스크린샷: 프리셋 `<PostProcess exposure · saturation · vignette · letterbox>` 를 준 카메라(디퍼드 `tonemap.hlsl`, 포워드 `postchain.hlsl` 의 `SW_POST_TONEMAP`). 후처리를 끈 CCTV 뷰는 값이 걸리지 않아야 한다.
