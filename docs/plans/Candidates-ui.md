# UI 후보

> 상태: 후보 · 선행: 없음 · 영역: ui · 옛번호: G-9, G-13 · 진행: 해당없음

이 문서는 착수 전 후보의 목록이다. 착수하면 절을 계획 문서로 옮기고 여기서 지운다. 런타임 UI, 글자 렌더링, 접근성, DPI 가 범위다.
글리프 래스터화 히치와 그리기 성능의 결정은 [결정 기록](../decisions/5-7_RuntimeUI.md) 5-7 에 있다.

## 기능

### AtSpiBridge
제목: 리눅스 AT-SPI 스크린 리더 브리지
옛번호: G-9
왜 필요한가: Windows 는 UI Automation 브리지가 있고([결정 기록 5-7](../decisions/5-7_RuntimeUI.md) A4), 리눅스 클라이언트에는 Orca 같은 스크린 리더에 닿는 길이 없다. 접근성 트리(`UIAccessibilityTree`) · 브리지 계약(`IUIAccessibilityBridge`)은 플랫폼과 무관해 AT-SPI 구현만 비어 있다.
2026-10-27 구현: `Engine/UI/Accessibility/Linux/AtSpiAccessibilityBridge.cpp` 가 libdbus-1 로 `Accessible` · `Component` · `Action` · `Value` 객체와 레지스트리 등록(`Socket.Embed`), 포커스 · 알림 신호를 낸다. 역할 · 상태 번호 규칙(`AtSpiMapping`)은 `AtSpiMappingTest` 가 본다. `-DSW_ENABLE_ATSPI=ON`(libdbus-1-dev) 일 때만 짓고 기본은 끈다 — 리눅스에서 지어 본 적이 없다.
남은 것: 리눅스 CI 에서 `SW_ENABLE_ATSPI=ON` 으로 짓기(컴파일 오류가 있으면 고친다), 사각형에 창 위치를 더해 화면 좌표로 맞추기(지금은 창 클라이언트 기준이라 확대기 · 강조 상자가 창 위치만큼 어긋난다).
함정: 스냅샷을 UIA 와 같은 방식으로 쓴다(브리지는 위젯을 쥐지 않고 (화면 핸들, 위젯 번호)로 찾는다). 붙은 스크린 리더가 없으면 일하지 않는 규칙(`isClientActive`)도 같다.

## 조건부

### MarkerComputeProjection
제목: 화면 마커의 투영 · 컬링을 컴퓨트로
옛번호: G-13
왜 필요한가: 이름표 2,000 개의 남은 게임 스레드 몫 `GT.UI.Markers` p50 0.49 ~ 0.52 ms(마커마다 월드 위치 읽기 · 투영 · 앵커 쓰기. 위치 · 배율 적용은 이미 GPU 앵커다 — [결정 기록](../09_Decisions.md) 5-7).
착수 조건: 게임 HUD 에서 마커가 2,000 개를 넘을 때(게임 내용의 사실이다).
남은 것: 컴퓨트가 GPUScene 변환을 읽어 앵커 표를 쓰는 모양이다(마커 ↔ GPU 인스턴스 대응이 필요하다). 데미지 숫자처럼 시간 함수로만 움직이는 UI 의 GPU 애니메이션도 같은 길이다.
2026-10-24 확인: 마커를 쓰는 곳은 벤치 장면(`Source/Games/Empty/BenchScene*`)뿐이라 게임 HUD 에는 조건이 닿지 않았다. 그대로 둔다(2026-10-27 재확인: `Source/Games` 에서 마커는 여전히 `Empty` 벤치뿐이다). 컴퓨트 패스 · 셰이더 · 마커 ↔ GPU 인스턴스 대응이 렌더러 영역이다.
