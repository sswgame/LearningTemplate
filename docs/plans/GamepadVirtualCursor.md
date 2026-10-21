# 패드 가상 커서의 남은 것

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: D4-3 · 진행: 2/3

명령형 게임에서 패드로 마우스 커서를 대신하는 가상 커서가 들어갔다(입력 맵 `virtualCursor` · 충돌 검사 `reportVirtualCursorConflict`). Nile · StarSkirmish 는 커서가 왼쪽 스틱, 오른쪽 스틱이 카메라 이동, 확대는 D 패드 위 · 아래다.
스틱 한 성분을 1D 액션이 읽는 바인딩(`<stick stick="Right" axis="Y" scaleY="<초당 칸>" scaleByDelta="1">`, `GamepadStickAxis`)이 들어가 스틱 확대를 데이터로 묶을 수 있다.
카메라 디렉터는 `Camera.Zoom` 이 발화한 프레임마다 그 값을 확대 칸으로 쓰므로(스틱은 `Down` 이라 기울인 동안 매 프레임) 코드 변경 없이 초당 칸으로 확대한다.
단위가 끝나면 그 절을 지운다.

## 1. 단위

| 단위 | 내용 | 선행 |
|---|---|---|
| G3 | 실제 패드로 확인하기 | 없음 |

### G3 실제 패드로 확인하기

- 시나리오 동안 가상 커서는 쉬어(`_pVirtualInput` 이 있으면 내지 않는다) 패드 길을 시나리오가 보지 않는다. 실기로 커서 이동 · A/B 클릭 · 확대 속도(프레임률과 무관한지)를 본다.
- 실기에서 D 패드 단계 확대가 거칠다고 보이면 아래 (가)의 가장자리 밀기 바인딩부터 후보로 올린다.

## 2. 끝난 단위의 확인 목록

G1(스틱 한 성분 바인딩)은 빌드 전이다. `EngineTest --test_filter=InputMapTest.GamepadStickAxisFeedsOneDimensionalAction:InputMapTest.GamepadStickScalesByAxisAndFrameTime:InputMapTest.*Resource*` 를 돌린다.

G2(Nile · StarSkirmish 의 패드 확대 바인딩)는 (다) 지금처럼 D 패드로 정했다(2026-10-22). 입력 맵 머리 주석의 결정 D4-3.3 과 같아 데이터는 그대로다.

- (가) 오른쪽 스틱 X 회전 · Y 확대는 패드의 카메라 이동을 없애므로 가장자리 밀기 바인딩(포인터가 화면 가장자리에 있으면 그쪽으로 미는 2D 축, 새 바인딩 종류)이 먼저다. 그 바인딩은 마우스 사용자에게도 가장자리 스크롤을 켜 시나리오의 가장자리 클릭과 부딪힐 수 있다.
- (나) 어깨 단추를 누르는 동안의 확대 레이어는 "액션이 눌린 동안 켜지는 레이어" 기능이 먼저인데, 그 레이어가 아래 레이어를 막으면 수정자 액션 자신도 막혀 켜고 꺼짐이 되풀이된다(수정자는 레이어 밖에서 읽어야 한다).
- 둘 다 실기 없이 감으로 정할 입력 기능이 아니다. 고르게 되면 스틱 한 성분 바인딩(G1)을 `Camera.Zoom` 에 `<stick stick="Right" axis="Y" scaleY="3" scaleByDelta="1"/>` 로 쓰고 두 입력 맵의 머리 주석을 고친다.
