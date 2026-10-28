# UI 후보

> 상태: 후보 · 선행: 없음 · 영역: ui · 진행: 해당없음

런타임 UI, 글자 렌더링, 접근성, DPI 에서 아직 안 한 일의 목록이다. 착수하면 절을 계획 문서로 옮기고 여기서 지운다.

## 한눈에 보기

| 절 | 한 줄 설명 | 착수 조건 |
|---|---|---|
| AtSpiBridge | 리눅스 스크린 리더(AT-SPI) 브리지(코드 끝, Linux 컴파일 확인 전) | 리눅스 환경 |
| MarkerComputeProjection | 화면 마커 투영 컴퓨트의 쿠킹 뒤 확인 · 남은 하위 항목 | 사용자가 엔진 셰이더를 쿠킹한 뒤 |

## 기능

### AtSpiBridge
제목: 리눅스 AT-SPI 스크린 리더 브리지
왜 필요한가: Windows 는 UI Automation 브리지가 있지만 리눅스 클라이언트는 Orca 같은 스크린 리더에 닿는 길이 없다. 접근성 트리와 브리지 계약은 플랫폼과 무관해 AT-SPI 구현(`AtSpiAccessibilityBridge.cpp`, `-DSW_ENABLE_ATSPI=ON`, 기본 끔)만 있고 리눅스에서 지어 본 적이 없다.
착수 조건: 없음.
화면 좌표 보정은 했다(X11 은 `XTranslateCoordinates`, 요청의 coord type 이 screen 일 때만 더한다. Wayland 는 클라이언트에 전역 좌표가 없어 0).
상태: 코드 끝, Linux 컴파일 확인 전(CMake `SW_ENABLE_ATSPI` · pkg-config `dbus-1` 처리는 Wayland 와 같은 모양으로 `Linux.cmake` 에 있다).
남은 첫 일: 리눅스 CI 에서 `-DSW_ENABLE_ATSPI=ON` 으로 지어 컴파일 · Orca 확인(이 기계는 libdbus 가 없다).
함정: 브리지는 위젯을 쥐지 않고 스냅샷에서 (화면 핸들, 위젯 번호)로 찾는다(UIA 와 같다). 스크린 리더가 안 붙었으면 일하지 않는다(`isClientActive`).

## 조건부

### MarkerComputeProjection
제목: 화면 마커 투영 컴퓨트의 남은 일
왜 필요한가: 이름표 2,000 개에서 게임 스레드 `GT.UI.Markers` 가 p50 0.5 ms 쯤 남는 것을 줄이는 일이다. 구현은 끝났다 — 마커마다 월드 점 · 규칙(`CanvasMarkerRecord`)을 패킷에 싣고 `canvasmarker.hlsl`(`RenderPassType::CanvasMarker`)이 앵커 원소에 화면 좌표 · 배율(가시 판정은 배율 0)을 쓴다. 마커 ↔ GPU 대응은 앵커 번호다. `-gv_uiMarkerGpuProjection=true` 로 켜고 기본은 끈다.
착수 조건: 사용자가 엔진 셰이더를 다시 쿠킹한 뒤(`canvasmarker.hlsl` 은 소스만 있다).
남은 첫 일: 쿠킹 뒤 마커 2,000 개 벤치(`Source/Games/Empty`)에서 켠 채 화면 위치가 CPU 투영과 같은지(통합 시나리오)와 `GT.UI.Markers` 감소를 잰다.
남은 하위 항목과 이유: ① 위치를 GPUScene 인스턴스 변환에서 읽기 — 메시 없는 마커도 있고 인스턴스 칸은 프레임마다 다시 짜이며 그 파일이 렌더러 작업과 겹쳐 월드 점을 기록으로 싣는 길을 먼저 했다. ② 물리 가림 · 놓인 자리를 읽는 마커(`needsMarkerPlacement`)와 크기가 정해지지 않은 마커는 CPU 투영으로 남는다(결과를 게임 스레드로 돌려받지 않는다). ③ 데미지 숫자처럼 시간 함수로만 움직이는 UI 의 GPU 애니메이션은 같은 길(기록에 시간 함수 칸)이다.
