# 카메라의 렌더러 쪽 연결과 다중 뷰 검증

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: D4-5 · 진행: 2/4

프리셋 · 블렌드 스택 · 디렉터 · 모드 · 다중 뷰 · 시퀀서 카메라 신호 · 렌즈 값 · 돌리 레일, 다중 뷰의 화면 크기 보임 판정과 뷰 수 · 픽셀 예산(`RenderViewCollector` · `RenderViewScheduler` 헤더), 에디터 게임 뷰의 화면 사각형 뷰 시나리오(`screenrectview`)는 들어갔다(`Source/GameFramework/` "카메라" 절).
카메라별 후처리 값은 렌더러가 뷰마다 읽는다 — `CameraPostProcessValues`(엔진, `Engine/Object/Component/CameraPostProcessValues.h`)를 카메라 컴포넌트가 갖고, 수집기가 `RenderViewSettings::_postProcess` 에 싣고, `FrameRenderer::applyViewPassConstants` 가 `g_CameraGrade` · `g_CameraFrame` · 블룸 세기로 채우며, `postgrade.hlsli` 가 톤맵 앞뒤에서 적용한다.
피사계 심도(패스 종류 `DepthOfField`, `CameraDepthOfFieldValues` 머리 주석)와 렌즈 시프트(`CameraComponent::getLensShift` — 투영 행렬이 비대칭이 된다)도 같은 길로 들어갔다.
남은 단위는 아래 둘이다. 단위가 끝나면 그 절을 지우고 교훈은 코드의 헤더 주석으로 옮긴다.

## 1. 단위

| 단위 | 내용 | 선행 |
|---|---|---|
| S3 | 컷 신호를 시간 누적 효과에 잇기 | 렌더러의 모션 벡터 · 자동 노출 · 모션 블러 |
| S4 | 다중 뷰 보임의 가려짐 | HZB 가림 컬링([FeatureQueue](Candidates-gameplay.md#featurequeue) "중간" 목록) |

### S3 컷 신호를 시간 누적 효과에 잇기

- 컷 신호는 카메라 쪽이 끝났다(디렉터 `consumeCut` → `CameraComponent::markCut` → `RenderViewSettings::_bCut`, 테스트 `CameraModeTest.DirectorComponentMarksTheCameraCutOnce` · `RenderViewCollectorTest.CameraPostProcessAndCutReachTheViewSettings`).
- 지금 `_bCut` 은 TAA 기록만 리셋한다. 렌더러에 모션 벡터 · 자동 노출 · 모션 블러가 없다(2026-10-22 `Source/Engine` 검색). 셋은 각각 렌더러 기능이라 FeatureQueue 에서 착수한다.
- 짧은 설계(기능이 들어올 때 붙일 자리): 시간 누적 상태는 모두 `FrameRenderer::ViewTarget` 의 뷰마다 기록(TAA 기록과 같은 자리)에 두고, 컷 프레임에는 TAA 의 컷 처리와 같은 분기 하나에서 모션 벡터 0 · 노출 기록을 이번 프레임 측정값으로 덮기 · 모션 블러 세기 0 을 함께 고른다. 기능마다 컷을 따로 읽지 않는다.
- 대기 뷰 신호(`CameraManagerComponent::setPendingView`)를 듣는 쪽은 [PreloadAndLod](PreloadAndLod.md).

### S4 다중 뷰 보임의 가려짐

- 렌더러에 가림 컬링 결과가 없다. HZB 가림 컬링이 들어오면 모니터가 지난 프레임에 그려졌는지(언리얼 `WasRecentlyRendered`)로 판정을 바꾼다(`RenderViewCollector` 헤더).
- 짧은 설계: (1) 장면 깊이에서 뷰마다 HZB(최대 깊이 밉 사슬, 컴퓨트)를 만든다. (2) `gpucull.hlsl` 이 지난 프레임 HZB 와 지난 뷰-투영으로 인스턴스 경계 구를 검사한다. (3) 살아남은 인스턴스의 "마지막으로 그린 프레임"을 인스턴스 버퍼 옆 UAV 에 쓰고 몇 프레임 늦게 되읽어 `MeshComponent` 쪽 `wasRecentlyRendered( frames )` 로 낸다. (4) 수집기 `computeVisibleScreenSize` 가 보임 오브젝트의 메시에 그것을 묻는다. 네 백엔드 GPU 확인이 필요한 일이라 빌드 없이 쓰지 않았다.

## 2. 끝난 단위의 확인 목록

S1(카메라별 후처리 값)은 빌드 · 화면 확인 전이다. 통합 때 아래를 돌린다.

- 셰이더 소스(`binding.hlsli` 의 PassCB 에 `g_CameraGrade` · `g_CameraFrame`, 새 `postgrade.hlsli`, `tonemap.hlsl` · `postchain.hlsl`)는 쿠킹 바이너리와 함께 커밋해야 한다(`CheckShaderStamps`). 빌드한 뒤 `App.exe --cook-shaders` 로 굽고 소스와 바이너리를 함께 커밋한다.
- `EngineTest --test_filter=RenderViewCollectorTest.*`, 카메라 테스트(`CameraModeTest.*` · `CameraBlendTest.*`).
- 골든 이미지 테스트 — 중립 값이면 결과가 같아야 한다(채도 `lerp` · 대비 식의 부동소수 오차만).
- 네 백엔드 스크린샷: 프리셋 `<PostProcess exposure · saturation · vignette · letterbox>` 를 준 카메라(디퍼드 `tonemap.hlsl`, 포워드 `postchain.hlsl` 의 `SW_POST_TONEMAP`). 후처리를 끈 CCTV 뷰는 값이 걸리지 않아야 한다.

S2(피사계 심도)와 렌즈 시프트는 빌드 · 화면 확인 전이다(브랜치 `wt/camdof`).

- 셰이더 소스(`binding.hlsli` 의 `g_CameraLens`, 새 `depthoffield.hlsl`)는 커밋에 없고 패치로 넘겼다. 패치를 적용하고 `App.exe --cook-shaders` 로 모든 셰이더를 다시 구워(PassCB 가 늘었다) 소스와 바이너리를 함께 커밋한다. 그 전에는 개발 빌드가 `enginedefaultassets.xml` 의 `_shaderDepthOfField` 파일이 없어 시작에서 멈춘다.
- `EngineTest --test_filter=RenderPassTest.*:ShaderCookRequestTest.*:RenderViewCollectorTest.*:CameraModeTest.*:CameraPresetTest.*`.
- `RenderPassGPUTest.FusedPostChainMatchesStaged` · `DepthPrepassRendersTheSameImage` 와 골든 이미지 — 조리개 기본값이 0(핀홀)이라 피사계 심도 패스는 원본을 그대로 내야 한다(그림이 같아야 한다). 포워드는 전체화면 왕복이 하나 늘었다(720p 약 57 us) — 재어 비싸면 꺼진 뷰에서 패스를 건너뛰는 실행을 정한다.
- 네 백엔드 스크린샷: Shooter3D `-gv_cameraPreset=dolly`(초점 8 m · f/2.8)에서 초점면 앞뒤가 흐리고 초점면이 선명한지, 후처리를 끈 CCTV 뷰는 선명한지. 얇은 렌즈 값은 흐림이 옅을 수 있다 — 보이지 않으면 프리셋 조리개를 낮춰 본다.
- 렌즈 시프트: 프리셋 `lensShift="0,0.1"` 로 세로선이 모이지 않은 채 화면이 위로 옮겨지는지, 피킹 · 컬링이 같이 옮겨지는지.
- 포워드 툰(`forwardtoonpipeline.xml`)과 2D 파이프라인에는 넣지 않았다(계획이 정한 넷만).
