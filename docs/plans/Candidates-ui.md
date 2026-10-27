# UI 후보

> 상태: 후보 · 선행: 없음 · 영역: ui · 옛번호: G-13, D6-4 · 진행: 해당없음

이 문서는 착수 전 후보의 목록이다. 착수하면 절을 계획 문서로 옮기고 여기서 지운다. 런타임 UI, 글자 렌더링, 접근성, DPI 가 범위다.
글리프 래스터화 히치와 그리기 성능의 남은 것은 결정이 [결정 기록](../decisions/5-7_RuntimeUI.md) 5-7 에 있고 측정 조건은 아래 조건부 절(`GlyphRasterBudgetValue` · `UiDrawCacheInvalidation`)이 가진다.

## 기능

### UiOptionsMenuVerification
제목: 옵션 · 일시정지 메뉴의 실기동 확인
옛번호: D6-4
왜 필요한가: `Engine/UI/README.md` 의 옵션 · 일시정지 메뉴 중 확인이 남았다.
남은 것:
- 실제 게임에서 "타이틀로" 단추를 눌러 타이틀 씬으로 가는 실기동: 타이틀 씬(`titleScene`)을 가진 게임이 아직 없어 어느 게임에서도 접혀 있다(2026-10-27 `Resource` 의 어느 게임 `gamesettings.xml` 에도 `titleScene` 이 없다). 단추 보임 · 눌림은 `UIOptionsMenuTest.PauseMenuSessionButtonsFollowGlobalCommands`, 게임 쪽 등록 · 씬 로드는 `GameFrameworkTest.GameInstanceBaseReturnToTitleFollowsTitleScene`(가짜 설정과 임시 씬)가 본다. 타이틀 화면을 만드는 게임이 생기면 그 게임으로 시나리오를 만든다.
2026-10-27 자동화: 키 다시 잡기와 재시작 뒤 유지를 옮겼다 — `game/shooter3d/automation/rebind.scenario.xml`(옵션 메뉴와 같은 길로 점프 키를 바꾸고 가상 패드 A 는 그대로), `UserSettingsTest.KeyBindingSurvivesRestart`(저장 → 새 설정 관리자 · 새 입력 맵), 진짜 두 번 기동 `py -3 -m Scripts scenario-seq …/sequence/rebindsave …/sequence/rebindrestart`(매니페스트 `shooter3d-keyrebind-scenario` · `usersettings-keybinding-restart` · `shooter3d-keyrebind-restart`). 패드 실기 입력은 가상 패드(`device="gamepad"`)가 같은 입력 길을 지나므로 따로 필요 없다.
못 하는 이유: 타이틀 씬을 가진 게임이 없다. 풀리는 조건: 타이틀 씬을 가진 게임이 생길 때.

### DetachedGameViewKeyboard
제목: 떼어 낸 게임 뷰 창의 키보드를 실제 OS 창에서 확인
옛번호: D6-2
왜 필요한가: 에디터에서 게임 뷰 패널을 다른 OS 창(ImGui 보조 뷰포트)으로 떼면 키 · 글자는 `GameViewPanel::forwardDetachedKeyboard` 가 게임 입력으로 넣는다. 키 표(`ImGuiKeyForward`)와 "누른 채 포커스를 잃으면 짝 뗌을 넣고 큐가 차면 다시 시도한다"는 규칙(`DetachedKeyForwarder`, 2026-10-27 패널에서 꺼냄)은 `ImGuiKeyForwardTest` 가 본다. 남은 것은 보조 OS 창이 실제로 포커스를 가진 채 ImGui 백엔드가 그 창의 키를 받아 이동 키가 게임에 닿는지다.
못 하는 이유(2026-10-27 재확인): 자동화 단계로 만들 길이 없다. `PostWindowMessage` · `requireForeground` 는 메인 창만 다루고, 보조 뷰포트 창은 ImGui 가 사용자의 끌기로만 만들며(패널을 떼는 공개 호출이 없다) 그 창으로 OS 포커스를 옮기는 것은 사용자 세션의 전경 창 규칙을 따른다. 풀리는 조건: 사람이 있는 Windows 에디터에서 패널을 떼어 이동 키를 누르거나, 보조 뷰포트를 코드로 만들고 전경으로 가져오는 길이 생길 때.

### AtSpiBridge
제목: 리눅스 AT-SPI 스크린 리더 브리지
옛번호: G-9
왜 필요한가: Windows 는 UI Automation 브리지가 있고([결정 기록 5-7](../decisions/5-7_RuntimeUI.md) A4), 리눅스 클라이언트에는 Orca 같은 스크린 리더에 닿는 길이 없다. 접근성 트리(`UIAccessibilityTree`) · 브리지 계약(`IUIAccessibilityBridge`)은 플랫폼과 무관해 AT-SPI 구현만 비어 있다.
착수 조건: 리눅스 클라이언트가 제품 대상이 되고 Orca + at-spi2 를 올린 세션(CI 에 xvfb + dbus + orca 를 띄우거나 실기)을 확보할 때.
2026-10-27 구현(미확인): `Engine/UI/Accessibility/Linux/AtSpiAccessibilityBridge.cpp` 가 libdbus-1 로 `Accessible` · `Component` · `Action` · `Value` 객체와 레지스트리 등록(`Socket.Embed`), 포커스 · 알림 신호를 낸다. 역할 · 상태 번호 규칙(`AtSpiMapping`)은 `AtSpiMappingTest` 가 본다. `-DSW_ENABLE_ATSPI=ON`(libdbus-1-dev) 일 때만 짓고 기본은 끈다 — 리눅스에서 지어 본 적도 Orca 에 붙여 본 적도 없다.
남은 것: 리눅스 CI 에서 `SW_ENABLE_ATSPI=ON` 으로 짓기(컴파일 오류가 있으면 고친다), Orca + at-spi2 세션에서 트리 · 포커스 · 누르기 확인, 이벤트 서명(`siiva{sv}`)과 알림(`Announcement`)의 값이 at-spi2-core 판과 맞는지 확인, 사각형에 창 위치를 더해 화면 좌표로 맞추기(지금은 창 클라이언트 기준이라 확대기 · 강조 상자가 창 위치만큼 어긋난다).
함정: 스냅샷을 UIA 와 같은 방식으로 쓴다(브리지는 위젯을 쥐지 않고 (화면 핸들, 위젯 번호)로 찾는다). 붙은 스크린 리더가 없으면 일하지 않는 규칙(`isClientActive`)도 같다.

## 조건부

### GlyphRasterBudgetValue
제목: 글리프 래스터 예산 값의 확정
옛번호: C-9
왜 필요한가: 새 글리프 래스터화는 프레임 예산(`gv_uiGlyphRasterBudgetMicros`, 기본 4000)으로 묶었다(결정 기록 5-7). 한글 400 글리프는 프레임당 두 개 안팎이라 200 프레임쯤 걸린다 — 길면 값을 올려 `GT.Frame` max 와 나타나는 속도의 균형을 정한다.
착수 조건: `perf-compare --cases hangulGlyphBudget`(매니페스트 id `glyph-hangul-hitch-measure`, 제한 없음 0 대 4000 us)의 `GT.Frame` max 가 옛 784 ms 에서 크게 줄지 않거나 글자가 나타나는 속도가 느릴 때.
남은 것: 값을 올려 균형점을 재고 기본값을 바꾼다. 글이 모두 나온 뒤에 찍히는지는 `UI.DeferredGlyphs` 탐침 대기가 맡는다. 함정: `-lang=ko` 로 재야 한다(영어 문화권 대체 글꼴 사슬에는 한글 글꼴이 없다).

### UiDrawCacheInvalidation
제목: 패널마다 하위 출력 캐시와 조상 변환을 캐시 밖에서 곱하기
옛번호: D6-6
왜 필요한가: 목표(`GT.UI` p50 0.3 ms) 안인 값을 더 줄이는 후보 둘이다 — U1 패널마다 하위 출력 캐시(Slate Invalidation Panel), U2 조상의 `kTransform` 이면 자손까지 다시 칠하는 그림 캐시의 변환을 캐시 밖에서 곱하기. 둘 다 그림 캐시의 무효화 계약(조상 불투명도 · 자르기가 캐시에 반영됨)을 바꾸는 일이라 무효화 누락이라는 새 결함 모양이 생긴다(결정 기록 5-7).
착수 조건: 매니페스트 id `ui-draw-performance-measures`(`perf-compare` 의 `uiDrawStill` · `uiDrawChurn` · `uiDrawOpacityPulse`, 위젯 1 만)의 세 측정 중 하나라도 `GT.UI.Paint` + `GT.UI.Layout` p50 합이 0.3 ms 를 넘을 때 — 멈춘 화면 · 일부만 바뀜은 U1, 조상 변환은 U2 를 구현한다. 모두 안이면 이 절을 지운다.

### MarkerComputeProjection
제목: 화면 마커의 투영 · 컬링을 컴퓨트로
옛번호: G-13
왜 필요한가: 이름표 2,000 개의 남은 게임 스레드 몫 `GT.UI.Markers` p50 0.49 ~ 0.52 ms(마커마다 월드 위치 읽기 · 투영 · 앵커 쓰기. 위치 · 배율 적용은 이미 GPU 앵커다 — [결정 기록](../09_Decisions.md) 5-7).
착수 조건: 게임 HUD 에서 마커가 2,000 개를 넘거나 이 구간이 0.5 ms 를 꾸준히 넘을 때. 구간의 측정은 이미 자동이다(`perf-compare` 의 `uiNameplates` 케이스, 매니페스트 `perf-compare-ui` 의 GT.UI.* 합 0.5 ms 한도) — 조건의 앞쪽(게임 HUD 의 마커 수)은 게임 내용의 사실이라 측정할 수 없다.
남은 것: 컴퓨트가 GPUScene 변환을 읽어 앵커 표를 쓰는 모양이다(마커 ↔ GPU 인스턴스 대응이 필요하다). 데미지 숫자처럼 시간 함수로만 움직이는 UI 의 GPU 애니메이션도 같은 길이다.
함정: 측정은 Release `App -gv_benchMeshes=2000 -gv_benchUiNameplates=2000 -gv_profileFrames=600` 의 `GT.UI.Markers` · `GT.UI.Paint`.
2026-10-24 확인: 마커를 쓰는 곳은 벤치 장면(`Source/Games/Empty/BenchScene*`)뿐이라 게임 HUD 에는 조건이 닿지 않았다. 그대로 둔다(2026-10-27 재확인: `Source/Games` 에서 마커는 여전히 `Empty` 벤치뿐이다). 컴퓨트 패스 · 셰이더 · 마커 ↔ GPU 인스턴스 대응이 렌더러 영역이다.
