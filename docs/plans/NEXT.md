# 다음 세션 인계

새 세션은 이 문서를 먼저 읽습니다. 배치 경계마다 적용 담당이 고쳐 씁니다. 끝난 항목은 지웁니다(무엇을 했는지는 `git log`, 교훈은 영역 README · 결정 기록에 있다).

## 지금 상태 (2026-10-11)

- 구조 이동(Core 층 · GameFramework · Engine 폴더 · DevTools 해체 · Network 이동 · ModuleHost 분리), 도구(약어 · 이름 린트 · 빌드 속도 측정), 에디터 보강, 이름 정리(큰 이름 · 파일 분리 · 바이트 접미사 · 난수 통합)가 끝났다 — 계획 문서는 지우고 기각한 제안은 결정 기록 5-4 로 옮겼다.
- 전체 검증(2026-10-11, PC `SW`)은 린트 63/63 · Debug `-L nogpu` · `-L hostgpu` · Shipping · Server · 게임별 프리셋 여덟 · 대표 실행 네 백엔드가 통과했다. 이후 변경은 린트 + Debug `-L nogpu` + Shipping `-L nogpu` 로 확인했다.
- **CI**: 새 push 마다 진행 중인 실행이 취소돼 결과를 못 볼 수 있다 — push 는 묶어서 하고 실행이 끝나기를 기다린 뒤 다음을 올린다. 리눅스 구성은 zlib `ZLIB::ZLIBSTATIC` 대응(`6f6a441c9`)으로 통과했고, 리눅스 빌드는 `EpollStreamTransport` 의 `unique_ptr<EpollLoop[]>` 를 고쳤다(`37df145`) —
  리눅스는 이제 컴파일되고 테스트 단계에서 진다(2026-10-11 `954ed55cb` 실행). 고친 것: `FileUtil` 파일 시각 기원(libstdc++ `file_clock` 은 2174 기원) · POSIX 경로 구분자 정규화, 헤드리스(서버) 입력의 게임패드 슬롯 · 창 포커스, 서버 테스트의 `exec`(dash 의 `sh -c` 가 SIGTERM 에 먼저 죽어 143).
  고쳤고 CI 로 확인할 것: `OnlineLoadBotTest.TwoHundredBots…`(캐시 TTL 이 단조 시계라 느린 빌드에서 매칭 임대가 풀렸다 — 캐시를 서버 틱 시각에 맞춤),
  `EpollStreamTransport` 경합 보고(비 const `_listLoop[]` · 공유 읽기 버퍼가 검출기에 쓰기로 잡혔다), `StreamTransportTest.PlatformAbortWithNothingPendingStillCloses`(epoll 은 멈춘 동안 읽지 않는다 — 테스트를 모델별로).
  `ab65f898f` 를 push 했고 CI 실행이 진행 중이었다(결과 미확인). 포함: POSIX `toPath` 변환 · Shipping 데이터 테스트의 `editor/` 리소스 건너뜀 · 에디터 현지화 키 전용(`SW_LOCKEY`) · `sw::Color` 이전.
  2026-10-11 실행(`ab65f898f`) 결과: 윈도 네 잡 통과(Debug 미확인), 리눅스는 테스트 단계 실패 — 그 뒤 고친 것: ASan 둘(OpenSSL 스택 수명 · `NetRequestTest` 선언 순서), `ServerBootTest` 접속 로그(`GameNetSession::shutdown` 이 남은 사건을 로그), 진단 보강(`(nested)` 줄 표식 · 리바인드 테스트 프레임별 포커스). 다음 CI 로 확인할 것: `UIOptionsMenuTest.RebindIgnoresUIActionsWhileListening`(리눅스에서 jump=839 · 포커스 823=`Tab.controls`, 창이 닫힌 뒤 탐색 Up 한 번 발화로 추정 — 실패 메시지가 범인을 보인다), TSan 조각의 새니타이저 보고.
  새로 나온 Windows Debug `DelayLoadBindTest`(`GF_Mailbox.dll` 지연 임포트 미결속)는 로컬에서 통과한다 — 다시 나오면 원인을 판다. 끝난 워크트리(locA~commentpass)는 지웠다.
  **아직 남은 리눅스 실패**(원인 불명): `UIOptionsMenuTest.RebindIgnoresUIActionsWhileListening`(포커스 위젯 ID 839 ≠ 823, Debug · Shipping · ASan — 다음 CI 에서 실패 메시지가 포커스 위젯 이름을 보인다),
  Linux Debug `EngineTest_NoGPU` 조각의 180 초 한도 초과(멈춘 것이 아니라 느린 것 — 조각을 여섯으로 늘렸다, CI 로 확인), ASan 중단 두 건(`PlatformLoginTest.Oidc…` · `NetRequestTest.IdempotentRetry…`).
  Windows STL containers 잡은 sw 할당자 · 기록 해시에 기대는 테스트 셋(`MemoryTagTest` 둘 · `DestructionDamageTest.EventLogHash…`)을 건너뛰게 했다. 같은 테스트가 기본 컨테이너 구성에서 가끔 지는지는 지켜본다. Windows Debug · STL 잡의 액션 키트 테스트 실패는 일회성이 아니라 네 실행 연속으로 나는 간헐 실패다 — 백로그 "CI Windows Debug · STL 잡에서 액션 키트 테스트 넷이 자주 진다" 항목.
- **못 한 것**: 리눅스(CI 로), 실제 RenderDoc 캡처(이 PC 에 없다), 단언 대화상자 세 단추(모달), Tracy 0.14.1 뷰어 연결, 배율 1.5 · 2 에디터, Dialogue Graph 첫 열기, 리눅스 RPATH.
- 최근에 들어간 것: 이름 정리(`ResourceUtil` → `ResourcePaths` + `ResourceIO`, `EditorUtil` 경로 → `EditorPaths`, `Misc` → `None`, `CameraShakeRequest.h`, `_sizeBytes`),
  트레이스 내보내기 X1 · X2(`ProfilerTraceExport`, `profile.trace save | record`, 에디터 Save Trace, Shipping 제외 매크로 `SW_PROFILER_TRACE_ENABLED`). 마지막 커밋 `d5cc62f1e` 는 아직 push 하지 않았다.

## 다음 세션이 할 일 (순서대로)

작업 방식은 [작업 흐름](../11_Workflow.md). 도우미 여럿은 워크트리(`py -3 -m Scripts worktree-make <이름>`)에서만 일하고 push 하지 않으며, 적용 담당 하나가 `git merge --ff-only wt/<이름>` 으로 `main` 에 넣고 push 한다.
도우미가 동시에 빌드할 때는 `D:\Projects\Personal\LT-wt\BUILD-LOCK-README.txt` 의 슬롯 잠금을 쓴다. 검증은 큰 묶음 끝에 한 번, 사이에는 Debug 풀 컴파일 + 린트 게이트만. WSL(리눅스)은 CI 로 본다.

1. **CI 확인과 리눅스 빌드 오류 잡기.** 위 "CI" 항.
2. **트레이스 내보내기 — 끝.** 합치기(`profile.trace merge`) · 자동 첨부(성능 회귀 · 느린 틱 `-gv_traceSlowTickMs`)까지 들어갔고 계획 문서는 지웠다. 서버 · 클라이언트 트레이스를 모아 합치는 일은 서버 디버깅 S1, 메시지 흐름 화살표는 S5 에 있다.
3. **빌드 속도 2 ~ 6 단계.** 계획 [빌드 속도](BuildSpeed.md). 기준선은 `docs/08_Verification.md` "빌드 속도". 단계마다 같은 표로 전후를 남기고, 효과 없으면 되돌려 `docs/09_Decisions.md` 3절에 숫자와 함께 적는다. PCH 공유(3 단계)는 타깃마다 다른 정의
   (`SW_LOG_TAG` · `SW_EXPORTS`/`SW_IMPORTS` · 타깃별 `FlagOps.gen.h` 강제 포함)가 걸림돌이다 — 먼저 컴파일 정의 차이 목록을 뽑는다. 7 ~ 9 는 필요할 때만(사용자 결정).
4. **중복 정리 — 끝.** 결정적 난수 · 격자 사본 여섯 · 타이머(남은 7곳은 엔진 · 에디터라 그대로)는 끝났고, 키트 체력 직접 관리의 `Vitality` 이전은 하지 않기로 했다(결정 기록 5-8). 남은 격자 손셈(`Habitat` · `Overworld` 타일맵 · 게임 코드)은 파일 스키마가 얽혀 키트가 `getTopology()` 를 열 때 같이.
5. **엔진 분할.** 계획 [엔진 분할](EnginePartition.md) 0 단계(기준 측정) · 2 ~ 5 단계(작은 고리의 남은 `Resource → Animation`, `Object` 코어 / 기능 가르기, `OBJECT` 라이브러리 → DLL 승격, 월드를 값으로, 모듈 창구).
6. **서버 디버깅.** 계획 [서버 디버깅](ServerDebug.md) — S2(디버거 대기 · 서버 `pause`/`step`)는 들어갔다. 다음 S1 은 `Server` 에서 네트워크 서버를 여는 테스트 게임이 먼저 있어야 접속을 확인할 수 있다.
7. **6 차(주석을 새 문체로) — 병합 · push 끝(2026-10-12, `334cae46e`).** 영역별 `wt/c6*` 에이전트가 문장 단위로 다시 썼고 전부 main 에 병합했다. 지연 로드 제거(`wt/delayroot`) · `gomnav` · `cireport` · `c6res` 도 들어갔다.
   - **검증한 것**: Debug `all AllTests` 경고 0, lint 63/63, `-L nogpu` 47/47(샤드 둘은 Shipping 빌드와 겹쳐 시간 제한에 걸려 단독 재실행으로 통과), `ProfilerBackendTest` 7/7, Shipping 빌드 성공. 6차는 주석을 뺀 토큰열 비교로 코드 파일 약 1,100개가 같음을 확인했다.
   - **못 한 것**: Shipping `-L hostgpu`, 게임 프리셋(`Ninja-Debug-<Game>`)과 네 백엔드 대표 실행(지연 로드 제거가 핫 리로드 · 모듈 적재를 바꿨다), Debug `-L hostgpu` 결과 확인(실행은 했으나 로그 끝만 봐서 통과 여부를 못 읽었다 — 다시 돌려 `tests passed` 를 볼 것).
   - **Shipping 경고 둘**: `Source/Editor/Common/Commands/EditorNetPlaySession.cpp` 의 `unused variable 'pChild'`(344줄) · `unused member function 'toForwardSlashes'`(65줄) — Shipping 구성에서만 쓰이지 않는다. 고치는 방법을 정해 반영한다.
   - **방화벽 허용 창**(EngineTest.exe): 원인은 `ProfilerBackendTest` 가 올린 Tracy 였고 `334cae46e` 로 막았다(TCP 8086 은 loopback 에만, UDP 브로드캐스트 끔, vcpkg 포트 `tracy` port-version 2). 다른 PC 에서 이 변경을 받으면 `cmake --preset` 이 Tracy 포트를 다시 짓는다. Tracy 0.14.1 뷰어는 주소(`localhost:8086`)를 직접 입력해 연결하는지 한 번 확인한다.
   - **처리하지 못한 구역**(주석이 짧거나 시간이 모자라 건드리지 않은 곳; 같은 방식으로 `worktree-make c6<이름>` 후 이어서 하면 된다):
     - Engine/Graphics · Renderer: `Graphics/Mesh/MeshUtil.*`, RHI 하위(`OpenGLRHICommandContext.cpp`, `VulkanRHISwapChain.h` 등), 소형 파일 약 220개.
     - Engine/Object 등: `Scene.h`, `SceneManager.h/.cpp`, `ComponentDefaults.h`, `ResourcePackManager.h`, `SerializeContext.h`, `AssetDatabase.h`, `AnimationLOD.h`, `ContainerVisitor.h`, `ResourcePaths.cpp`, `SceneComponent.cpp`, `ReflectValue.h`, `MeshInstanceBatch.h`, 그리고 Spatial · Common 대부분. `SceneNavigation.h` · `GameObjectManager.h` 는 충돌 때문에 주석을 다시 쓰지 못했다.
     - Engine/UI 등: `Console/DebugHUDRegistry.h`(범위 오류로 되돌림), Compression, Observability, Profiling, Telemetry 전부.
     - Editor: 주석 25줄 이상 약 45개(`EditorPlayToolbar.cpp`, `EditorUIContext.h`, `EditorToolAssetCommands.cpp`, `SequencerPanel.*`, `AnimGraphPanel.cpp`, `TileMapPanel.cpp`, `EditorViewportOverlays.h` 등)와 그보다 짧은 파일. `EditorIconGlyphs.h` 는 생성 파일이라 제외.
     - GameFramework Base: 짧은 헤더 약 230개와 .cpp · .inl. Kits/Feature: 끝낸 파일 안에 옛 어휘(꼬리표 · 그새 · 거두다 · 짓)가 남은 파일 약 35개(`NetSimHarness.h`, `TurnRelay.h`, `AccountTypes.h`, `LoginStoreLogic.h`, `SocialService.h`, `VoxelMesher.*`, `VoxelTerrain.*`, `LeaderboardService.cpp`, `RespConnection.*` 등), Kits/Feature/Online 중 Account 등을 뺀 나머지 폴더 확인.
     - Core: Math, File, Process, Delegate, Memory, Diagnostics, CommandLine, Container, Task, Network 나머지(`NetHostSecurity.h`, `NetParallel.h`, `INetSecurityProvider.h`, `NetTransport.h`, `BitStream.h`), LogSink, String. RuntimeAPI 는 손대지 않았다. Games: `BenchScene.cpp` 일부, `BenchSceneRig.cpp`, VoxelCraft 전체, 나머지 컴포넌트.
     - '시험' → '테스트': `Scripts/**` · `Test/**` · `docs/Config/**` 278곳, 셰이더 주석 2곳(`waterwaveprobe.hlsl` · `lighting2d.hlsli`, 바꾸면 `App --cook-shaders` 재쿠킹 필요).
   - 끝난 뒤 8번(빌드 속도) 이후를 잇는다.

### 알아 둘 함정

- **vcpkg 가 갑자기 포트 전체를 다시 지으면**(20분 넘게): `VCPKG_BINARY_SOURCES` 가 `cmake --preset` 환경에만 있어 ninja 재구성에서 빠졌었다(`d649e08f4` 가 `Vcpkg.cmake` 에서 정한다). 그래도 재발하면 `Tools/vcpkg/packages/*/share/*/vcpkg_abi_info.txt` 의 `triplet_abi` 가 흔들리는지 본다. 설치 도중에는 건드리지 않는다.
- **`main` 체크아웃에서는 빌드 · 편집하지 않는다**(도우미가 워크트리에서 일한다). 테스트 실행 파일은 `AllTests` 를 지어야 최신이다(옛 exe 로 돌리면 모듈 ABI 불일치로 진다 — 지금 ABI v4).
- **새 소스 파일을 더한 뒤**에는 `cmake --preset Ninja-Debug` 로 다시 구성한다 — `EditorTest` 는 소스 목록(`Test/EditorTest/CMakeLists.txt`)을 손으로 들고 있다.
- **샤드를 동시에 돌리면**(`ctest -j3`) 시간 제한이 가짜로 진다 — 의심되면 하나만 다시 돌린다.
- **임시 파일**은 scratchpad 에서 에이전트 이름을 앞에 붙인다. 파이썬 조각은 heredoc 대신 파일로 쓴다(bash heredoc 은 따옴표 · 역슬래시를 망가뜨린다).
- **측정**은 다른 빌드가 도는 동안 오염된다. 측정 PC 이름(CPU)을 표에 적는다(이 PC 는 Ryzen 7 6800H).
- 사용 한도(`rate_limit`)로 도우미가 끊기면 워크트리 작업은 그대로 남는다 — 한도가 풀린 뒤 `SendMessage` 로 이어서 시킨다.
- 빌드 폴더 함정: 옛 대소문자 이름의 CMake 폴더(`Test/EditorUiTest`)가 남아 있으면 `-Wnonportable-include-path` 경고가 난다 — 그 폴더를 지우고 다시 구성한다.

### 에디터 작은 단위(E5 · G2 · G3 · H1)의 남은 것

남은 것: 리눅스 X11 · dlopen 갈래(CI), 실제 RenderDoc 캡처, 단언 대화상자 세 단추(모달), 스크린샷 토스트 "Show in Explorer" 단추(알림 관리자에 동작 단추가 없다),
`screenshotbutton` 시나리오가 `Bin/Saved/Screenshots` 에 남기는 PNG, Engine README 개발 명령 줄의 경로 확인.
RenderDoc 은 RHI 단계 맨 앞에서 올린다. `ThirdParty/renderdoc` 에 원문 헤더를 넣었고 그 폴더의 `.clang-format` 은 `DisableFormat` 이다.

## 검증할 것

- 리눅스 RPATH: CI 결과로 확인(사용자 결정 2026-10-10 — 로컬 WSL 빌드는 하지 않는다).
