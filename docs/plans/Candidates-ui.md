# UI 후보

> 상태: 후보 · 선행: 없음 · 영역: ui · 옛번호: G-13, D6-4 · 진행: 해당없음

이 문서는 착수 전 후보의 목록이다. 착수하면 절을 계획 문서로 옮기고 여기서 지운다. 런타임 UI, 글자 렌더링, 접근성, DPI 가 범위다.
계획 문서로 옮긴 것: [GlyphRasterizationHitch](GlyphRasterizationHitch.md)(새 글리프 래스터화 히치) · [UiDrawPerformance](UiDrawPerformance.md)(그리기 성능의 남은 것).

## 기능

### UiOptionsMenuVerification
제목: 옵션 · 일시정지 메뉴의 실기동 확인
옛번호: D6-4
왜 필요한가: `Engine/UI/README.md` 의 옵션 · 일시정지 메뉴 중 확인이 남았다.
남은 것:
- 키 바인딩이 재시작 뒤에도 유지되는지의 실기동 확인(Shooter3D · 패드): 두 번 기동해야 하고 사용자 설정 파일을 쓰므로 시나리오 하나로 못 한다(15 초 되돌림은 `engine/automation/displayrevert.scenario.xml` 이 본다).
- "타이틀로" 단추: 타이틀 씬(`titleScene`)을 가진 게임이 아직 없어 어느 게임에서도 접혀 있다. 타이틀 화면을 만드는 게임이 생기면 그 게임으로 확인한다.
못 하는 이유(2026-10-24): 패드 실기 입력과 두 번 기동이 필요하고, 타이틀 씬을 가진 게임이 없다. 풀리는 조건: 패드가 있는 실기에서 두 번 기동해 보거나 타이틀 씬을 가진 게임이 생길 때.
2026-10-27 재확인: `Resource` 의 어느 게임 `gamesettings.xml` 에도 `titleScene` 이 없다. 그대로 둔다.

### DetachedGameViewKeyboard
제목: 떼어 낸 게임 뷰 창의 키보드를 실제 OS 창에서 확인
옛번호: D6-2
왜 필요한가: 에디터에서 게임 뷰 패널을 다른 OS 창(ImGui 보조 뷰포트)으로 떼면 키 · 글자는 `GameViewPanel::forwardDetachedKeyboard` 가 게임 입력으로 넣는다. 키 표(`ImGuiKeyForward`)는 `ImGuiKeyForwardTest` 가 보지만, 보조 OS 창이 포커스를 가진 채 이동 키가 게임에 닿는지와 키를 누른 채 창을 바꿔도 키가 눌린 채 남지 않는지는 실제 창이 있어야 한다.
못 하는 이유(2026-10-24): 보조 뷰포트 OS 창을 만들고 그 창으로 OS 포커스를 옮기는 일은 무인 실행 안에서 할 수 없다(포커스는 사용자 세션의 전경 창 규칙을 따른다). 풀리는 조건: 사람이 있는 Windows 에디터에서 패널을 떼어 이동 키를 누르거나, 시나리오 실행기가 보조 창을 전경으로 가져오는 길(`requireForeground` 의 확장)이 생길 때.

### AtSpiBridge
제목: 리눅스 AT-SPI 스크린 리더 브리지
옛번호: G-9
왜 필요한가: Windows 는 UI Automation 브리지가 있고([ScreenReaderSupport](ScreenReaderSupport.md) A4), 리눅스 클라이언트에는 Orca 같은 스크린 리더에 닿는 길이 없다. 접근성 트리(`UIAccessibilityTree`) · 브리지 계약(`IUIAccessibilityBridge`)은 플랫폼과 무관해 AT-SPI 구현만 비어 있다.
착수 조건: 리눅스 클라이언트가 제품 대상이 되고 Orca + at-spi2 를 올린 세션(CI 에 xvfb + dbus + orca 를 띄우거나 실기)을 확보할 때.
남은 것: D-Bus 위의 `org.a11y.atspi` 객체(Accessible · Action · Value · Component 인터페이스)를 접근성 레지스트리에 등록하는 구현과 D-Bus 의존성(libdbus 또는 sd-bus)이다. 외부 계약(AT-SPI)과 새 의존성이 필요해 한 번에 검증할 환경 없이 쓰면 CI 의 리눅스 빌드를 깨는 위험만 커서 지금 만들지 않았다.
함정: 스냅샷을 UIA 와 같은 방식으로 쓴다(브리지는 위젯을 쥐지 않고 (화면 핸들, 위젯 번호)로 찾는다). 붙은 스크린 리더가 없으면 일하지 않는 규칙(`isClientActive`)도 같다.

### X11ConsoleXft
제목: 리눅스 개발 콘솔 오버레이를 Xft 로 그려 한글 · 일본어 글리프를 온전히
옛번호: D3-3
왜 필요한가: 오버레이(`X11DevConsoleWindow`)는 코어 X 글꼴(iso10646-1)로 UTF-8 줄을 그려 깨지지는 않지만, 한글 · 일본어 글리프는 X 서버의 코어 글꼴이 그 글자를 가진 만큼만 나온다(보통 비어 보인다). 폴백 글꼴 목록(fontconfig)을 쓰는 Xft 가 필요하다.
착수 조건: 리눅스 클라이언트에서 개발 콘솔로 한글 치트·명령 이름을 쓰는 일이 생길 때(`libxft` 의존성이 CI 리눅스 이미지에 들어갈 때).
남은 것: `XftDrawStringUtf8` 로 바꾸고, 콘솔 창 단위의 Xft 글꼴·그리기 객체를 두며, CMake 에 libxft 를 찾아 이어야 한다. 새 시스템 의존성이라 CI 리눅스 이미지 패키지 목록(`ci.yml`)도 고친다.
함정: 로캘(`setlocale`)은 쓰지 않는다 — 아무도 부르지 않는 전제다. 글꼴이 없으면 콘솔을 세우지 않는 지금 규칙(폴백 금지)을 지킨다.

## 조건부

### MarkerComputeProjection
제목: 화면 마커의 투영 · 컬링을 컴퓨트로
옛번호: G-13
왜 필요한가: 이름표 2,000 개의 남은 게임 스레드 몫 `GT.UI.Markers` p50 0.49 ~ 0.52 ms(마커마다 월드 위치 읽기 · 투영 · 앵커 쓰기. 위치 · 배율 적용은 이미 GPU 앵커다 — [결정 기록](../09_Decisions.md) 5-7).
착수 조건: 게임 HUD 에서 마커가 2,000 개를 넘거나 이 구간이 0.5 ms 를 꾸준히 넘을 때.
남은 것: 컴퓨트가 GPUScene 변환을 읽어 앵커 표를 쓰는 모양이다(마커 ↔ GPU 인스턴스 대응이 필요하다). 데미지 숫자처럼 시간 함수로만 움직이는 UI 의 GPU 애니메이션도 같은 길이다.
함정: 측정은 Release `App -gv_benchMeshes=2000 -gv_benchUiNameplates=2000 -gv_profileFrames=600` 의 `GT.UI.Markers` · `GT.UI.Paint`.
2026-10-24 확인: 마커를 쓰는 곳은 벤치 장면(`Source/Games/Empty/BenchScene*`)뿐이라 게임 HUD 에는 조건이 닿지 않았다. 그대로 둔다(2026-10-27 재확인: `Source/Games` 에서 마커는 여전히 `Empty` 벤치뿐이다). 컴퓨트 패스 · 셰이더 · 마커 ↔ GPU 인스턴스 대응이 렌더러 영역이다.
