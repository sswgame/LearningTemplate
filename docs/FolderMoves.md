# FolderMoves

> 질문: 폴더 · 파일 자리 정리에서 아직 남은 것은 무엇인가?
> 독자: 구조 정리 묶음을 하는 에이전트(통합 담당).
> 결론: 기준은 [FolderLayout](agents/FolderLayout.md)(린트 `folder-layout` · `acronyms` · `CoreTiers` 가 막는다). 이동 목록은 끝났고(2026-11-08 갈래 F), 아래는 **막는 것이 있어 구조를 먼저 바꿔야 하는 것**만 남긴다. 다 끝나면 이 문서를 지운다.

## 남은 것 — 막는 것과 풀 모양

| 항목 | 지금 | 막는 것 | 풀 모양 |
| --- | --- | --- | --- |
| `Engine/Profiling/SubsystemTimingReport` → DevTools/Profiling | Engine.cpp 가 기동 · 종료 시간 요약을 이름으로 부른다 | 엔진 루프가 개발 보고를 직접 부른다 | 기동 단계 확장점(`EngineStartupExtension`)에 "서브시스템 시간 관찰자" 를 두고 DevTools 가 붙는다 — ProfilerTraceExport 를 프레임 끝 훅으로 옮긴 것과 같은 모양 |
| `Engine/Profiling/Tracy/TracyProfilerBackend` → DevTools/Profiling/Tracy | `ProfilerBackend` 가 정적 칸으로 Tracy 를 고른다 | 그 정적 칸은 갈래 M(가변 정적 → 주인 멤버) 범위 | M 이 `ProfilerBackend` 를 서브시스템 멤버로 바꾼 뒤, DevTools `ProfilerLaunchSwitches` 가 `IProfilerBackend` 를 등록한다 |
| `Graphics/View/IEngineRenderDriver.h` → `Presentation` | 창구 셋(UI · Audio · Render) 중 하나만 Graphics(층 4) | `FrameViewports` 등 Graphics 형식을 값으로 쓴다(층 2 가 못 본다) | 그 형식을 앞 선언 · 포인터로 바꾸고 옮긴다(include 6) |
| `App/RuntimeUIHost` · `App/UserSettingsHost` → Engine(UI · UserSettings) | 엔진 헤더만 쓰는 엔진 일인데 App 에 있다 | `Engine/Engine.h`(층 8)를 받아 서비스를 꺼낸다 — 층 6 폴더가 못 본다 | `Engine&` 대신 필요한 서비스(UISystem · UserSettingsManager · RenderThread · IWindow)를 인자로 받게 한 뒤 옮긴다 |
| `GameFramework/Runtime/Foundation/Random/ValueNoise` → Core/Math | GF | `GameHash`(GF)를 쓴다 | 좌표 해시를 `Core/Hash/HashUtil` 로 내린 뒤 옮긴다 |
| 엔진 쪽 `Dev/` 예외(`Config/Dev/FolderLayout.json` `_devFolderAllowed`) | ModuleManager/Dev · Host/Dev · App/Dev · GF GameData/Dev · Graphics/Material/Dev (+ 게임 Dev/ 는 영구) | DevTools 가 볼 수 없는 모듈 안(TargetLayers devtools 게이트), ModuleManager 는 정적 라이브러리(DevTools.dll 로 옮기면 상태가 둘), 머티리얼 그래프는 REFLECT 에셋(DevTools 에 리플렉션 단계가 없다) | ModuleManager 를 공유 라이브러리로(언리얼 HotReload 처럼 공유 모듈 관리자 위 Developer 모듈), DevTools 에 리플렉션 단계를 더하면 핫 리로드 · 머티리얼 그래프가 DevTools 로 간다 |
| 약어 `OK`, `gv_` 이름의 소문자 약어(`gv_editorUiScale` · `gv_meshLodForce`) | 그대로 | `OK` 는 약어 목록에 넣을지 정하지 않았다(`_bOk` 103 곳). `gv_` 이름은 명령줄 데이터(NamingCpp: 철자 유지) | `OK` 를 목록에 넣으면 같은 커밋에 전수로 |

## 판정해 그대로 두는 것(다시 보지 않는다)

- `Core/Foundation/BitFlagTrait.h` — 생성 헤더(`*.gen.h` · 강제 include `FlagOps.gen.h`)가 include 하는 잎이라 `<type_traits>` 밖을 끌어오면 모든 TU 의 include 누락을 가린다(헤더 머리말). EnumUtil 에 합치지 않는다.
- `Engine/UI/Screen/UIScreenSwitch` — 개발 화면 스위치의 **등록 틀(확장점)** 자체다. DevTools(`OptionsMenuSwitch`) · 게임(Empty Client/Dev)이 여기에 붙는다.
- `Engine/Profiling/FrameProfileReport` — `FrameProfiler`(런타임 계측)가 내는 보고 값 형식. 보고를 쓰는 쪽(FrameProfileSession)은 DevTools 다.
- 그 밖의 판정(Process/ThreadCrashStack · Math/SecureRandom · Container/DependencyGraph · Container/string.h · Config/NetEmulationPresetTable · Foundation/AnimationNotifyPhase · Observability ↔ Telemetry · Compression 둘 · 쿠킹 셋 · Online 이름 겹침)은 FolderLayout 정본과 같다.
