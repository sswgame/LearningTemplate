# 패드 가상 커서의 남은 것

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: D4-3 · 진행: 1/3

명령형 게임에서 패드로 마우스 커서를 대신하는 가상 커서가 들어갔다(입력 맵 `virtualCursor` · 충돌 검사 `reportVirtualCursorConflict`). Nile · StarSkirmish 는 커서가 왼쪽 스틱, 오른쪽 스틱이 카메라 이동, 확대는 D 패드 위 · 아래다.
스틱 한 성분을 1D 액션이 읽는 바인딩(`<stick stick="Right" axis="Y" scaleY="<초당 칸>" scaleByDelta="1">`, `GamepadStickAxis`)이 들어가 스틱 확대를 데이터로 묶을 수 있다.
카메라 디렉터는 `Camera.Zoom` 이 발화한 프레임마다 그 값을 확대 칸으로 쓰므로(스틱은 `Down` 이라 기울인 동안 매 프레임) 코드 변경 없이 초당 칸으로 확대한다.
단위가 끝나면 그 절을 지운다.

## 1. 단위

| 단위 | 내용 | 선행 |
|---|---|---|
| G2 | Nile · StarSkirmish 의 패드 확대 바인딩 정하기 | 없음 |
| G3 | 실제 패드로 확인하기 | G2 |

### G2 Nile · StarSkirmish 의 패드 확대 바인딩

- 두 게임은 오른쪽 스틱 두 축을 모두 카메라 이동(`Camera.Pan`)에 쓰므로 오른쪽 스틱 Y 를 확대에 그대로 묶으면 앞뒤 이동과 확대가 한 입력이 된다.
- 고를 길: (가) 오른쪽 스틱 X = 회전 · Y = 확대로 바꾸고 이동은 커서가 화면 가장자리에 닿을 때(콘솔 RTS 관례), (나) 어깨 단추를 누르는 동안만 오른쪽 스틱 Y 가 확대(레이어 전환, 스틱 바인딩에 수정자가 없어 입력 맵에 레이어 하나를 더한다), (다) 지금처럼 D 패드.
- 정하면 `nile.input.xml` · `skirmish.input.xml` 의 `Camera.Zoom` 에 `<stick ... axis="Y" scaleY="3" scaleByDelta="1"/>` 를 더하고 머리 주석의 조작 설명을 고친다. 가상 커서 충돌 검사(`reportVirtualCursorConflict`)는 커서 스틱만 거절하므로 오른쪽 스틱은 묶을 수 있다.

### G3 실제 패드로 확인하기

- 시나리오 동안 가상 커서는 쉬어(`_pVirtualInput` 이 있으면 내지 않는다) 패드 길을 시나리오가 보지 않는다. 실기로 커서 이동 · A/B 클릭 · 확대 속도(프레임률과 무관한지)를 본다.

## 2. 끝난 단위의 확인 목록

G1(스틱 한 성분 바인딩)은 빌드 전이다. `EngineTest --test_filter=InputMapTest.GamepadStickAxisFeedsOneDimensionalAction:InputMapTest.GamepadStickScalesByAxisAndFrameTime:InputMapTest.*Resource*` 를 돌린다.
