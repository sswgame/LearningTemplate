# App 구조 정리 (2026-10-22)

> 상태: 구현중 · 선행: 없음 · 영역: engine · 옛번호: 없음 · 진행: 6/7

`Source/App` 은 형식 10 · 파일 22 였다. 같은 일을 App 과 Server 가 따로 구현하고(리로드 소유 · 표준 입력 명령 · 호스트 범위 세우기), 자동화가 호스트 객체를 찾는 길이 정적 포인터였으며,
"N 번째 프레임에 무엇을 한다" 스위치가 세 벌이었다. 결정은 [결정 기록](../09_Decisions.md) 5-10 의 2026-10-22 세 줄이다.

## 1. 근본 원인

1. **호스트 객체를 찾는 창구가 정적 포인터였다.** `RHIBackendSwitcher::findActive` · `ShellInputActions::findActive` · `DebugHUD::findActive` · `EditorConfig::getActiveInstance` 와
   `AppAutomation` 의 `s_pLiveReloadManager` 는 "지금 도는 것" 을 전역 칸에 적어 두었다. 범위(`SubsystemScope`)가 부모를 올라가 찾는 창구가 이미 있는데 쓰지 않았다.
2. **App 과 Server 가 공용 폴더(`Source/ModuleHost`)를 두고도 호스트 일을 각자 적었다.** 리로드 매니저의 수명과 씬 뒤 훅, `quit` · `stop` 낱말, 호스트 범위의 세 줄이 두 벌이었다.
3. **시점을 고르는 수단이 시나리오 단계와 명령줄 스위치로 갈렸다.** 스위치마다 프레임 카운터와 관찰자 함수를 따로 들었다.

## 2. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| A1 | `AppAutomation.h/.cpp` 를 `ModuleReloadDriver.cpp` 로 합치고 구동기를 `Source/ModuleHost` 로 옮긴다. 정적 칸 둘(`s_pLiveReloadManager` · `s_reloadTargetCount`)은 구동기 필드가 되고, 구동기가 자신을 호스트 범위에 건다(`SubsystemScope::bind`). 개발 명령 `module.reload` 와 탐침 `Module.*` 이름은 그대로다 | 끝(빌드) |
| A2 | `RuntimeUIHost` 를 App 의 함수 셋(`installRuntimeUIHooks` · `removeRuntimeUIHooks` · `onQuitGameCommand`)으로 합친다. D6-4.1 의 주어를 고쳤다 | 끝(빌드) |
| A3 | 정적 활성 창구를 범위 `bind` 로 — 교체기(App 의 호스트 범위), 셸 액션(엔진 서비스 범위), 디버그 HUD(그것을 든 `UISystem::getDebugHUD`), 에디터 테마 설정(환경설정 등록 줄의 인스턴스 `getPreferences<EditorConfig>()`). 게이트 `CheckServiceAccess` 의 `ServiceAccess/ActiveWindow` 가 헤더의 `static T* findActive…()` · `getActive…()` 를 막고, 남는 것은 예외 표에 이유와 함께 둔다 | 끝(빌드) |
| A4 | "N 번째 프레임" 스위치 셋(`gv_reloadGameAtFrame` · `gv_rhiSwapAtFrame`/`gv_rhiSwapTo` · `gv_userSettingsApplyFrame`)을 지우고 시점은 시나리오 단계가 고른다. `AppSmokeTest` 의 교체 두 케이스는 시나리오 `engine/automation/rhiswap/swapother.scenario.xml` 로 건다. 교체기의 빈 관찰자 함수와 `UserSettingsHost` 의 프레임 카운터가 사라졌다 | 끝(빌드 · 실행 미확인) |
| A5 | 두 번째 파동 — App · Server 중복을 `Source/ModuleHost` 한 곳으로: 호스트 범위 세우기(`ModuleImageHostSubsystem::initializeHostScope`), 리로드 소유(A1 의 구동기를 Server 도 쓴다 — Server 가 `module.reload` · `Module.*` 를 얻는다), 표준 입력 명령 채널(`StandardInputCommands`) | 끝(빌드) |
| A6 | 낡은 주석 — `RHIBackendSwitcher` 의 "LiveReloadManager 를 소유한 루프", `App.h` 의 `steady_clock` | 끝 |
| A7 | 남은 활성 창 `IWindow::getActiveWindow` · `setActiveWindow` 를 범위로 — Engine 가 만든 창을 App 에 넘기는 소유권 전달과 입력 · RHI 의 관찰 포인터를 함께 쓴다(쓰는 곳 약 40 곳 · 테스트 10 곳). 소유권 전달은 `Engine` 의 반환값으로, 관찰은 엔진 범위의 창 서비스로 나누는 안이다 | 할 일 |

## 3. 남은 것

- A4 의 실행 확인: `AppSmokeTest.BackendSwapFollowsTheNewDevice` · `BackendSwapRebuildsGameBeforeEditor`(GPU, DX11 · DX12)와 `rhiswap/swapbackends` · `editor/previewswap` 시나리오. 표준 입력 `quit`(에디터 멀티 프로세스 재생)과 Server 의 `quit` · `stop` · `status`.
- 예외 표에 남긴 활성 창구: `MemoryProfiler` · `CompressionCodecRegistry`(Core 층 — 엔진 범위를 모른다), `ProfilerBackend`(잠금 없이 렌더 · 작업 스레드가 읽는다), `IWindow`(A7).
  `GameInstanceBase::findActiveObjectManager` · `GameAutoplay::findActive` 는 이름만 같고 정적 포인터를 들지 않는다.
- 같은 모양의 다른 가족: 설정 활성본 `EngineConfig::getActive` · `GameConfig::getActive` · `Render2DSettings::getActive`(참조를 돌려주는 정적 사본). 게이트는 포인터만 본다. 범위로 옮길지는 이 문서 밖의 일이다.
