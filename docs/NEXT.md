# 다음 세션 인계

> 질문: 무엇이 남았고 무엇부터 하는가?
> 독자: 이어서 일하는 세션(에이전트와 사람).
> 결론: 아래 **남은 일**만 본다 — 끝난 일은 git 기록과 계획 문서([StructureRefactor](StructureRefactor.md))에 있다. 순서는 1 → 6. 이 문서에는 끝난 일을 쌓지 않는다(끝나면 줄을 지운다).

## 일하는 법 (요약)

- 단계마다 코드 → `py -3 -m Scripts lint` → 커밋. 묶음 끝에 빌드(`cmake --build --preset Ninja-Debug`) · 단위(`build/Ninja-Debug/Bin/App.exe --test`) · 전체 `ctest`(build/Ninja-Debug, 약 5 분) 한 번.
- **테스트마다 로그의 Error · Warning 을 모두 분류한다** — `build/Ninja-Debug/Testing/Logs/*.log`, id 없는 줄과 `[id:…]` 줄을 나눠 테스트가 선언한 것인지 본다. 통과만 보지 않는다.
- 느린 곳은 새로 재지 말고 로그의 `[Scenario] … timeline:` · `stage reset took` · `Backend swap took` 줄을 모은다(`Test/README.md` 의 시간 기록).
- 규칙: 구조로 푼다 · 폴백 없음 · 게이트는 최후 · `#if` 대신 빌드 단위 · 용어는 `docs/agents/NamingCpp.md` 의 표. 시나리오 체계는 `Test/README.md` 가 정본.
- Linux 는 WSL 클론(`~/LT-linux`)에서 바뀐 파일의 문법 검사만 했다 — 실제 빌드 · 테스트는 아직.

## 남은 일

### 1. 진행 중 (커밋 전, 2026-10-31)

| 일 | 상태 |
|---|---|
| 고정 대기 → 조건 대기 | `<Wait N/>` + `<Expect/>` 483 곳을 `<WaitUntil … timeoutFrames="max(3N,60)"/>` 로 바꾼 뒤, 실제로 기다린 159 곳만 남기고 바로 참이던 304 · 돌지 않은 20 곳은 되돌렸다. 같은 줄이 둘인 파일 넷(viewporthover · viewportnavigation · visualizertoggle)은 손으로 확인. 바꾼 상태에서 `terrainbrush` · `flowterrain` 골든(8.5 % · 1.6 %)과 `rendertargetview` 의 `RenderTargetSnapshotReady` 가 졌다 — 되돌린 뒤 ctest 로 남는지 보고, 남으면 그 시나리오의 남은 조건 대기를 고정으로 |
| 폴리지 병렬 계산 경합 | `computeLayerInstances` 를 `const` 로(경합 감지기가 비-const 접근을 쓰기로 셌다) |
| 백엔드 교체 | 단계별 시간 로그(`Backend swap took`) · 텍스처 파일 읽기를 워커로(`RHIRenderResource::prepareRHI`). 실측: 교체 0.5~1.0 초 중 디바이스 생성 0.26~0.48 · 단계 다시 세우기 0.09~0.34 · 자원 다시 올리기 0.01~0.13 초 |

### 2. 테스트 · 시나리오 공백

| 일 | 내용 |
|---|---|
| 게임 시나리오 | Empty 외 11 게임은 시나리오 0. 옛 것(커밋 `27b17ee91^` 의 `Resource/game/<게임>/automation`) 약 46 개를 옮긴다 — shooter3d 19 · harvestvalley 5 · nilecity 4 · starskirmish 4 · meadowvillage 3 · wildwest · voxelcraft · themepark · frontiertown · abilityarena 각 2 · pixeltown 1. 변환: `<At>` → 순차(`<Wait>`/`<WaitUntil>`), `probe` → `watch`, 지표 문턱 → 골든, `WorldSettings` → `SceneSettings`. 활성 게임을 바꿔(`SW_ACTIVE_GAME`) 골든을 뜬다. 게임마다 독립이라 병렬 작업자로 나눌 수 있다. 엔진 쪽 옛 `rhiswap` 6 · `load` 2 · 에디터 `chain` 5 도 옮겨졌는지 본다 |
| 일정의 하루 | 일정 키트가 하루를 도는 게임 시나리오(`gv_timeScale` 16 배) |
| GameFramework · Kits 단위 테스트 | 정본 표 `PYTHONIOENCODING=utf-8 py -3 Scripts/dev/TestCoverage.py --uncovered GameFramework`. 남은 것: 온라인 Bus · Cache · Directory · Server · Service, Kits 전반. Engine: Asset/Image(DDS) · `PackCooker` · Serialization/Serializer · Reflection |
| 옛 단위 테스트 이식 | 옛 `Test/`(커밋 `3f70e665f^`, 약 950 파일) — 빠진 것 Core 56 · Engine 약 520 · Reflection 14. 순수 함수는 include · 위치만, 하네스(TestBench · 자식 프로세스 · 픽스처)에 기대던 것은 손으로 |
| 약한 편집기 시나리오 | `dialoguegraph`(캡처만 하고 판정 없음), `viewportpick`("카메라가 아님"만 본다 — 무엇이 골라졌는지) |
| 나머지 고정 대기 | `Screenshot` · `EditorClick` · `DevCommand` · `EditorKey` 앞의 큰 대기(바뀐 곳 목록은 `Wait` 다음 단계별 합계 — 시뮬레이션 진행 · 키 누름 유지는 고정이 맞다) |

### 3. 프레임 밖으로 뺄 일 (TaskManager — 조사 2026-10-31)

| 일 | 위치 |
|---|---|
| 요청 직후 `wait()` 해 다시 직렬이 되는 곳 — 로드 중 상태를 받아들이는 모양으로 | `Material.cpp:195` · `TextureCache.cpp:155` · `MeshCache.cpp:191` · `PrefabAsset.cpp:1169` · `SkeletalAnimatorComponent.cpp:391` · `CharacterAppearanceComponent.cpp:1109` |
| 교체 뒤 셰이더 미리 데우기(`emplaceParallel`, PSO 생성 스레드 안전성은 백엔드마다) · 지형 재생성 | `Engine.cpp` 교체 경로, `ShaderCache` |
| `DevCommand` 단계 평균 87 ms/프레임의 원인 | timeline 줄의 시나리오별 `DevCommand` |

### 4. 구조 개편 10 단계 남은 것 ([StructureRefactor](StructureRefactor.md) T5 · T6 · T8 · T9 · T10)

| 주제 | 남은 것 (코드에서 확인, 2026-10-31) |
|---|---|
| T5 셰이더 소스 계층 | `ShaderSourceID` · `ShaderIncludeResolver` · `ShaderBinaryLocation::make` · `IShaderBytecodeProvider`(Cooked · Live) · DDC 하나 — 아직 0 |
| T6 저장 파이프라인 | `GameInstance::saveStateToFile`(9 곳) · `Autosave` → `ILocalStore` 슬롯, 에디터 외형 프리셋 · 썸네일, `UserDataPath::resolve` → `LocalStoreLocation`, 키트 옵션 `Kind::FilePath`(0), 공급자 JSON 읽기 셋 → 하나, `ensureDevCertificate` 작업 폴더(4 곳), `ConfigManager` 세 후보 찾기 |
| T8 `#if` → 빌드 단위 | `App.cpp` 의 `SW_SHIPPING` 11 곳 → 개발 도구 함수 선언 하나에 Dev.cpp · Shipping.cpp 를 CMake 가 고른다(빌드마다 고정이라 인터페이스 · 가상 함수 없이), `AppAutomationHost` 두 구현 · 자동화 없는 빌드의 `-scenario` 기동 오류, 키트 개발 대체물 → DevOnline 확장점, 오디오 · 접근성 팩토리 → 등록표, `FileWatchDispatcher` |
| T9 생성 계약 | 타입 → 모듈 판정 넷 → CMake `--module` + `ReflectedTypes.list`, 계약 글자 → `ConfigConstants` 표 하나, 파서 엄격화(템플릿 이름 enum · 경고 후 건너뜀 → 오류 · `ReflectionSchema` 대상), OnlineLoadBot(UTF-8 매니페스트 · `LoadBot.online.json` · `<Load>` · `CommandLineManager` · 보고서 실패 종료 코드 · `--tls` 값 없음 = 개발 인증서 폴백 제거 · `--local-server` CTest) |
| T10 폴백 | `EditorPaths::resolveProjectRelativePath`(7 곳) · `EditorBakeOutputPath` 남은 것 확인 |

### 5. 그 뒤

- SOLID · 프로젝트 구성 점검과 구조 리팩토링(중복 제거 · 일원화 · 레이어 구분 · 최적화) — 사용자 요청 2026-10-31.
- Linux 실제 빌드 · 테스트(X11 · Wayland · ALSA · epoll 은 Windows 가 짓지 않는다 — CI 후보).
- `GameFramework/Base` · `Plugin/Editor/Base` 이름(모듈 이름 · 매니페스트가 따라간다).

### 6. 다음 설계 (긴 것)

- 빌드 속도: `MaterialPanel` · `PrefabPanel`(각 50 초대, Release)을 `-ftime-trace` 로 나눈다.
- 시나리오 범용화: 재생기 · 판정 층을 나눌 때 `AutomationSession`(이미 `ISubsystem` + `IScenarioPlayer`)에 인터페이스를 더 붙이지 않고 재생기를 별도 객체로. 시퀀서 이벤트 트랙이 시나리오를 부르는 쪽, 액션 단위 녹화 · 리플레이(고스트 · 킬캠). 단계 등록은 키트 · 게임이 아직 0.
- 리로드: 배포 `IAssetCache` 에 reload 가 없게 — 리로드는 DevTools 빌드 단위의 리로더 객체가 캐시를 참조해 공개 API(무효화 · 다시 요청)로 한다(구성, 캐시에 인터페이스를 더 붙이지 않는다 — 캐시 8 개가 다중 상속이 된다). `IAssetCache` 는 이름은 `I*` 인데 상태(`_stats`)를 가진 기반이다 — 이름을 내용대로(`AssetCacheBase` 등).
- Core 에 Engine 과 같은 층 껍데기(`#error`) 빌드 형식.
- 창 바둑판 간격의 창 틀 두께가 Win32 대략 값이다(`AutomationBackendMatrix`) — 플랫폼 창 틀 API 가 생기면.
- 전용 서버 종료 때 태그별 증가 진단(Game 약 0.5 KB) 출처.
