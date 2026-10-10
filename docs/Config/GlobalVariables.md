<!-- 생성 문서입니다. 손으로 고치지 말고 원본 코드를 고친 뒤 다시 만듭니다: py -3 Scripts/generate/GenerateConfigReference.py -->

# 전역 변수 (`-gv_*`)

[설정 색인](README.md)

값은 명령줄 `-gv_<이름>=<값>`, 에디터의 Global Variables 패널, 개발 콘솔로 바꿉니다. 종류는 셋입니다. **일반** 은 배포본에도 있습니다. **시험** 은 배포본에서 등록되지 않아 기본값으로만 읽힙니다. **시험 · 배포본에도** 는 스크립트가 배포 실행 파일을 조종할 때 쓰는 스위치입니다. 사용자 설정이 값을 넣는 변수는 `UserSettingsVariables.cpp` 에 있고, 시작할 때는 명령줄 `-gv_*` 가 플레이어 값보다 우선합니다(`docs/07_Configuration.md`).

## `Source/App`

| 이름 | 타입 | 기본값 | 종류 | 설명 | 정의 |
|---|---|---|---|---|---|
| `gv_devConsoleExec` | `string` | — | 시험 | 시작 씬이 열린 뒤 개발 콘솔로 돌릴 명령 (; 로 나눔) | [App.cpp](../../Source/App/App.cpp) |
| `gv_devConsoleOpen` | `int32` | `0` | 시험 | 게임 창 개발 콘솔을 연 채로 시작 (1=열기) | [App.cpp](../../Source/App/App.cpp) |
| `gv_fixedFrameDelta` | `float32` | `0.0` | 시험 · 배포본에도 | 프레임마다 흘릴 고정 시간(초, 0=실시간) — 결정적 실행 | [App.cpp](../../Source/App/App.cpp) |
| `gv_reloadGameAtFrame` | `int32` | `0` | 시험 | 이 프레임에 게임 모듈 핫 리로드를 요청한다 (0=사용 안 함) | [App.cpp](../../Source/App/App.cpp) |
| `gv_userSettingsApply` | `string` | — | 시험 · 배포본에도 | 사용자 설정을 메뉴와 같은 길로 바꾼다: "id=value;id=value" (자동화) | [UserSettingsHost.cpp](../../Source/App/UserSettingsHost.cpp) |
| `gv_userSettingsApplyFrame` | `int32` | `30` | 시험 · 배포본에도 | gv_userSettingsApply 를 적용할 프레임 | [UserSettingsHost.cpp](../../Source/App/UserSettingsHost.cpp) |
| `gv_userSettingsAutoConfirm` | `bool` | `true` | 시험 · 배포본에도 | gv_userSettingsApply 의 화면 변경을 바로 확인한다 (false 면 카운트다운이 되돌린다) | [UserSettingsHost.cpp](../../Source/App/UserSettingsHost.cpp) |

## `Source/Core/Network`

| 이름 | 타입 | 기본값 | 종류 | 설명 | 정의 |
|---|---|---|---|---|---|
| `gv_netEmuBandwidthKilobytesPerSecond` | `int32` | `0` | 시험 | 네트워크 흉내: 회선 속도(KB/s, 0=제한 없음) | [NetEmulation.cpp](../../Source/Core/Network/Transport/NetEmulation.cpp) |
| `gv_netEmuDuplicatePercent` | `int32` | `0` | 시험 | 네트워크 흉내: 중복(%) — PktDup | [NetEmulation.cpp](../../Source/Core/Network/Transport/NetEmulation.cpp) |
| `gv_netEmuJitterMs` | `int32` | `0` | 시험 | 네트워크 흉내: ± 흔들림(ms) — PktLagVariance | [NetEmulation.cpp](../../Source/Core/Network/Transport/NetEmulation.cpp) |
| `gv_netEmuLatencyMs` | `int32` | `0` | 시험 | 네트워크 흉내: 한쪽 지연(ms) — 언리얼 PktLag | [NetEmulation.cpp](../../Source/Core/Network/Transport/NetEmulation.cpp) |
| `gv_netEmuLossPercent` | `int32` | `0` | 시험 | 네트워크 흉내: 손실(%) — PktLoss | [NetEmulation.cpp](../../Source/Core/Network/Transport/NetEmulation.cpp) |
| `gv_netEmuReorderPercent` | `int32` | `0` | 시험 | 네트워크 흉내: 순서 뒤바뀜(%) — PktOrder | [NetEmulation.cpp](../../Source/Core/Network/Transport/NetEmulation.cpp) |

## `Source/Editor`

| 이름 | 타입 | 기본값 | 종류 | 설명 | 정의 |
|---|---|---|---|---|---|
| `gv_editorStartupScene` | `string` | — | 일반 | 에디터 시작 시 열 씬의 리소스 경로 (비우면 열지 않는다) | [ImGuiEditor.cpp](../../Source/Editor/ImGuiEditor.cpp) |
| `gv_editorUiScale` | `float32` | `0.0` | 일반 | 에디터 UI 배율 (0 = 모니터 DPI 를 따름) | [ImGuiEditor.cpp](../../Source/Editor/ImGuiEditor.cpp) |

## `Source/Editor/Common`

| 이름 | 타입 | 기본값 | 종류 | 설명 | 정의 |
|---|---|---|---|---|---|
| `gv_editorOpenPanel` | `string` | — | 시험 | 시작할 때 이 id 의 패널 하나만 연다, all 이면 전부 연다 (비우면 사용 안 함) | [EditorDockLayout.cpp](../../Source/Editor/Common/GUI/EditorDockLayout.cpp) |
| `gv_editorPanelDump` | `int32` | `0` | 시험 | N 번째 프레임에 에디터 ImGui 창별 드로우 통계를 덤프 (0=사용 안 함) | [EditorPanelDump.cpp](../../Source/Editor/Common/GUI/EditorPanelDump.cpp) |
| `gv_tracyViewerPath` | `string` | — | 일반 | Tracy 뷰어(tracy-profiler 0.14.1) 경로 — 파일이나 폴더 (비우면 Tools/Tracy) | [EditorTracyLauncher.cpp](../../Source/Editor/Common/Commands/EditorTracyLauncher.cpp) |

## `Source/Editor/Panels`

| 이름 | 타입 | 기본값 | 종류 | 설명 | 정의 |
|---|---|---|---|---|---|
| `gv_editorPanelTimes` | `int32` | `0` | 시험 | N 프레임 동안 에디터 패널마다 그리기 시간을 모아 한 번 로그로 찍는다 (0=끄기) | [EditorPanelManager.cpp](../../Source/Editor/Panels/EditorPanelManager.cpp) |

## `Source/Editor/SelfTest`

| 이름 | 타입 | 기본값 | 종류 | 설명 | 정의 |
|---|---|---|---|---|---|
| `gv_editorRegistryDump` | `bool` | `false` | 시험 | 시작할 때 에디터 레지스트리(패널 · 팝업 · 인스펙터 · 시각화 · 메뉴)를 로그로 덤프 | [EditorRegistryDump.cpp](../../Source/Editor/SelfTest/EditorRegistryDump.cpp) |
| `gv_editorSelfTest` | `string` | — | 시험 | 에디터가 뜬 뒤 이름이 패턴에 맞는 에디터 자체 시험을 돌리고 끝낸다 (* · 쉼표, 비우면 사용 안 함) | [EditorSelfTest.cpp](../../Source/Editor/SelfTest/EditorSelfTest.cpp) |
| `gv_editorSelfTestReport` | `string` | — | 시험 | 에디터 자체 시험 결과를 쓸 파일 (비우면 로그에만) | [EditorSelfTest.cpp](../../Source/Editor/SelfTest/EditorSelfTest.cpp) |

## `Source/Engine`

| 이름 | 타입 | 기본값 | 종류 | 설명 | 정의 |
|---|---|---|---|---|---|
| `gv_canvasTestPattern` | `bool` | `false` | 시험 | 주 출력에 캔버스(화면 2D) 시험 그림을 그린다 — 사각형 · 자르기 · 그림자 · 글자 | [EngineLoop.cpp](../../Source/Engine/EngineLoop.cpp) |
| `gv_crashTest` | `int32` | `0` | 시험 · 배포본에도 | 일부러 크래시를 내 리포트 경로를 검증합니다 (1=널 쓰기 2=스택 오버플로 3=작업 스레드 스택 오버플로 4=abort 5=순수 가상 호출) | [EngineLoop.cpp](../../Source/Engine/EngineLoop.cpp) |
| `gv_dumpReflection` | `string` | — | 시험 | 첫 프레임에 이 이름들(쉼표로 여럿)의 리플렉션 등록 내용을 로그로 남긴다 — 타입 · enum (비우면 사용 안 함) | [EngineLoop.cpp](../../Source/Engine/EngineLoop.cpp) |
| `gv_navDebugDraw` | `int32` | `0` | 일반 | 내비메시 디버그 — 선(편집기 뷰포트): 1 폴리곤 테두리 · 2 에이전트 경로 · 4 에이전트 속도 · 8 장애물, 16 걷는 면 · 경로를 게임 화면의 메시로 (31 = 모두, 0 = 끔) | [EngineLoop.cpp](../../Source/Engine/EngineLoop.cpp) |
| `gv_physicsDebugDraw` | `bool` | `false` | 일반 | 강체 물리 바디 · 캐릭터를 디버그 선으로 그린다 | [EngineLoop.cpp](../../Source/Engine/EngineLoop.cpp) |
| `gv_rhiSwapAtFrame` | `int32` | `0` | 시험 | 이 프레임에 백엔드 교체를 요청한다 (0=사용 안 함) | [EngineLoop.cpp](../../Source/Engine/EngineLoop.cpp) |
| `gv_rhiSwapTo` | `RHIBackend` | `DirectX12` | 시험 | gv_rhiSwapAtFrame 에 바꿀 백엔드 | [EngineLoop.cpp](../../Source/Engine/EngineLoop.cpp) |
| `gv_telemetryFolder` | `string` | — | 시험 · 배포본에도 | 텔레메트리 스풀 폴더 (비면 사용자 폴더의 telemetry/) | [EngineLoop.cpp](../../Source/Engine/EngineLoop.cpp) |

## `Source/Engine/Destruction`

| 이름 | 타입 | 기본값 | 종류 | 설명 | 정의 |
|---|---|---|---|---|---|
| `gv_destructionMaxDebrisBodies` | `int32` | `512` | 일반 | 한 씬(월드)의 모든 파괴 오브젝트가 함께 드는 떨어진 덩어리 바디의 상한(넘으면 오래된 작은 것부터 사라진다) | [FractureComponentBase.cpp](../../Source/Engine/Destruction/FractureComponentBase.cpp) |

## `Source/Engine/Environment`

| 이름 | 타입 | 기본값 | 종류 | 설명 | 정의 |
|---|---|---|---|---|---|
| `gv_environmentAnimate` | `int32` | `1` | 시험 · 배포본에도 | 바람 · 파도 시간이 흐른다 (0=멈춤, 스크린샷 비교용) | [EnvironmentUtil.cpp](../../Source/Engine/Environment/EnvironmentUtil.cpp) |

## `Source/Engine/Graphics`

| 이름 | 타입 | 기본값 | 종류 | 설명 | 정의 |
|---|---|---|---|---|---|
| `gv_gpuUploadQueue` | `int32` | `1` | 일반 | GPU 업로드를 워커로 앞당긴다 (0=렌더 스레드가 그 자리에서 만든다) | [GPUUploadQueue.cpp](../../Source/Engine/Graphics/Upload/GPUUploadQueue.cpp) |
| `gv_rhiBackBufferFormat` | `int32` | `0` | 일반 | 요청 백버퍼 포맷: 0=R8G8B8A8_UNORM, 1=B8G8R8A8_UNORM (실제 채택값은 getBackBufferFormat) | [IRHIDevice.cpp](../../Source/Engine/Graphics/RHI/IRHIDevice.cpp) |
| `gv_rhiBackend` | `RHIBackend` | `SW_RHI_BACKEND_DEFAULT` | 일반 | Current RHI Backend | [RHI.cpp](../../Source/Engine/Graphics/RHI/RHI.cpp) |
| `gv_rhiSoftwareAdapter` | `int32` | `0` | 시험 | 소프트웨어 어댑터로 띄운다: 0=하드웨어, 1=WARP(DX11 · DX12) · CPU 디바이스(Vulkan) | [IRHIDevice.cpp](../../Source/Engine/Graphics/RHI/IRHIDevice.cpp) |

## `Source/Engine/Object`

| 이름 | 타입 | 기본값 | 종류 | 설명 | 정의 |
|---|---|---|---|---|---|
| `gv_animationForceVertexAnimation` | `int32` | `0` | 시험 | Draw every crowd-shared unit with vertex animation regardless of distance (verification) | [AnimationSystem.cpp](../../Source/Engine/Object/Animation/AnimationSystem.cpp) |
| `gv_animationLod` | `int32` | `1` | 일반 | Animation LOD: frustum visibility, update rate, bone LOD and budget (0 = every unit every frame) | [AnimationSystem.cpp](../../Source/Engine/Object/Animation/AnimationSystem.cpp) |
| `gv_animationRewind` | `int32` | `0` | 시험 | Record animation rewind history (poses, graph state, notifies, curves, root motion) | [AnimationRewind.cpp](../../Source/Engine/Object/Animation/AnimationRewind.cpp) |
| `gv_animationRewindSeconds` | `float32` | `10.0` | 시험 | Seconds of animation rewind history kept per unit | [AnimationRewind.cpp](../../Source/Engine/Object/Animation/AnimationRewind.cpp) |

## `Source/Engine/Profiling`

| 이름 | 타입 | 기본값 | 종류 | 설명 | 정의 |
|---|---|---|---|---|---|
| `gv_memoryReport` | `int32` | `0` | 시험 | 다음 프레임에 메모리 태그 표(살아 있는 · 최고치 · 예산)를 한 번 남김 (1=남기기) | [MemoryBudgetMonitor.cpp](../../Source/Engine/Profiling/MemoryBudgetMonitor.cpp) |
| `gv_memoryTracking` | `int32` | `-1` | 시험 | 메모리 태그 추적 (-1=구성 기본, 0=끄기, 1=켜기) | [MemoryBudgetMonitor.cpp](../../Source/Engine/Profiling/MemoryBudgetMonitor.cpp) |
| `gv_profileAllocSites` | `int32` | `0` | 시험 | 측정 구간의 할당을 콜스택별로 세어 상위 N 곳을 보고 (0=끄기) | [FrameProfileSession.cpp](../../Source/Engine/Profiling/FrameProfileSession.cpp) |
| `gv_profileFrames` | `int32` | `0` | 시험 · 배포본에도 | 프레임 프로파일 측정 프레임 수 (0=사용 안 함) | [FrameProfileSession.cpp](../../Source/Engine/Profiling/FrameProfileSession.cpp) |
| `gv_profileSeconds` | `int32` | `0` | 시험 · 배포본에도 | 프레임 프로파일 측정 시간(초, 0=사용 안 함) | [FrameProfileSession.cpp](../../Source/Engine/Profiling/FrameProfileSession.cpp) |
| `gv_tracy` | `bool` | `false` | 시험 | Tracy 프로파일러로 계측을 내보낸다(기동부터, 뷰어는 localhost 로 붙는다) | [ProfilerBackend.cpp](../../Source/Engine/Profiling/ProfilerBackend.cpp) |
| `gv_tracyMemory` | `bool` | `false` | 시험 | Tracy 에 할당 · 해제를 메모리 태그별로 보낸다(느림, gv_tracy 와 같이) | [ProfilerBackend.cpp](../../Source/Engine/Profiling/ProfilerBackend.cpp) |

## `Source/Engine/Renderer`

| 이름 | 타입 | 기본값 | 종류 | 설명 | 정의 |
|---|---|---|---|---|---|
| `gv_deferred` | `bool` | `false` | 일반 | 기본 파이프라인을 디퍼드로 (기본 포워드) | [FrameRenderer.cpp](../../Source/Engine/Renderer/Frame/FrameRenderer.cpp) |
| `gv_drawMerge` | `int32` | `1` | 일반 | 씬 배치 멀티 드로우 묶기 (0=배치마다 호출, 진단용) | [FrameRenderer.cpp](../../Source/Engine/Renderer/Frame/FrameRenderer.cpp) |
| `gv_dumpRenderGraph` | `bool` | `false` | 시험 | 렌더 그래프를 컴파일할 때마다 레벨 · 패스 · 읽고 쓰는 자원을 로그로 남긴다 | [FrameRendererPassExecute.cpp](../../Source/Engine/Renderer/Frame/FrameRendererPassExecute.cpp) |
| `gv_gpuCulling` | `int32` | `1` | 일반 | GPU 컬링 컴퓨트 디스패치 (0=건너뜀, 진단용) | [FrameRendererCompute.cpp](../../Source/Engine/Renderer/Frame/FrameRendererCompute.cpp) |
| `gv_morphDiag` | `int32` | `0` | 일반 | 메시 모프 진단 (0 평소 / 1 강제 켬 / 2 디스패치 생략 / 3 번호표) | [FrameRendererCompute.cpp](../../Source/Engine/Renderer/Frame/FrameRendererCompute.cpp) |
| `gv_renderPipeline` | `string` | — | 일반 | 이 파이프라인 XML 로 그린다 (비우면 기본 포워드 · gv_deferred) | [FrameRenderer.cpp](../../Source/Engine/Renderer/Frame/FrameRenderer.cpp) |
| `gv_renderViewBudget` | `int32` | `4` | 일반 | 한 프레임에 그리는 추가 뷰(CCTV · 백미러 · PiP)의 최대 수 (0 = 제한 없음) | [RenderViewCollector.cpp](../../Source/Engine/Renderer/Frame/RenderViewCollector.cpp) |
| `gv_rhiImmediateSubmit` | `bool` | `false` | 일반 | RHI 커맨드 리스트를 프레임 끝에 모아 제출하지 않고 즉시 제출 (디버깅용, 오버헤드 큼) | [RenderThread.cpp](../../Source/Engine/Renderer/RenderThread.cpp) |
| `gv_screenshot` | `string` | — | 시험 · 배포본에도 | 화면에 나간 그림(Present 결과)을 PPM 으로 덤프할 경로 (비면 사용 안 함) | [RenderThread.cpp](../../Source/Engine/Renderer/RenderThread.cpp) |
| `gv_screenshotAttachment` | `string` | — | 시험 · 배포본에도 | Present 결과 대신 덤프할 트랜지언트 이름 (비면 Present 결과) | [RenderThread.cpp](../../Source/Engine/Renderer/RenderThread.cpp) |
| `gv_screenshotCount` | `int32` | `1` | 시험 · 배포본에도 | 연속으로 찍을 스크린샷 수 (기본 1) | [RenderThread.cpp](../../Source/Engine/Renderer/RenderThread.cpp) |
| `gv_screenshotFrame` | `int32` | `10` | 시험 · 배포본에도 | 스크린샷을 찍을 프레임 번호 (기본 10) | [RenderThread.cpp](../../Source/Engine/Renderer/RenderThread.cpp) |
| `gv_screenshotInterval` | `int32` | `1` | 시험 · 배포본에도 | 연속 스크린샷 사이 프레임 수 (기본 1) | [RenderThread.cpp](../../Source/Engine/Renderer/RenderThread.cpp) |
| `gv_useRenderThread` | `bool` | `true` | 일반 | 전용 RenderThread 사용 (false = 게임 스레드 인라인 submit) | [RenderThread.cpp](../../Source/Engine/Renderer/RenderThread.cpp) |
| `gv_vertexPool` | `int32` | `1` | 일반 | 씬 메시 정점 풀 (0=메시마다 정점 버퍼, 진단용) | [FrameRenderer.cpp](../../Source/Engine/Renderer/Frame/FrameRenderer.cpp) |
| `gv_viewMode` | `int32` | `0` | 일반 | 씬 보기 방식 (0 Lit / 1 Unlit / 2 Wireframe) | [FrameRenderer.cpp](../../Source/Engine/Renderer/Frame/FrameRenderer.cpp) |

## `Source/Engine/Resource`

| 이름 | 타입 | 기본값 | 종류 | 설명 | 정의 |
|---|---|---|---|---|---|
| `gv_assetLoadProfile` | `int32` | `1` | 일반 | 에셋 로드 시간 · 바이트 기록 (0=끄기) | [AssetLoadProfiler.cpp](../../Source/Engine/Resource/AssetLoadProfiler.cpp) |
| `gv_assetLoadReport` | `int32` | `0` | 시험 · 배포본에도 | 엔진 종료 때 에셋 로드 표를 로그로 (1=켜기) | [AssetLoadProfiler.cpp](../../Source/Engine/Resource/AssetLoadProfiler.cpp) |

## `Source/Engine/Scene`

| 이름 | 타입 | 기본값 | 종류 | 설명 | 정의 |
|---|---|---|---|---|---|
| `gv_defaultMaterial` | `string` | — | 일반 | 씬 기본 머티리얼 경로 덮어쓰기 (비면 EngineDefaultAssets) | [Scene.cpp](../../Source/Engine/Scene/Scene.cpp) |

## `Source/Engine/UI`

| 이름 | 타입 | 기본값 | 종류 | 설명 | 정의 |
|---|---|---|---|---|---|
| `gv_benchUiChurn` | `int32` | `0` | 시험 · 배포본에도 | UI 벤치 — 프레임마다 글을 바꾸는 셀 수(앞쪽 보이는 셀 안에서 돈다) | [UIBenchScreen.cpp](../../Source/Engine/UI/Debug/UIBenchScreen.cpp) |
| `gv_benchUiWidgets` | `int32` | `0` | 시험 · 배포본에도 | UI 벤치 — 격자 셀 수(셀마다 테두리 · 아이콘 · 글, 대부분 스크롤 밖) (0=사용 안 함) | [UIBenchScreen.cpp](../../Source/Engine/UI/Debug/UIBenchScreen.cpp) |
| `gv_uiDebugSafeZone` | `float32` | `0.0` | 시험 | UI 안전 영역 흉내 — 각 변을 화면 크기의 이 비율(0..0.1)만큼 안쪽으로 민다 | [UIScale.cpp](../../Source/Engine/UI/Layout/UIScale.cpp) |
| `gv_uiDemo` | `bool` | `false` | 시험 · 배포본에도 | UI 시험 화면을 띄운다 — 글 · 버튼 다섯 · 슬라이더 · 체크 · 진행 · 콤보 · 입력 필드 · 그리기 견본(둥근 상자 · 자르기 · 9-슬라이스 · 오른쪽에서 왼쪽 글) | [UIDemoScreen.cpp](../../Source/Engine/UI/Debug/UIDemoScreen.cpp) |
| `gv_uiOptionsMenu` | `bool` | `false` | 시험 · 배포본에도 | 옵션 메뉴를 띄운다 — 사용자 설정 스키마에서 만든 탭 · 행(개발 확인 · 스크린샷) | [OptionsMenuScreen.cpp](../../Source/Engine/UI/Screen/OptionsMenuScreen.cpp) |

## `Source/Engine/UserSettings`

| 이름 | 타입 | 기본값 | 종류 | 설명 | 정의 |
|---|---|---|---|---|---|
| `gv_cameraFieldOfView` | `float32` | `70.0` | 일반 | 카메라 시야각(도) (사용자 설정 gameplay.fieldOfView) | [UserSettingsVariables.cpp](../../Source/Engine/UserSettings/UserSettingsVariables.cpp) |
| `gv_cameraHeadBob` | `bool` | `true` | 일반 | 걷기 머리 흔들림 (사용자 설정 gameplay.headBob) | [UserSettingsVariables.cpp](../../Source/Engine/UserSettings/UserSettingsVariables.cpp) |
| `gv_cameraShakeScale` | `float32` | `1.0` | 일반 | 카메라 흔들림 배율 0~1 (사용자 설정 gameplay.cameraShake) | [UserSettingsVariables.cpp](../../Source/Engine/UserSettings/UserSettingsVariables.cpp) |
| `gv_colorVisionMode` | `int32` | `0` | 일반 | 색각 보정 0 끔 1 적색약 2 녹색약 3 청색약 (사용자 설정 accessibility.colorVision, UI 캔버스만 — 톤맵 미구현) | [UserSettingsVariables.cpp](../../Source/Engine/UserSettings/UserSettingsVariables.cpp) |
| `gv_effectsQuality` | `int32` | `2` | 일반 | 이펙트 품질 0~3 (사용자 설정 graphics.effectsQuality) | [UserSettingsVariables.cpp](../../Source/Engine/UserSettings/UserSettingsVariables.cpp) |
| `gv_foliageDensity` | `float32` | `1.0` | 일반 | 식생 밀도 배율 (사용자 설정 graphics.foliageDensity) | [UserSettingsVariables.cpp](../../Source/Engine/UserSettings/UserSettingsVariables.cpp) |
| `gv_motionBlur` | `bool` | `true` | 일반 | 모션 블러 (사용자 설정 graphics.motionBlur) | [UserSettingsVariables.cpp](../../Source/Engine/UserSettings/UserSettingsVariables.cpp) |
| `gv_postQuality` | `int32` | `2` | 일반 | 후처리 품질 0~3 (사용자 설정 graphics.postQuality) | [UserSettingsVariables.cpp](../../Source/Engine/UserSettings/UserSettingsVariables.cpp) |
| `gv_reduceFlashing` | `bool` | `false` | 일반 | 번쩍임 줄이기 (사용자 설정 accessibility.reduceFlashing) | [UserSettingsVariables.cpp](../../Source/Engine/UserSettings/UserSettingsVariables.cpp) |
| `gv_renderScale` | `float32` | `1.0` | 일반 | 3D 렌더 해상도 배율 0.5~1 (사용자 설정 graphics.renderScale, 렌더러 미구현) | [UserSettingsVariables.cpp](../../Source/Engine/UserSettings/UserSettingsVariables.cpp) |
| `gv_shadowQuality` | `int32` | `2` | 일반 | 그림자 품질 0~3 (사용자 설정 graphics.shadowQuality) | [UserSettingsVariables.cpp](../../Source/Engine/UserSettings/UserSettingsVariables.cpp) |
| `gv_subtitleBackgroundOpacity` | `float32` | `0.5` | 일반 | 자막 배경 불투명도 0~1 (사용자 설정 accessibility.subtitleBackground) | [UserSettingsVariables.cpp](../../Source/Engine/UserSettings/UserSettingsVariables.cpp) |
| `gv_subtitles` | `bool` | `true` | 일반 | 자막 표시 (사용자 설정 accessibility.subtitles) | [UserSettingsVariables.cpp](../../Source/Engine/UserSettings/UserSettingsVariables.cpp) |
| `gv_subtitleSize` | `int32` | `1` | 일반 | 자막 크기 0 작게 1 보통 2 크게 (사용자 설정 accessibility.subtitleSize) | [UserSettingsVariables.cpp](../../Source/Engine/UserSettings/UserSettingsVariables.cpp) |
| `gv_textureQuality` | `int32` | `2` | 일반 | 텍스처 품질 0~3 (사용자 설정 graphics.textureQuality) | [UserSettingsVariables.cpp](../../Source/Engine/UserSettings/UserSettingsVariables.cpp) |
| `gv_uiReduceMotion` | `bool` | `false` | 일반 | UI 움직임 줄이기 — UI 애니메이션 · 트윈 · 스타일 전환이 바로 끝 값으로 (사용자 설정 accessibility.reduceMotion) | [UserSettingsVariables.cpp](../../Source/Engine/UserSettings/UserSettingsVariables.cpp) |
| `gv_uiScale` | `float32` | `1.0` | 일반 | 게임 UI 배율 (사용자 설정 accessibility.uiScale) | [UserSettingsVariables.cpp](../../Source/Engine/UserSettings/UserSettingsVariables.cpp) |
| `gv_uiTextScale` | `float32` | `1.0` | 일반 | 게임 UI 글자 크기 배율 (사용자 설정 accessibility.textSize) | [UserSettingsVariables.cpp](../../Source/Engine/UserSettings/UserSettingsVariables.cpp) |
| `gv_uiTheme` | `string` | `default` | 일반 | 게임 UI 테마 이름 default · highcontrast (사용자 설정 accessibility.uiTheme) | [UserSettingsVariables.cpp](../../Source/Engine/UserSettings/UserSettingsVariables.cpp) |
| `gv_upscaler` | `int32` | `0` | 일반 | 업스케일러 (0 끔, 사용자 설정 graphics.upscaler) | [UserSettingsVariables.cpp](../../Source/Engine/UserSettings/UserSettingsVariables.cpp) |
| `gv_userSettingsFile` | `string` | — | 시험 · 배포본에도 | 사용자 설정 파일 경로 (비면 사용자 폴더의 usersettings.json) | [UserSettingsVariables.cpp](../../Source/Engine/UserSettings/UserSettingsVariables.cpp) |
| `gv_viewDistanceScale` | `float32` | `1.0` | 일반 | 시야 거리 배율 (사용자 설정 graphics.viewDistance) | [UserSettingsVariables.cpp](../../Source/Engine/UserSettings/UserSettingsVariables.cpp) |

## `Source/Engine/Utility`

| 이름 | 타입 | 기본값 | 종류 | 설명 | 정의 |
|---|---|---|---|---|---|
| `gv_timeScale` | `float32` | `1.0` | 일반 | 게임 시간 배율 (1=실시간, 0.25=슬로 모션, 0=멈춤) | [GameTimeScale.cpp](../../Source/Engine/Utility/GameTimeScale.cpp) |

## `Source/GameFramework/Base`

| 이름 | 타입 | 기본값 | 종류 | 설명 | 정의 |
|---|---|---|---|---|---|
| `gv_aiDirectorTrace` | `int32` | `0` | 시험 | AiDirector: log every phase change, spawn, encounter and reward (1=on) | [AiDirector.cpp](../../Source/GameFramework/Base/Actor/AI/Director/AiDirector.cpp) |
| `gv_cameraPreset` | `string` | — | 시험 · 배포본에도 | 카메라 디렉터의 시작 프리셋 id (캡처 카메라 제외, 비우면 데이터대로) | [CameraDirectorComponent.cpp](../../Source/GameFramework/Base/Actor/Camera/CameraDirectorComponent.cpp) |
| `gv_firstScene` | `string` | — | 시험 · 배포본에도 | 팩 설정의 시작 씬 대신 처음 열 씬의 리소스 경로 (비우면 사용 안 함) | [GameInstanceBase.cpp](../../Source/GameFramework/Base/Foundation/Framework/Flow/GameInstanceBase.cpp) |
| `gv_scheduleTrace` | `string` | — | 시험 | Schedule: log the why-am-I-here trace and today's timeline of this NPC id (* = all) when the value changes | [ScheduleSystem.cpp](../../Source/GameFramework/Base/Actor/AI/Schedule/ScheduleSystem.cpp) |

## `Source/Games/AbilityArena`

| 이름 | 타입 | 기본값 | 종류 | 설명 | 정의 |
|---|---|---|---|---|---|
| `gv_arenaAutoPlay` | `int32` | `0` | 시험 · 배포본에도 | AbilityArena: 플레이어도 AI 가 조종 (1=켜기) | [ArenaDirectorComponent.cpp](../../Source/Games/AbilityArena/ArenaDirectorComponent.cpp) |

## `Source/Games/Empty`

| 이름 | 타입 | 기본값 | 종류 | 설명 | 정의 |
|---|---|---|---|---|---|
| `gv_benchAnimate` | `int32` | `1` | 시험 · 배포본에도 | 벤치의 시간 구동 변화(회전·상하 이동·스케일) (0=멈춤, 픽셀 비교 검증용) | [BenchScene.cpp](../../Source/Games/Empty/BenchScene.cpp) |
| `gv_benchCharacterColumns` | `int32` | `0` | 시험 | 벤치 캐릭터를 N 열 격자로 세운다 (0=한 줄) | [BenchScene.cpp](../../Source/Games/Empty/BenchScene.cpp) |
| `gv_benchCharacters` | `int32` | `0` | 시험 | 벤치 스킨드 캐릭터 수 (0=사용 안 함, KayKit 기사 · Idle/Walking_A) | [BenchScene.cpp](../../Source/Games/Empty/BenchScene.cpp) |
| `gv_benchCharacterStagger` | `int32` | `0` | 시험 | 벤치 캐릭터 시작 시각을 흩는다 (0=모두 0 초) | [BenchScene.cpp](../../Source/Games/Empty/BenchScene.cpp) |
| `gv_benchCombat` | `int32` | `0` | 시험 | 벤치 전투 연출 (0=사용 안 함, KayKit 스켈레톤 — 알림 · 히트 존 · 래그돌 · 칼 떨어뜨리기) | [BenchScene.cpp](../../Source/Games/Empty/BenchScene.cpp) |
| `gv_benchCrowdShare` | `int32` | `0` | 시험 | 벤치 캐릭터 군중 포즈 공유 (0=캐릭터마다 사본 · 1=그룹 공유 + 먼 캐릭터 VAT) | [BenchScene.cpp](../../Source/Games/Empty/BenchScene.cpp) |
| `gv_benchFaces` | `int32` | `0` | 시험 | 벤치 시험 머리 수 (0=사용 안 함, 모프 · 표정 · 립싱크 · 깜빡임 · 시선) | [BenchScene.cpp](../../Source/Games/Empty/BenchScene.cpp) |
| `gv_benchGround` | `int32` | `0` | 시험 | 격자 아래에 바닥 평면을 깝니다 (그림자를 받는 면) | [BenchScene.cpp](../../Source/Games/Empty/BenchScene.cpp) |
| `gv_benchInstanced` | `int32` | `0` | 시험 | 1 이면 큐브를 GameObject 없이 메시 인스턴스 배치로 만든다 (기본 0) | [BenchScene.cpp](../../Source/Games/Empty/BenchScene.cpp) |
| `gv_benchLightRadius` | `float32` | `0.0` | 시험 | 벤치 라이트 반경 (0=격자 간격에서 정한다) | [BenchScene.cpp](../../Source/Games/Empty/BenchScene.cpp) |
| `gv_benchLights` | `int32` | `0` | 시험 | 격자에 흩뿌릴 점광·스포트라이트 수 (주광은 별개) | [BenchScene.cpp](../../Source/Games/Empty/BenchScene.cpp) |
| `gv_benchMaterialChurn` | `int32` | `0` | 시험 | 프레임당 값을 무작위로 바꿀 머티리얼 인스턴스 수 (0=사용 안 함) | [BenchScene.cpp](../../Source/Games/Empty/BenchScene.cpp) |
| `gv_benchMaterialChurnAdd` | `int32` | `0` | 시험 | 프레임당 새로 붙이거나 떼어낼 머티리얼 인스턴스 수 (0=사용 안 함) | [BenchScene.cpp](../../Source/Games/Empty/BenchScene.cpp) |
| `gv_benchMaterialChurnKeyword` | `int32` | `0` | 시험 | N 프레임마다 키워드·멀티컴파일을 흔듭니다 (0=사용 안 함) | [BenchScene.cpp](../../Source/Games/Empty/BenchScene.cpp) |
| `gv_benchMaterialInstances` | `int32` | `0` | 시험 | 벤치 큐브마다 MaterialInstance 부여 (배치를 큐브 수만큼 가른다) | [BenchScene.cpp](../../Source/Games/Empty/BenchScene.cpp) |
| `gv_benchMeshes` | `int32` | `0` | 시험 · 배포본에도 | 시작 시 생성할 벤치 큐브 수 (0=사용 안 함) | [BenchScene.cpp](../../Source/Games/Empty/BenchScene.cpp) |
| `gv_benchMeshMorph` | `int32` | `0` | 시험 | 벤치 도형의 정점을 GPU 가 매 프레임 변형합니다 (0=사용 안 함) | [BenchScene.cpp](../../Source/Games/Empty/BenchScene.cpp) |
| `gv_benchMeshShapes` | `int32` | `1` | 시험 | 벤치가 섞어 쓸 도형 수 (1=큐브만 · 최대 5: 큐브·구·실린더·캡슐·원뿔) | [BenchScene.cpp](../../Source/Games/Empty/BenchScene.cpp) |
| `gv_benchMeshVariants` | `int32` | `1` | 시험 | 벤치 메시 종류 수 (= 배치 수, 드로우 경로 측정용) | [BenchScene.cpp](../../Source/Games/Empty/BenchScene.cpp) |
| `gv_benchMovePercent` | `int32` | `100` | 시험 | 프레임마다 위치·스케일을 다시 쓰는 큐브의 비율 (퍼센트, 기본 100) | [BenchScene.cpp](../../Source/Games/Empty/BenchScene.cpp) |
| `gv_benchRig` | `int32` | `0` | 시험 · 배포본에도 | 후처리 리그 데모 기사 수 (0=사용 안 함, 발 디딤 · 시선 · 왼손 IK · 망토 스프링) | [BenchSceneRig.cpp](../../Source/Games/Empty/BenchSceneRig.cpp) |
| `gv_benchRigEnabled` | `int32` | `1` | 시험 · 배포본에도 | 리그 데모의 PoseModifierComponent 를 켤지 (0=대조군) | [BenchSceneRig.cpp](../../Source/Games/Empty/BenchSceneRig.cpp) |
| `gv_benchRigView` | `int32` | `0` | 시험 · 배포본에도 | 리그 데모 카메라 (0 왼쪽 앞 · 1 정면 · 2 오른쪽 옆 · 3 오른쪽 뒤) | [BenchSceneRig.cpp](../../Source/Games/Empty/BenchSceneRig.cpp) |
| `gv_benchSpawnChurn` | `int32` | `0` | 시험 | 프레임마다 큐브 N 개를 지우고 같은 자리에 새로 만든다 (스폰·파괴·틱 레지스트리 측정) | [BenchScene.cpp](../../Source/Games/Empty/BenchScene.cpp) |
| `gv_benchTickMovers` | `int32` | `0` | 시험 | 큐브마다 틱 무버 컴포넌트 N 개 — 첫 번째가 틱 안에서 위치를 쓴다 (0=배치 쓰기) | [BenchScene.cpp](../../Source/Games/Empty/BenchScene.cpp) |
| `gv_benchTransparent` | `int32` | `25` | 시험 · 배포본에도 | 벤치 큐브 중 투명으로 만들 비율 (퍼센트) | [BenchScene.cpp](../../Source/Games/Empty/BenchScene.cpp) |
| `gv_benchUiMarkers` | `int32` | `0` | 시험 · 배포본에도 | 앞쪽 벤치 큐브 K 개에 화면 마커(숫자 글)를 붙인다 (0=사용 안 함) | [BenchScene.cpp](../../Source/Games/Empty/BenchScene.cpp) |
| `gv_benchViews` | `int32` | `0` | 시험 | 격자를 둘러보는 캡처 카메라(렌더 텍스처 512²) 수 (0=사용 안 함) | [BenchScene.cpp](../../Source/Games/Empty/BenchScene.cpp) |

## `Source/Games/HarvestValley`

| 이름 | 타입 | 기본값 | 종류 | 설명 | 정의 |
|---|---|---|---|---|---|
| `gv_farmAutoPlay` | `int32` | `0` | 시험 · 배포본에도 | HarvestValley: 농부도 AI 가 조종 (1=켜기) | [FarmDirectorComponent.cpp](../../Source/Games/HarvestValley/FarmDirectorComponent.cpp) |

## `Source/Games/MeadowVillage`

| 이름 | 타입 | 기본값 | 종류 | 설명 | 정의 |
|---|---|---|---|---|---|
| `gv_meadowAutoPlay` | `int32` | `0` | 시험 · 배포본에도 | MeadowVillage: 시계를 8 배로 흘려 날을 넘긴다 (1=켜기) | [MeadowFarmDirectorComponent.cpp](../../Source/Games/MeadowVillage/MeadowFarmDirectorComponent.cpp) |

## `Source/Games/NileCity`

| 이름 | 타입 | 기본값 | 종류 | 설명 | 정의 |
|---|---|---|---|---|---|
| `gv_nileAutoPlay` | `int32` | `0` | 시험 · 배포본에도 | NileCity: 자동 계획으로 도시를 짓고 돌리기 (1=켜기) | [NileDirectorComponent.cpp](../../Source/Games/NileCity/NileDirectorComponent.cpp) |

## `Source/Games/Shooter3D`

| 이름 | 타입 | 기본값 | 종류 | 설명 | 정의 |
|---|---|---|---|---|---|
| `gv_shooterAutoPlay` | `int32` | `0` | 시험 · 배포본에도 | Shooter3D: 조준 · 사격도 AI 가 (1=켜기) | [ShooterDirectorComponent.cpp](../../Source/Games/Shooter3D/ShooterDirectorComponent.cpp) |
| `gv_shooterMotionTrace` | `string` | — | 시험 | Shooter3D: 프레임마다 몸 · 본 · 카메라 · 적 자리를 CSV 로 (경로, 비면 끔) | [ShooterDirectorComponent.cpp](../../Source/Games/Shooter3D/ShooterDirectorComponent.cpp) |

## `Source/Games/StarSkirmish`

| 이름 | 타입 | 기본값 | 종류 | 설명 | 정의 |
|---|---|---|---|---|---|
| `gv_skirmishAutoPlay` | `int32` | `0` | 시험 · 배포본에도 | StarSkirmish: 두 플레이어 모두 AI 로 돌리기 (1=켜기) | [SkirmishDirectorComponent.cpp](../../Source/Games/StarSkirmish/SkirmishDirectorComponent.cpp) |

## `Source/Games/ThemeParkTycoon`

| 이름 | 타입 | 기본값 | 종류 | 설명 | 정의 |
|---|---|---|---|---|---|
| `gv_parkAutoBuild` | `int32` | `0` | 시험 · 배포본에도 | ThemeParkTycoon: 돈이 모이면 자동으로 짓기 (1=켜기) | [ParkDirectorComponent.cpp](../../Source/Games/ThemeParkTycoon/ParkDirectorComponent.cpp) |

## `Source/Games/VoxelCraft`

| 이름 | 타입 | 기본값 | 종류 | 설명 | 정의 |
|---|---|---|---|---|---|
| `gv_voxelAutoPlay` | `int32` | `0` | 시험 · 배포본에도 | VoxelCraft: 걷기 · 부수기 · 놓기도 AI 가 (1=켜기) | [VoxelDirectorComponent.cpp](../../Source/Games/VoxelCraft/VoxelDirectorComponent.cpp) |

## `Source/Server`

| 이름 | 타입 | 기본값 | 종류 | 설명 | 정의 |
|---|---|---|---|---|---|
| `gv_serverExitAfterTicks` | `int32` | `0` | 시험 · 배포본에도 | 이 틱 수를 돈 뒤 정상 종료 (0=끄기) | [ServerApp.cpp](../../Source/Server/ServerApp.cpp) |
