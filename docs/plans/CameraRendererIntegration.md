# 카메라의 렌더러 쪽 연결과 다중 뷰 검증

> 상태: 측정대기 · 선행: 없음 · 영역: gameplay · 옛번호: D4-5 · 진행: 4/4

프리셋 · 블렌드 스택 · 디렉터 · 모드 · 다중 뷰 · 시퀀서 카메라 신호 · 렌즈 값 · 돌리 레일, 다중 뷰의 화면 크기 보임 판정과 뷰 수 · 픽셀 예산(`RenderViewCollector` · `RenderViewScheduler` 헤더), 에디터 게임 뷰의 화면 사각형 뷰 시나리오(`screenrectview`)는 들어갔다(`Source/GameFramework/` "카메라" 절).
카메라별 후처리 값은 렌더러가 뷰마다 읽는다 — `CameraPostProcessValues`(엔진, `Engine/Object/Component/CameraPostProcessValues.h`)를 카메라 컴포넌트가 갖고, 수집기가 `RenderViewSettings::_postProcess` 에 싣고, `FrameRenderer::applyViewPassConstants` 가 `g_CameraGrade` · `g_CameraFrame` · 블룸 세기로 채우며, `postgrade.hlsli` 가 톤맵 앞뒤에서 적용한다.
피사계 심도(패스 종류 `DepthOfField`, `CameraDepthOfFieldValues` 머리 주석)와 렌즈 시프트(`CameraComponent::getLensShift` — 투영 행렬이 비대칭이 된다)도 같은 길로 들어갔다.
자동 노출(카메라의 EV 범위 · 적응 속도, `FrameRendererExposure.cpp` 머리 주석)과 컷 신호(S3)도 들어갔다 — 컷은 뷰마다 한 곳(`FrameRenderer::beginViewHistory` → `ViewTarget::_bHistoryReset`)에서 정하고 TAA · 모션 블러 · 자동 노출 · 가림 판정이 그것 하나를 본다.
S4(다중 뷰 보임의 가려짐)도 코드는 들어갔다. 렌더러가 주 시점의 가림 타일(`FrameRendererOcclusion.cpp`)을 GPU 에서 CPU 로 몇 프레임 늦게 되읽고(`AsyncBufferReadback`),
수집기가 모니터의 경계 구를 그 사본으로 가려졌는지 본다(`OcclusionTileQuery::isSphereOccluded` — gpucull.hlsl 의 `isOccluded` 와 같은 식). 남은 것은 아래 확인뿐이다.

## 1. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| S1 ~ S3 | 카메라별 후처리 값 · 피사계 심도와 렌즈 시프트 · 컷 신호 | 끝(확인은 2절) |
| S4 | 다중 뷰 보임의 가려짐 — RHI 비동기 되읽기(네 백엔드) · 가림 타일 사본 · 수집기의 가려짐 판정 | 코드 끝, 확인 대기: 매니페스트 id `async-buffer-readback-gpu`, `occlusion-snapshot-tests`, `occlusion-culling-visibility-run` |

### S4 설계 (들어간 것)

- 선행이던 RHI 비동기 되읽기: `IRHIResourceFactory::createReadbackBuffer` · `readReadbackBuffer`, `IRHICommandList::copyBufferToReadback`, `RHIBufferState::CopySource`(RHI ABI v33). 네 백엔드 구현은 DX11 스테이징 구조화 버퍼, DX12 읽기 힙, Vulkan 호스트 보임 버퍼(`vkCmdCopyBuffer`), GL `glCopyBufferSubData`. 끝났는지 알리는 새 펜스 API 는 만들지 않고 기존 `IRHIDevice::enqueueGPURelease` 콜백 안에서 읽는다(읽기는 기다리지 않는다).
- 그 위의 고리 `AsyncBufferReadback`(읽기 버퍼 4 개를 돌려 쓰고, 모두 쓰이는 중이면 그 프레임은 건너뛴다)과 `FrameRenderer::requestOcclusionReadback`(컴퓨트 프리패스에서 `recordOcclusionTiles` 직후 복사 — 요청 때의 뷰-투영 · 격자는 요청 번호로 짝지어 둔다). 기록을 버리는 프레임(처음 · 컷)의 타일은 사본으로 올리지 않고 우편함을 비운다.
- 게임 스레드 쪽: `OcclusionTileMailbox`(`FrameRenderer::getOcclusionSnapshot`)의 가장 최근 사본을 패킷 만들기(`RenderPacketBuilder`)와 직접 경로(`FrameRenderer::execute( scene )`)가 `RenderViewCollector::collectExtraViews` 에 넘긴다. 사본이 없으면(`gv_occlusionCulling` 꺼짐 — 기본) 판정은 그대로 화면 크기뿐이다. 화면 사각형 뷰는 사본이 없어 가려짐을 보지 않는다.
- 처음 설계의 "패킷에 보임 질의를 싣고 작은 컴퓨트로 판정" 대신 타일 사본을 CPU 가 읽는 길을 택했다. 타일 버퍼가 이미 있고 작아(1080p 에서 8,160 float) 되읽기가 싸며, 새 셰이더(쿠킹 바이너리 동반 커밋)가 필요 없고, 판정 식을 CPU 단위 테스트로 직접 잴 수 있다.

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

S3(컷 신호)와 그 선행 렌더러 기능(자동 노출 · FSR 1 · 가림 판정)은 브랜치 `wt/gfxrender` 다. 셰이더(`autoexposure.hlsl` · `fsr1.hlsl` · `occlusiontiles.hlsl` 새 파일, `binding.hlsli` 의 `g_AutoExposureIndex` · 에뮬 슬롯 2, `postgrade.hlsli`, `gpucull.hlsl`)는 커밋에 없고 패치로 넘겼다.

- 패치를 적용하고 `App.exe --cook-shaders` 로 모든 셰이더를 다시 굽는다(PassCB 가 늘었다). 그 전에는 개발 빌드가 `enginedefaultassets.xml` 의 `_shaderAutoExposure` · `_shaderUpscale` · `_shaderOcclusionTiles` 파일이 없어 시작에서 멈춘다.
- `EngineTest --test_filter=RenderPassGPUTest.AutoExposureMeasuresTheSceneAndSnapsOnCut:RenderPassGPUTest.SpatialUpscalerReplacesTheLinearPresent:RenderPassGPUTest.OcclusionCullingUsesTheLastFrameDepth:RenderPassGPUTest.CameraCutResetsTaaHistory` 와 `ShaderCookRequestTest.*`(표 단계가 이제 패스 define 을 붙여 쿠킹한다 — 낡은 바이너리 정리).
