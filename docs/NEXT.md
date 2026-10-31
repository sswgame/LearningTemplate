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

### 1. 진행 중 (2026-10-31)

| 일 | 상태 |
|---|---|
| 통합 검증 | T5 · T6 · T8 · T9 · TaskManager 오프로드 · GF 테스트 · 기록 대조 · 셰이더 파일 이름(`_` 구분)을 main 에 모았다. 빌드 · 단위 통과, 전체 ctest(셰이더 캐시를 지운 첫 실행) 결과를 분류한다 |
| 컴포넌트의 편집기 기능 떼기 | 작업자 브랜치 `worktree-agent-a58a7c97e4850070b`(커밋 10, 빌드 전) — 메시 쪼개기 · PO 파일 · 커브 범위 · 자동화 등록을 편집기 · 옆 파일로. 가져와 빌드 · 테스트(편집기 DLL 의 엔진 기호 링크, 결정성 표시 configure). 남긴 것: `DialogueRunnerComponent::previewLine` · `_lastFitResult` · `UIResolutionPresetTable` · 대화 노드 표시 칸, 호출자 없는 `AIDirector::explain` |
| 게임 골든 | 옮긴 게임 시나리오의 골든을 활성 게임을 바꿔(`SW_ACTIVE_GAME`) 뜨고 게임 체인을 돌린다 — shooter3d 31 · themepark 5 · wildwest 2 |

### 2. 테스트 · 시나리오 공백

| 일 | 내용 |
|---|---|
| 일정의 하루 | 일정 키트가 하루를 도는 게임 시나리오(`gv_timeScale` 16 배) |
| GameFramework · Kits 단위 테스트 | 정본 표 `PYTHONIOENCODING=utf-8 py -3 Scripts/dev/TestCoverage.py --uncovered GameFramework` 로 남은 것을 본다. CardGame · Poker 테스트는 Release + `SW_BUILD_UNUSED_KITS` 에서만 짓는다. Engine: Asset/Image(DDS) · `PackCooker` · Serialization/Serializer · Reflection |
| 옛 단위 테스트 이식 | 옛 `Test/`(커밋 `3f70e665f^`, 약 950 파일) — 빠진 것 Core 56 · Engine 약 520 · Reflection 14. 순수 함수는 include · 위치만, 하네스(TestBench · 자식 프로세스 · 픽스처)에 기대던 것은 손으로 |
| 나머지 고정 대기 | `Screenshot` · `EditorClick` · `DevCommand` · `EditorKey` 앞의 큰 대기(시뮬레이션 진행 · 키 누름 유지는 고정이 맞다) |

### 3. 프레임 밖으로 뺄 일 (TaskManager — 조사 2026-10-31)

| 일 | 위치 |
|---|---|
| 요청 직후 `wait()` 해 다시 직렬이 되는 곳 — 로드 중 상태를 받아들이는 모양으로 | `Material.cpp:195` · `TextureCache.cpp:155` · `MeshCache.cpp:191` · `PrefabAsset.cpp:1169` · `SkeletalAnimatorComponent.cpp:391` · `CharacterAppearanceComponent.cpp:1109` |
| 교체 뒤 셰이더 미리 데우기(`emplaceParallel`, PSO 생성 스레드 안전성은 백엔드마다) · 지형 재생성 | `Engine.cpp` 교체 경로, `ShaderCache` |
| `-gv_screenshot` 파일 쓰기가 아직 렌더 스레드 | `takeCommandLineScreenshotIfDue` — 실행 중 스크린샷(`runScreenshotFileWrite`)과 같은 잡으로 |
| `DevCommand` 단계 평균 87 ms/프레임의 원인 | timeline 줄의 시나리오별 `DevCommand` |

### 4. 구조 개편 10 단계 남은 것 ([StructureRefactor](StructureRefactor.md) T6 · T8 · T9 · T10)

| 주제 | 남은 것 (2026-10-31) |
|---|---|
| T6 저장 파이프라인 | 게임 상태 세이브(`saveStateToFile`) · `Autosave` → `ILocalStore` 슬롯은 런타임 저장소 주인이 먼저 있어야 한다 |
| T8 `#if` → 빌드 단위 | 같은 `#if` 모양이 남은 곳: `EditorModuleManager` · `RHIBackendSwitcher` · `ServerApp` · `GameModuleManager` · `ModuleReloadDriver`. 배포(Shipping) 빌드 확인 — 오디오 · 접근성 정적 등록이 `/WHOLEARCHIVE` 에 기댄다 |
| T9 생성 계약 | 런타임 모듈 이름 = CMake 대상 이름 가정을 핫 리로드 시나리오로 확인. 같은 모양 후보: 전역 변수 · 데이터 종류 등록의 올리는 쪽 이름, `FixedSchemas.gen.txt` 가 생성 글을 다시 읽음 |
| T5 셰이더 | `verifyDomain` 의 도메인 단위 `shaders/bin` 경로 잇기 하나 |
| T10 폴백 | `EditorBakeOutputPath`(7 곳) 남은 것 확인 |

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
