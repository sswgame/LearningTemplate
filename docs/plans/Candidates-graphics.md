# 그래픽스 후보

> 상태: 후보 · 선행: 없음 · 영역: graphics · 옛번호: C-55, D2-1, D2-2, D2-5.2, D2-6, D2-7, D2-9, D2-10, D2-11, D2-12, D2-14, E-3, E-13 · 진행: 해당없음

이 문서는 그래픽스 · RHI · 셰이더 영역에서 착수 전인 후보의 목록입니다. 착수하면 그 절을 계획 문서로 옮기고 여기서 지웁니다.

## 확인 대기

테스트 필터 · 시나리오로 도는 확인은 검증 묶음(`Test/QA/VerifyBatch.json`)이 돌렸고, 아래는 사람 눈이 필요하거나 전 바이너리를 따로 지어야 해서(`manual`) 묶음이 돌리지 못하는 확인이다. 각 절의 `매니페스트 id` 줄이 항목의 주인이다. 사람이 끝내면 항목과 절을 지운다.

### GraphicsFormatBundleChecks
제목: 그래픽스 형식 묶음의 화면 확인과 Release 비용
옛번호: D2-1, D2-6, D2-7, D2-10
왜 필요한가: HDR 장면 색(포워드 넷의 `SceneColor` R16F) · 툰 · 변형 탄젠트 · Shaded Wireframe · 2D 조명 · VRM 런타임 · 메시 구간의 코드와 테스트(`RenderPassGPUTest`)는 통과했고 2D 쿠키 시나리오 `light2dcookie` 도 통과한다(결정 기록 5-5). 화면과 Release 비용 확인이 남았다.
남은 것:
- 매니페스트 id `graphics-format-visual-checks` — G2 VRM 스펙 샘플 "VRMC_materials_mtoon UV Animation Test" 의 UV 방향, G5 와이어프레임 선이 면 위에 보이고 z-fighting 이 없는지(포워드 · 디퍼드 · 깊이 프리패스 네 백엔드, 바이어스 상수 -4 · 기울기 -1 은 그때 맞춘다), G6 2D 쇼케이스 씬(자유 모양 · 곱하기 · 부드러운 그림자 빛)의 네 백엔드 캡처.
- 매니페스트 id `graphics-format-release-cost` — G1 대역폭(R16F `SceneColor`), G3 모프 풀 48 B 업로드 · 스키닝(`RT.GPUScene`), G6 부드러운 그림자(가림막 검사가 다섯 배라 토막 수백 개에서 GPU 시간)를 Release 로 잰다.
못 하는 이유: 화면 항목은 사람 눈이 판정하고(VRM 샘플 · 선 모양은 기준 그림이 없다), 비용 항목은 R8G8B8A8 시절 `SceneColor` 와 모프 풀 이전 바이너리가 있어야 하는 바이너리 A/B 라 한 명령에 전 바이너리를 줄 길이 없다(`perf-compare` 의 `morphPool` 케이스는 있다). 풀리는 조건: 기준 그림을 정하면 캡처를 시나리오로 옮기고, 이득 판정이 필요 없어지면 비용 항목을 지운다.
함정: avatarsample_d 를 다시 임포트하면(`kVersion` 2 · `SECT`) 구간 메시 폴더가 사라지고 프리팹이 생기므로 `toonshowcase.scene.xml` 의 구간 엔티티 열일곱을 프리팹 인스턴스 하나로 바꾼다. VRoid 텍스처 BC7 재임포트는 Release 로 한다.

### IndirectLightingScreenChecks
제목: 반사 프로브 · 금속도 PBR 의 화면 확인
옛번호: D2-12
왜 필요한가: 라이트 프로브(P0 ~ P3, P6)와 반사 프로브(P4) · 금속도 PBR(P5) · G버퍼 셋째 타깃(D2-6.4)의 코드와 테스트는 통과했고 에디터 굽기 시나리오(`lightprobebake` · `reflectionprobebake`)도 통과한다(결정 기록 5-5). 쿠킹 뒤 화면이 남았다.
남은 것: 매니페스트 id `indirect-lighting-visual-checks` — 반사 프로브 · 금속도 PBR · 셋째 타깃의 쿠킹 뒤 네 백엔드 화면(GL · DX11 에서 에디터가 렌더 스레드의 컨텍스트를 잡는지 포함)과 골든 다시 뜨기(유전체에도 하이라이트가 생겼다).
못 하는 이유: 새 하이라이트가 맞는 그림인지는 사람이 보고 골든을 다시 뜰지 정한다. 풀리는 조건: 사람이 보고 골든(`golden-record`)을 다시 뜨면 이 절과 항목을 지운다.

### HalfResolutionBloomMeasurement
제목: 반해상도 블룸의 전후 GPU 시간
옛번호: D2-9
왜 필요한가: 디퍼드 블룸의 밝은 부분을 반해상도 패스(`BloomDownsample`)로 옮겼다(`ee6fff232`). 후(`GPU.BloomDownsample` + `GPU.PostBloom`)의 p50 이 전(`GPU.PostBloom`)보다 작지 않으면 `deferredpipeline.xml` 에서 `BloomDownsample` 패스 · `BloomBright` 첨부 · `PostBloom` 입력을 지워 되돌린다. 코드(`Bloom::computeBrightDownsampled`)는 포워드 단계 파이프라인이 쓰는 한 둔다.
남은 것: 매니페스트 id `half-resolution-bloom` — `py -3 -m Scripts perf-compare --app <후>/App.exe --app-before <전>/App.exe --cases deferredBloom --runs 5`(전은 `ee6fff232^` 의 Release 전체 빌드).
못 하는 이유: 전 커밋을 따로 지어야 하는 바이너리 A/B 이다. 풀리는 조건: 되돌리지 않기로 하면 이 절과 항목을 지운다.

## 관찰

### PostEffectBypass
제목: 꺼진 후처리 효과가 전 화면 패스와 타깃 비용을 낸다
옛번호: 없음
왜 필요한가: 렌즈 없는 카메라(조리개 0)와 서 있는 카메라에서도 피사계 심도 · 모션 블러가 전 화면을 한 번씩 읽고 써서(셰이더가 원본을 고르거나 원본을 복사) Empty `GPU.Frame` 이 468 → 1089 us 가 됐고, 두 출력 타깃(RGBA16F)이 늘 메모리를 차지했다(GPU 메모리 56.8 → 176.9 MB, `renderbench` Native 1600×900 기준선 커밋 `14c775c55`). 블룸 · 외곽선 · SSAO 도 꺼진 뷰(후처리 끔 · 품질 미달)에서 원본을 복사하거나 셰이더로 옮기거나 타깃을 지웠다.
끝난 것(wt/renderbase): 꺼질 수 있는 효과(`kSkipsWithoutPostProcess`)는 빠지는 방법이 정확히 하나다 — 블룸 · 외곽선 · 피사계 심도 · 모션 블러는 출력 이름이 원본에 이어지고(`kBypassesToSource`, `TransientAttachmentPool::forward`), 원본이 없는 SSAO 는 출력이 없어(`kOmittedWhenOff`, `markAbsent`) 읽는 쪽이 AO 역할을 걸지 않고 셰이더 기본값 1 을 쓴다. 셋업(`FrameRenderer::applyViewPassSelection`, 뷰마다 프레임에 한 번)이 패스를 뒤에서부터 보며 고르고, 읽는 패스가 모두 빠진 풀스크린 패스(블룸이 꺼진 뷰의 BloomDownsample)도 뺀다. 일반 풀스크린 패스만 쓰는 첨부는 풀을 만들 때 만들지 않고 그 패스가 처음 돌 때 만든다. C++ 의 원본 복사 · 셰이더 원본 고르기 · 지우기 길은 지웠다. 디퍼드 OutlineColor 를 RGBA8 → RGBA16F 로 맞췄다 — 외곽선은 톤맵 앞 HDR 색에 그리므로 RGBA8 은 필요 없었고 1 이 넘는 색을 TAA · 톤맵 앞에서 잘랐다. 모션 블러의 "뷰-투영이 지난번과 같으면 끔" 은 셰이더가 카메라 움직임만 쓰기 때문이다(`motionblur.hlsl` 이 깊이와 `g_PrevViewProj` 만 읽고, 렌더러에 물체 속도 버퍼 · 속도 패스가 없다). 로드 검증 3-6(출력 · 원본의 형식 · 나눗수, 출력을 쓰는 패스 하나, 뒤 패스가 원본을 다시 쓰지 않음)과 3-7(SSAO 출력은 선택 입력으로만 읽힘)을 더했다. 주 시점 첨부마다의 크기를 `[RenderTargets]` Info 줄로 남긴다.
재측정(wt/rgprune, 2026-10-27, 코드 읽기): 감사 SB R1 이 말한 패스스루(`FrameRendererPassExecute.cpp` 의 블릿 · `SW_PASS_FLAG_SKIP_POST` 길, 상수의 조리개 0 분기)는 renderbase 가 다 지웠다. 그래프를 뷰 상태 키로 다시 컴파일해 캐시하는 길은 만들지 않았다 — 출력 이름 잇기가 같은 결과(빠진 패스 · 원본을 읽는 소비자)를 컴파일 없이 내고, 결정 5-5 가 기각했다. 남은 틈은 뷰 상태가 바뀌는 프레임의 TAA 기록이었다: 후처리를 끈 프레임의 TAA 는 기록을 쓰지 않는데 다시 켠 첫 프레임이 끄기 전 기록을 섞었다. `beginViewHistory` 가 `_bTaaHistoryReset`(컷이거나 지난 프레임이 기록을 쓰지 않음)을 같은 자리에서 정하고 `executeTaaPass` 가 그것을 본다. 피사계 심도 · 모션 블러는 TAA 뒤라 켜고 꺼도 기록과 무관하다. 테스트: `RenderPassGPUTest.DepthOfFieldToggleForwardsAndKeepsTaaHistory`(조리개 0 → 1.4 → 0 에서 출력이 TaaColor 를 가리키다 자기 타깃에 흐린 그림을 그리고 다시 가리킴, TaaColor 는 그대로) · `PostProcessReenableResetsTaaHistory`(껐다 켠 프레임의 TaaColor = 원본). 측정 기대치(통합이 볼 것): Empty `GPU.Frame` 은 468 us 근처, `GPUPassTimeline` 의 DepthOfField · MotionBlur 칸은 렌즈 없는 서 있는 카메라에서 빈다(패스가 기록되지 않는다), `gpu_memory_mb` 는 두 RGBA16F 타깃(1600×900 에서 23 MB)만큼 준다.
메모리 추정(실행 안 함): 1600×900 RGBA16F 하나가 11.5 MB 라 포워드에서 두 타깃을 빼면 23 MB 가 준다. 같은 기간에 장면 색이 RGBA8 → RGBA16F(+5.8 MB), 깊이 히스토리 R32(+5.8 MB)가 더해졌다. 디퍼드는 OutlineColor 가 +5.8 MB 늘고 꺼진 뷰에서 블룸 · 외곽선 · SSAO · BloomDownsample 타깃이 빠진다. 나머지 약 85 MB 는 렌더 타깃 목록 밖(버퍼 · 프로브 · 볼륨 안개 등)일 수 있어 `[Profile] GPU memory by kind` 의 종류별 줄과 `[RenderTargets]` 줄을 함께 보고 가른다.
남은 것(통합): 다섯 셰이더 진입점의 `SW_PASS_FLAG_SKIP_POST` 분기는 main 에서 이미 지워졌다(2026-10-28 `Resource/engine/shaders` 읽기: `bSkipPost` 는 `postchain.hlsl` · `tonemap.hlsl` 에만 남는다 — Present 쪽이라 남는다). 쿠킹 바이너리가 이 변경과 맞는지는 `App --cook-shaders` 와 `ShaderCookRequestTest` 가 알리는 낡은 바이너리로 통합이 본다. 빌드 · `RenderPassTest` · `TransientAttachmentPoolTest` · `RenderPassGPUTest` · 모션 블러 시나리오(`game/empty/automation/motionblur.scenario.xml`, 움직인 프레임이 여전히 달라야 한다) 네 백엔드. 디퍼드 골든은 OutlineColor HDR 화로 밝은 곳이 달라지므로 바뀐 그림을 보고 다시 뜬다. 그다음 기준을 새로 뜬다 — 숨기기 위한 재기록이 아니라 이 변경을 넣은 뒤의 값이다. `perf-frame` 은 `py -3 -m Scripts perf --app <app> --record`, `load-baseline-suite` 는 `py -3 -m Scripts load suite baseline --runs 3 --record`, `load-auto-render-scale` 은 LowEnd `renderbench` 를 `--app-arg=-gv_autoRenderScale=1` 로 `--record`. 뜨기 전에 `GPU.Frame` 이 468 us 근처로 돌아왔는지와 `gpu_memory_mb` 를 종류별로 나눈 표를 결과 문서에 남긴다(돌아오지 않은 몫은 원인을 적고 뜬다).
TAA 패스스루(wt/taabypass): 후처리를 끈 뷰(CCTV)의 TAA 가 원본을 TaaColor 로 복사하던 길(형식이 같으면 블릿, 다르면 기록 슬롯에 원본을 건 TAA 셰이더 — 전 화면 한 번)을 지웠다. TAA 에 `kSkipsWithoutPostProcess` · `kBypassesToSource` 를 붙여 셋업의 패스 고르기(`isSelectable` 이 kBypassesToSource 패스도 받는다)가 출력 이름을 원본에 잇고 `executeTaaPass` 는 기록하지 않는다. 검증 3-6 이 TAA 에도 걸린다(디퍼드 OutlineColor · TaaColor 는 둘 다 RGBA16F). 컬러 출력이 없는 패스는 3-1 · 3-2 가 이미 알리므로 3-6 은 건너뛴다. TaaColor 타깃과 기록은 셋업이 기록 형식으로 읽으므로 계속 만든다(꺼진 뷰에서도 메모리는 남는다). 다시 켠 첫 프레임은 `_bTaaHistoryReset` 이 기록을 버린다. TAA 가 도는지는 패스 고르기 결과 하나다 — `applyViewPassSelection` 이 `_bTaaHistoryWritten` 을 남기고(후처리 끔 · `_minPostQuality` · 읽는 쪽 없음 모두 같은 판정), 실행은 출력이 이어졌는지만 본다.
꺼진 효과 타깃 해제(wt/taabypass): 처음 돌 때 만드는 효과 타깃은 쓰는 패스가 이어서 `TransientAttachmentPool::kIdleFramesBeforeRelease`(120, 60 Hz 에서 약 2 초) 프레임 빠지면 셋업이 놓고(`releaseIfIdle`), 다시 켜지면 만든다. 모션 블러가 카메라가 서고 움직일 때마다 켜지고 꺼져도 할당을 되풀이하지 않는다. 놓은 자원은 네 백엔드의 기존 해제 큐가 앞 프레임들이 끝난 뒤에 지운다(DX12 · Vulkan 은 GPU 펜스, DX11 · GL 은 `kGPUReleaseFrameLatency` — 새 큐를 만들지 않았다). 테스트: `TransientAttachmentPoolTest.IdleTargetsReleaseOnlyAfterTheFrameCount`, `RenderPassGPUTest.IdleEffectTargetIsReleasedAfterFramesAndRemade`. TAA 출력 · 지오메트리 출력처럼 셋업이 늘 있다고 보고 읽는 타깃은 놓지 않는다.
골든 · 기준 훑기(wt/gfx-baseline, 2026-10-28, 코드 · git 읽기): 벤치 골든 8 장을 잘못 갱신한 원인(`862ad03f3`)은 빈 Saved 의 무인 실행이 사양으로 프리셋 'low' 를 고르던 것이고, 그 자동 선택은 `d07ef6016`(10-04)부터 `a0521e79a`(10-27)까지 있었다. 그 사이에 다시 뜬 다른 그림 · 기준은 같은 의심이 있다 — `Test/QA/Golden/{NileCity,Shooter3D,StarSkirmish}`(`d26a92a89`, 10-22) · `Test/QA/Golden/Empty`(`04c17923f`, 10-26) 네 백엔드, `Test/AppTest/Golden/shippingframe_dx12`(`6f2d663f9`, 10-25), `Test/Load/Baselines.json`(`4fcef54b5`, 10-15). `Scripts/qa/GoldenImages.py` · `PerfRegression.py` 는 프리셋을 정하지 않았고(`-unattended` · `-scenario` 도 없어 `a0521e79a` 의 무인 판정에도 걸리지 않았다), 뜰 때 Saved 가 비었는지 알 수 없다. 구조로 닫았다: `Test/QA/Games.json` 의 `quality`(high — 스키마 기본값과 같아 그림이 바뀌지 않는다)를 `common/QualityPreset.py` 가 사용자 설정 파일로 써 `-gv_userSettingsFile` 로 건네고, 골든 `capture.quality` · perf 기준 `quality` · Load 기준선 `quality` 에 적어 견줄 때 다르거나 없으면 거절한다(그래서 기존 골든 · 기준은 다시 뜰 때까지 "프리셋을 모른다" 로 진다). 새 러너가 틈을 다시 열지 못하게 App 을 띄우는 공통 입구 `AppRun.runApp` 이 `settings=SettingsIsolation.*` 를 필수 키워드로 받고 인자와 어긋나면 띄우기 전에 ValueError 다(`Soak` · `SceneLoadBench` · `PerfCompare` · `RunBackendSmoke` · `RunDestructionNetPair` 클라이언트는 설정 파일, 테스트 실행 파일 · 서버 · 쿠킹 · `WriteSnapshot` 은 `NOT_APPLICABLE`, `ScenarioRun` 은 `ISOLATED_AUTOMATED`). 앞서 남았던 틈 둘도 닫았다: `ScenarioRun` 은 시나리오 · 백엔드마다 없는 격리 설정 파일(`kAutomationUserSettingsVariable`)을 붙이고(시퀀스가 `--app-arg` 로 준 파일은 그대로), 배포본은 엔진 전용 환경 변수 `SW_USER_DATA_ROOT`(`ProcessOptions::_listEnvironment` 로 자식에게만 준다; `AppTestUtil::makeAppLaunch` 의 `Saved/UserHome`)로 사용자 설정 · 세이브를 실행별 폴더에 가른다(결정은 5-2_Build.md). `runApp` 을 거치지 않고 `runProcess` · `subprocess` 로 App 을 띄우는 코드는 `TestQaRunners` 의 정적 검사가 막는다(근사). 확인 필요: POSIX `PosixProcess.cpp` 의 `execve` 길은 Windows 에서 컴파일되지 않아 Linux 컴파일 · `ProcessTest.EnvironmentOverrideReachesOnlyTheChild` 실행이 통합 몫이다. 갈라 보려면 통합이 `a0521e79a` 이후 빌드로 `verify-batch golden-record` 와 `SW_UPDATE_GOLDEN` 없이 `ShippingSceneMatchesGoldenImage` · QA 골든 비교를 돌려 차이가 0 인지 본다(차이가 있으면 'low' 로 뜬 것이니 이 변경 뒤 다시 뜬다). 이 항목의 기준 재측정과 같이 한 번에 한다.
같은 결함 후보: 포워드 Present 의 합친 체인(`postchain`)은 후처리를 끈 뷰에서 셰이더가 블룸 · 외곽선 · 톤맵을 건너뛰지만 한 패스라 빠질 것이 없다.

### VulkanVsyncFrameMiss
제목: Vulkan VSync 에서 프레임이 주기를 놓침
옛번호: E-3
왜 필요한가: Vulkan(FIFO) + VSync 에서 가끔 한 프레임이 여러 주기를 놓친다(Release 큐브 100, 165 Hz). 조사 결과는 [검증과 측정](../08_Verification.md) 2절 "Vulkan VSync 프레임 놓침", 스왑체인 이미지 4 개는 결정 기록 3절에서 기각했다.
끝난 것(wt/gfxc, 빌드 · 실행 안 함): 측정 항목 — 프로파일 보고에 `[Profile] pacing  median <us>  over1.5x <n>  over2.5x <n>` 줄을 더했다(`FrameProfileSession::computePacing`, 100 us 칸 히스토그램). VSync 를 켜면 중앙 간격이 화면 주기라 over1.5x 가 주기를 하나 이상, over2.5x 가 둘 이상 놓친 프레임 수다. 고정 문턱 `over16ms` 는 60 Hz 기준이라 165 Hz 에서 주기 하나를 놓친 12 ms 프레임을 세지 못했다. 재는 판 — 검증 묶음 항목 `vsync-frame-miss`(Release · `-vk -vsync -gv_benchMeshes=100 -gv_profileSeconds=60`, 지표 셋은 보고만). 코드 수정은 없다 — 문서의 원인(이미지가 한 주기 늦게 돌아옴 · 합성기 경로의 펜스 지연)은 드라이버 · 합성기 쪽이고 엔진이 고칠 자리가 적혀 있지 않다(스왑체인 이미지 4 개는 결정 기록 3절에서 기각).
남은 것(통합): 확인 대기: 매니페스트 id `vsync-frame-miss`. 같은 판을 `-dx12 -vsync` 로 견주고, 놓침이 보이면 `-gv_tracy=1` 로 acquire · 펜스 · present 를 시간축으로 보고 전체 화면(독점) · NVIDIA "Vulkan/OpenGL 프레젠트 방식" 설정을 바꿔 가른다. 주기를 놓친 프레임이 0 이 아니어도 실패 판정은 하지 않는다(기준이 없다) — 값이 쌓이면 `max` 를 단다.

### DeviceLossHangRepro
제목: 실제 TDR 뒤 행을 재현하지 못함
옛번호: E-13, A-2
왜 필요한가: `rhi.removeDevice` 로 DX12(장치 제거) · Vulkan(손실 상태로 둠) 모두 프레임을 계속 돌고 종료 코드 0 으로 끝난다. 남은 것은 드라이버 안의 대기(첫 `VK_ERROR_DEVICE_LOST` 를 돌려주기 전)뿐이다.
끝난 것(wt/gfxc, 빌드 · 실행 안 함): `rhi.removeDevice <ms>` 가 드라이버 대기를 흉내 낸다 — 렌더 스레드가 다음 프레임의 `beginFrame`(펜스 대기 자리) 바로 앞에서 그만큼 잠든 뒤 장치를 제거한다(`RenderThread::requestDeviceLossAfterDriverWait`, Shipping 에는 없음). 시나리오 둘을 `engine/automation/rhiswap/` 에 더했다 — `removedevicewait`(2.5 초 대기 뒤 손실, 프레임이 다시 돌고 종료 코드 0), `removedevicehang`(40 초 대기, 상한 20 초 + 유예 5 초 뒤 행 감시가 보고를 쓰고 종료 코드 12 — 이것이 통과다).
남은 것(통합): 두 시나리오를 `-dx12` · `-vk` 로 돌린다. hang 은 보고 JSON 의 `hang: true` 와 `lastStep` 을 보고, 멈춘 동안 `py -3 -m Scripts stacks <pid>` 로 렌더 스레드가 `RenderThread::consumeDeviceLossRequest` 에 있는지 스택을 남긴다. 실제 TDR 이 다시 보이면 같은 명령으로 매달린 스레드의 스택을 남긴다.
왜 지금 못 하는가(실제 TDR 만): 진짜 TDR 은 GPU 를 2 초 넘게 붙잡는 컴퓨트로 일으킬 수 있으나 화면이 깨지고 쿠킹한 셰이더가 하나 더 필요해 실기에서 사용자가 승인한 때만 시험한다. 풀리는 조건: 실기 TDR 재현 또는 그 승인.

### ShaderPrefixAndFormat
제목: 셰이더 이름의 sw 접두를 없애고 clang-format 을 셰이더에도 건다
옛번호: 없음
끝난 것: (1) 셰이더 60 개에 clang-format 을 건다(설정 `Resource/.clang-format` — 루트를 물려받고 `MacroBlockBegin/End`(`SW_ROOT_CONSTANTS_*`), `AlignConsecutiveDeclarations`, `StatementMacros: [SW_NUMTHREADS]`, `AttributeMacros: [SW_GL_BINDING]`, `BreakConstructorInitializers: BeforeColon`). 루트에 두지 않은 까닭은 `AlignFunctionDeclarations: false` 가 C++ 정렬을 바꾸기 때문. `FormatClangFormat` · `FormatModified` · 커밋 훅이 셰이더를 대상에 넣었고 두 번째 포맷은 바뀌는 파일이 0 이다. 남은 한계: `SW_ROOT_CONSTANTS_BEGIN/END` 정의가 여러 줄로 갈리고(매크로 구조 변경은 폐기), 흐름 속성은 `common.hlsli` 의 속성 매크로 `SW_UNROLL` · `SW_LOOP` · `SW_BRANCH`(`.clang-format` AttributeMacros)로 쓰고, 마지막 맨 `[unroll]` 열 곳(`probeupdate.hlsl`)도 바꿨다(wt/gfxc, 소스만 — 아래 통합 몫). `CheckShaderConventions` 가 문장 머리의 맨 `[unroll]` · `[loop]` · `[branch]` · `[flatten]` 을 막는다(포맷 설정만으로는 못 막는다). (2) 구조체 필드 261 개를 `_camelCase` 로 바꿨다(셰이더 46 개, 머티리얼 구조체 필드 68 이름은 `.material` 75 개와 C++ 문자열 · `customization.xml` · 테스트를 같은 커밋에서). 함수 본문 안의 `#if` 를 정의로 몰았다(남은 본문 `#if` 는 백엔드 매크로 `DX11` · `DX12` · `__spirv__` · `SW_NATIVE_BINDLESS` 뿐이고 게이트가 그 밖의 조건을 막는다). `CheckShaderConventions.py` 와 [NamingScripts](../agents/NamingScripts.md) HLSL 절을 같은 커밋에서 고쳤다. 셰이더 컴파일 · 쿠킹은 네 백엔드로 했다.
남은 것(통합 검증): wt/gfxc 의 셰이더 소스 변경(커밋 훅이 쿠킹 바이너리를 요구해 작업자가 커밋하지 못한 몫 — `probeupdate.hlsl` 과 PostEffectBypass 의 다섯 진입점)을 넣고 `App --cook-shaders` 로 네 백엔드를 굽는다. 그 전에는 전체 `CheckShaderConventions` 가 `probeupdate.hlsl` 에서 진다. 새로 빌드한 Release App 으로 쿠킹이 같은 결과인지(필드 · 프로퍼티 이름이 바뀌어 리플렉션이 달라진다), 게임별 골든(`Scripts golden`), `MaterialTest` · `ShaderBindingValidatorTest` · `RenderPassGPUTest`. 에디터 머티리얼 패널은 `displayName` 이 없는 프로퍼티에 `_color` 처럼 밑줄 이름을 그대로 보인다. 자원을 가리키는 매크로 별칭 셋은 아직 매크로다 — `SW_MATERIAL( index )`(원소 형식이 셰이더마다 달라 함수 반환형을 못 적는다), `SW_SAMPLER_STATE`(백엔드마다 배열 · 개별 변수), `SW_APPEARANCE_*`(필드 유무가 퍼뮤테이션). 셰이더를 고친 뒤 `FormatModified.py` 로 포맷한다.
결정(사용자, 2026-10-23):
- 셰이더에도 `.clang-format` 을 적용한다. 폐기한 안: `#endif` 주석 자동 달기, 매크로 구조 변경, 매크로 헤더만 `clang-format off`. 포맷한 뒤 다시 돌려 멱등을 확인한다.
- `sw` 접두(FXC 에 네임스페이스가 없어 붙였던 것)는 **구조체로 묶어** 없앤다: 구조체를 첫 인자로 받던 함수는 멤버 함수(`context.computeLit( albedo )`), 구조체 없이 쓰거나 만드는 함수는 `static` 멤버 함수(`PixelContext::make( … )`, `View::getViewProjection()`). 멤버 · static 멤버 함수와 안에서 전역 cbuffer 읽기는 FXC `ps_5_0` · DXC `ps_6_6` · SPIR-V 모두 컴파일된다(실행 결과와 OpenGL 경로는 못 봄). `public` / `private` / `const` 멤버 함수는 안 되고 접근을 막는 린트는 두지 않는다. `SW_` 매크로 접두와 `g_` 전역은 그대로 둔다.
- 구조체 필드도 C++ 처럼 `_camelCase`(거울 필드는 이름이 같아져 변환 규칙이 사라진다).
- DX11 은 당분간 지원한다 — FXC 경로가 남으므로 `SW_ROOT` 같은 백엔드 차이 매크로는 지금 못 없앤다. 함수 안 `#if` 는 정의로 몰아 호출 쪽 본문에서 없앤다. `IndentPPDirectives: BeforeHash` 는 바꾸지 않는다(`None` 은 C++ 400 개 중 61 개를 바꾸고 `Leave` 는 20.1.8 이 못 읽으며 `PPIndentWidth: 2` 는 3중첩에서 `#` 줄이 코드와 겹친다).

### RhiFileLeftovers
제목: RHI 파일 후보 중 아직 남은 것
옛번호: 없음
왜 필요한가: 통합 N 뒤 RHI 파일을 훑어 같은 결함 후보로 적은 여섯 가운데(2026-10-28 `old-defects` 재측정) 남은 것이다.
끝난 것: DX11 Present 실패 로그가 장치 제거 뒤 매 프레임 반복되던 것은 `SW_LOG_ERROR_ONCE` 로 첫 실패만 남긴다(DX12 는 `_bDeviceRemovedLogged`, Vulkan 은 `DeviceLost` 를 `Failed` 와 갈라 처리). DX11 `Map(DO_NOT_WAIT)` 는 쓰지 않기로 이미 정했다(`D3D11RHIResourceFactory.cpp` 의 되읽기 주석).
끝난 것(wt/rhi-leftovers, 소스만 — 빌드 · 실행 안 함, 결정은 [5-5](../decisions/5-5_GraphicsRHI.md) 2026-10-28 네 줄): ① DX11 은 장부에 합치지 않고 총량만 세는 `RHIStagingBudget`(`Support/`)으로 뺐다 — 오프셋이 없는 백엔드에 장부를 쓰면 합이 들어가는 업로드를 조각 남음으로 `Busy` 시키기 때문이다. 대신 장부에만 있던 굶음 방지(예산 이상의 업로드가 못 받으면 예약을 남겨 작은 업로드를 막음)를 옮겼고, `UploadStaging` 의 RAII 수명과 두 번 시도는 그대로다. 예산 합 상한 · 혼자 들어감 · 예약 · 포기 · 예약 0 은 `RHIStagingRingTest.UploadBudget*` 세 케이스(nogpu)가 본다. ② Vulkan 프레임 밖 구조 버퍼 갱신과 GPU 시계 읽기의 펜스 대기는 비동기로 바꾸지 않기로 정했다 — 둘 다 U2 로 자기 제출의 펜스만 기다리고, `RhiAsyncUploads` 단위(U7 ~ U14)에 들어 있지 않으며 경계는 "텍스처 전송 티켓과 `waitIdle` 호출 정리는 그 계획, 일회성 제출 둘은 이 항목이 비목표로 둠"이다. ③ 큐 소유권 이동은 사실이 아니다 — 큐 패밀리가 하나이고 모두 `EXCLUSIVE` 에 `QUEUE_FAMILY_IGNORED` 라 옮길 소유권이 없다(전용 전송 큐를 도입할 때 같이 설계). GL 비트필드는 `mapFlags`(`GLbitfield`)가 아니라 `transitionBuffer` 의 배리어 비트가 결함이었다: `VertexOrConstant` · `Index` 가 SSBO 비트만 걸어 컴퓨트가 쓴 정점 · 유니폼 · 인덱스 데이터가 안 보일 수 있었다(호출처가 아직 없어 잠복). 정점 · 유니폼 · 요소 배열 비트로 고쳤다.
버퍼 상태 대조(소스만): `RHIBufferState` 여덟 값 × 네 백엔드 표를 `IRHICommandList::transitionBuffer` 머리말에 두었다(텍스처 상태는 RHI 계약에 없다 — 백엔드가 렌더 패스 · 복사 안에서 옮긴다). 빠진 곳 둘을 고쳤다: GL `VertexOrConstant` · `Index` 비트(위), Vulkan `VertexOrConstant` 에 컴퓨트 스테이지가 없어 컴퓨트가 읽는 상수 버퍼(`bindComputeConstantBuffer`)가 앞선 UAV 쓰기를 기다리지 않던 것. DX12 매핑은 빠짐이 없다. 새 상태가 생기면 네 백엔드가 모두 `default:` 없는 `switch` 라 컴파일 경고로 결정을 요구한다 — DX11 은 비교 한 줄이라 새 상태가 말없이 "UAV 를 뗀다" 로 흘렀는데 `switch` 로 바꿨다.
남은 것(통합, GPU): (a) DX11 에서 예산보다 큰 업로드 한 장이 받아지는지 — `RHIStagingRingGPUTest.DirectX11TransferLargerThanTheBudgetIsAcceptedAlone` 를 `DirectX12...` 를 본떠 썼다(실행 전). 68 MiB 업로드가 예산 64 MiB 를 넘어 혼자 들어가고 되읽은 값이 같으며 뒤의 4 MiB 구간 업로드와 작은 읽기가 다시 들어가는지 본다. (b) 굶음 방지의 실기 확인은 아직 만들지 않았다. 방법: DX11 장치에서 작은 업로드(256 KiB 초과, 예: 1 MiB)를 쉬지 않고 내 복사 중인 것이 늘 있게 하는 스레드 하나와, 예산보다 큰 업로드(68 MiB)를 `Busy` 면 다시 내는 스레드 하나를 두고 큰 업로드가 `Accepted` 가 되기까지 시간을 잰다. 합격은 예약 시간(250 ms)의 몇 배 안이고, 예약 시간 0 으로 초기화한 대조군은 같은 부하에서 훨씬 늦거나 끝내 못 받아야 시험이 굶음을 잡는 것이다. 시나리오 파일보다 `RHIStagingRingGPUTest` 의 두 스레드 케이스가 낫다(프레임 없이 `submitTextureUpload` · 폴링만으로 돈다). (c) GL 배리어: 컴퓨트가 쓴 버퍼를 `VertexOrConstant` · `Index` 로 넘겨 그리는 호출처가 생기는 날 네 백엔드 골든으로 본다(지금은 호출처가 없다). (d) Vulkan 컴퓨트 상수 버퍼: 컴퓨트가 쓴 버퍼를 다른 컴퓨트가 상수 버퍼로 읽는 호출처가 생기면 검증 레이어(동기화 검증)로 본다. 매니페스트 `rhi-transfer-gl1-staging-ring` 이 `RHIStagingRingGPUTest.*` 를 이미 돌리므로 (a) 케이스는 따라온다.
왜 지금 못 하는가: (a) · (b) 는 DX11 장치와 GPU 실행이 필요하고, (c) · (d) 는 호출처가 없다. 풀리는 조건: 통합의 GPU 단계(`rhi-transfer-gl1-staging-ring`)가 (a) 를 돌릴 때, (b) 는 (a) 통과 뒤 두 스레드 케이스를 쓸 때.

## 조건부

### VfxMultiGroupCompaction
제목: GPU 방출기 다중 그룹 압축
옛번호: D2-11
왜 필요한가: GPU 시뮬레이션은 방출기 하나를 그룹 하나(256 스레드)가 돌아 풀이 수만 개면 느리다(결정 기록 5-5). 풀이 크면 디스패치 셋으로 나눈다: (1) 그룹마다 생성 · 적분 · 생존 표시와 개수, (2) 개수의 접두사 합(그룹 하나), (3) 그룹마다 자기 시작 위치로 안정 압축. 단계마다 셋이라 여러 단계를 한 디스패치에 못 넣는다.
착수 조건: 풀이 수만 개인 GPU 방출기의 컴퓨트 프리패스 시간(`RT.Vfx.Simulate`)이 프레임 시간에서 보이면, 단계 수 × 3 디스패치가 그룹 하나보다 빨라지는 풀 크기를 Release 로 잰 뒤 풀 크기로 두 경로를 고를지(같은 결과여야 한다) 정한다.

### VfxGpuLightSlotSweep
제목: GPU 파티클 라이트 슬롯 수별 컴퓨트 시간
옛번호: 없음
왜 필요한가: GPU 구간 상한(`GPULightBuffer::kMaxGpuLight` = 프레임 라이트 상한의 4분의 1)은 측정 뒤 확정하기로 했다(결정 기록 5-5). 기준값은 `vfxgpulights` 씬의 기본 슬롯 `RT.Vfx.Simulate` 이고 `perf-compare --cases vfxGpuLights` 가 낸다(매니페스트 id `vfx-gpu-lights-measure`).
착수 조건: 기준값이 컴퓨트 프리패스에서 눈에 띄면(0.2 ms 이상) 슬롯 32 · 128 · 256 에서 같은 시간을 재고 상한을 줄인다. 슬롯 수를 바꾸는 스위치가 없어 슬롯별 효과 파일을 더하는 일이 먼저다.

### RealtimeProbeUpdateSweep
제목: 실시간 프로브 갱신 컴퓨트의 프로브 수 · 광선 수별 GPU 시간
옛번호: D2-14, D2-5.2
왜 필요한가: 안개 · 하늘 · 복셀 · 프로브 갱신의 테스트와 시나리오(`fog` · `sky` · `realtimegi`)는 통과했다. 시나리오 문턱은 첫 측정 전의 어림값이라 `measured` 로 올릴 값(예: 하늘 `atMost` -0.02)은 통합 측정이 정한다. 기준값은 `realtimegi` 씬의 `RT.Probe.Update` 이고 `perf-compare --cases realtimeProbe` 가 낸다(매니페스트 id `realtime-probe-measure`).
착수 조건: 기준값이 눈에 띄면(0.5 ms 이상) 프로브 수 · 광선 수 · 갱신 주기별 GPU 시간과 수렴 속도를 재서 기본값을 정한다. 볼륨 값을 바꾸는 스위치가 없어 볼륨 파일을 값별로 더하는 일이 먼저다.

### SpanAllocatorVatSkinPools
제목: 스킨 원본 · VAT 풀의 구간 할당기
옛번호: C-55
왜 필요한가: 스킨 원본 전체 다시 올리기(`RT.Deform.rebuildSkinSources`, 집합이 바뀐 프레임에만 찍힘)와 VAT 테이블 풀(`RT.Deform.rebuildVertexAnimationPool`)을 `GPUMeshVertexPool` · `GPUMeshMorphPool` 과 같은 모양의 `SpanAllocator` 구간 갱신으로 바꿀 이득이 있는지는 재야 안다. 구간은 이미 들어갔다.
착수 조건: 스트리밍 셀 진입 · 군중 합류 프레임의 프로파일에서 집합이 바뀐 프레임의 두 구간이 1 ms 이상으로 보이면 구간 할당기로 옮기는 단위를 계획 문서로 올린다. 1 ms 미만이면 접는다(이 절만 지운다).

### DirectionalShadowCascadeCulling
제목: 방향광 그림자 캐스케이드별 컬링
옛번호: D2-2
왜 필요한가: 뷰마다 그림자 컬링 칸이 하나(`_shadowCullSlot`)인 GPU 컬링 구조를 캐스케이드별 타일 칸으로 늘리면 그림자 패스 비용이 줄 수 있다. 이득이 있는지는 그림자 패스의 몫으로 정한다.
착수 조건: `perf-compare --cases shadowPassShare`(큐브 4000, 매니페스트 id `perf-compare-shadow-pass-share`)에서 `GPU.Shadow` 가 `GPU.Frame` 의 한 자리 퍼센트를 넘으면 칸을 타일로 늘리는 단위를 올린다. 한 자리 퍼센트면 접는다. 여드름(shadow acne) 눈 확인은 게임마다 빌드가 따로라(`SW_ACTIVE_GAME`) 착수할 때 게임별 빌드로 전후 스크린샷을 찍는다(경계 구 맞춤 · envshowcase CSM 포함).

### R8TextureSampling
제목: R8 텍스처를 네 백엔드에서 샘플링
옛번호: G-12
왜 필요한가: `App --import-textures` 가 1 채널 `R8_UNORM` DDS 를 내고 로더가 읽지만(결정 기록 5절 "텍스처 임포트의 형식 이름은") 실제 샘플링은 네 백엔드에서 본 적이 없습니다.
끝난 것(wt/gfxc, 빌드 · 실행 안 함): 외부 원본 없이 푼다 — `Scripts/generate/GenerateSpriteTextures.py` 가 임포터와 같은 형식(DX10 머리, DXGI 61)의 마스크 `engine/textures/test/mask_r8.dds`(64², 왼쪽 반 255 · 오른쪽 반 96)를 만들고, `RenderPassGPUTest.R8MaskTextureSamplesAsRedOnEveryBackend` 가 그것을 스프라이트로 그려 네 백엔드가 (r, 0, 0, 1) 로 샘플하는지 본다(초록 · 파랑 0, 알파 1 이라 그려짐, 두 반쪽이 갈림). 임포터가 그 형식을 낸다는 것은 `EditorTexturePipelineTest.SingleChannelR8ImportRoundTrips` 가 이미 본다.
남은 것(통합): 확인 대기: 매니페스트 id `r8-texture-sampling`(이제 한 명령으로 돈다). 실제 `*_mask.*` 원본이 생기면 그 게임의 시나리오도 본다.
