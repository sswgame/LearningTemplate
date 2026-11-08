# FolderMoves

> 질문: 폴더 · 파일 자리 정리에서 아직 남은 것은 무엇인가?
> 독자: 구조 정리 묶음을 하는 에이전트(통합 담당).
> 결론: 기준은 [FolderLayout](agents/FolderLayout.md)(린트 `folder-layout` · `acronyms` · `CoreTiers` 가 막는다). 이동 목록은 끝났고(2026-11-08 갈래 F), 아래는 **막는 것이 있어 구조를 먼저 바꿔야 하는 것**만 남긴다. 다 끝나면 이 문서를 지운다.

## 남은 것 — 막는 것과 풀 모양

| 항목 | 지금 | 막는 것 | 풀 모양 |
| --- | --- | --- | --- |
| `Engine/Profiling/Tracy/TracyProfilerBackend` → DevTools/Profiling/Tracy | `ProfilerBackend::startTracy` 가 Tracy 출력을 직접 만든다 | `ProfilerBackend` 의 정적 칸(지점 표 · 이름 저장소 · 활성 출력)은 프로세스 수명 바닥이라 그대로 둔다(`Config/Dev/MutableStaticExceptions.json`) — 엔진이 Tracy 형식을 아는 것만 남았다 | `startTracy` 를 지우고 DevTools `ProfilerLaunchSwitches` · 에디터 "Tracy 열기" 가 `TracyProfilerBackend` 를 만들어 `ProfilerBackend::setActiveBackend` 로 꽂는다 |
| `App/RuntimeUIHost` · `App/UserSettingsHost` → Engine(UI · UserSettings) | 엔진 헤더만 쓰는 엔진 일인데 App 에 있다 | `Engine/Engine.h`(층 8)를 받아 서비스를 꺼낸다 — 층 6 폴더가 못 본다 | `Engine&` 대신 필요한 서비스(UISystem · UserSettingsManager · RenderThread · IWindow)를 인자로 받게 한 뒤 옮긴다 |
| 엔진 쪽 `Dev/` 예외(`Config/Dev/FolderLayout.json` `_devFolderAllowed`) | ModuleManager/Dev · Host/Dev · App/Dev · GF GameData/Dev · Graphics/Material/Dev (+ 게임 Dev/ 는 영구) | DevTools 가 볼 수 없는 모듈 안(TargetLayers devtools 게이트), ModuleManager 는 정적 라이브러리(DevTools.dll 로 옮기면 상태가 둘), 머티리얼 그래프는 REFLECT 에셋(DevTools 에 리플렉션 단계가 없다) | ModuleManager 를 공유 라이브러리로(언리얼 HotReload 처럼 공유 모듈 관리자 위 Developer 모듈), DevTools 에 리플렉션 단계를 더하면 핫 리로드 · 머티리얼 그래프가 DevTools 로 간다 |
| 약어 `OK`, `gv_` 이름의 소문자 약어(`gv_editorUiScale` · `gv_meshLodForce`) | 그대로 | `OK` 는 약어 목록에 넣을지 정하지 않았다(`_bOk` 103 곳). `gv_` 이름은 명령줄 데이터(NamingCpp: 철자 유지) | `OK` 를 목록에 넣으면 같은 커밋에 전수로 |

## 판정해 그대로 두는 것(다시 보지 않는다)

- `Core/Foundation/BitFlagTrait.h` — 생성 헤더(`*.gen.h` · 강제 include `FlagOps.gen.h`)가 include 하는 잎이라 `<type_traits>` 밖을 끌어오면 모든 TU 의 include 누락을 가린다(헤더 머리말). EnumUtil 에 합치지 않는다.
- `Engine/UI/Screen/UIScreenSwitch` — 개발 화면 스위치의 **등록 틀(확장점)** 자체다. DevTools(`OptionsMenuSwitch`) · 게임(Empty Client/Dev)이 여기에 붙는다.
- `Core/Diagnostics/SubsystemTimingReport` — DevTools 가 아니라 Core 진단이다: 시계 걸기(개발 구성만)와 한 줄 요약뿐이고 Core 형식만 쓴다. 엔진 루프 · 에디터 기동 로그가 부른다(언리얼 `FScopedBootTiming` 도 Core).
- `Engine/Profiling/FrameProfileReport` — `FrameProfiler`(런타임 계측)가 내는 보고 값 형식. 보고를 쓰는 쪽(FrameProfileSession)은 DevTools 다.
- 그 밖의 판정(Process/ThreadCrashStack · Math/SecureRandom · Container/DependencyGraph · Container/string.h · Config/NetEmulationPresetTable · Foundation/AnimationNotifyPhase · Observability ↔ Telemetry · Compression 둘 · 쿠킹 셋 · Online 이름 겹침)은 FolderLayout 정본과 같다.
