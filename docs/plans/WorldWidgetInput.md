# 월드 공간 위젯의 입력 · 가려짐

> 상태: 구현중 · 선행: 없음 · 영역: ui · 옛번호: G-3 · 진행: 2/4

World 위젯(`WidgetComponent` 의 `Space = World`)은 그리기만 했다. 마우스로 월드의 단추를 누르거나, 화면 마커가 벽에 가려지면 숨는 길이 없었다. 언리얼의 `WidgetInteractionComponent` 에 해당하는 광선 → 위젯 좌표 길과 물리 씬 가림 판정을 넣었다.

## 1. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| W1 | 광선 입력 — `WorldWidgetPick`(화면 점 → 광선 → 사각형의 위젯 좌표, 앞면 · 뒷면 · 사각형 밖 · 평행), `WidgetComponent::_bAcceptsInput`(기본 꺼짐) · `pickWorld` · `dispatchWorldPointer`(위젯마다 `UIPointerState` 하나라 Enter · Leave · 끌기가 맞는다), `UIWorldPointerRouter`(가장 가까운 위젯 하나가 받고, 끌기 중인 위젯은 사각형 밖 평면 점도 받는다), `UISystem::routeWorldPointer`(크로스헤어 · 패드 같은 다른 출처가 자기 광선을 넣는 길), 마우스는 `UISystem::processWorldPointer`(화면 위젯 아래가 아닌 이벤트만, 처리한 누름 · 뗌은 게임이 받지 않게 소비). 테스트 `WorldWidgetPickTest` · `WidgetComponentTest.WorldInputClicksWidgetUnderRay` | 끝남(빌드 · 테스트 확인) |
| W2 | 물리 가림 — `WidgetComponent::_bOccludedByPhysics`: World 면 사각형보다 가까운 물리 바디가 광선을 막으면 입력을 받지 않고, Screen 이면 카메라와 마커 점 사이에 바디가 있을 때 마커를 숨긴다(`isRayBlockedByPhysics`, 오브젝트 자신은 보지 않는다) | 끝남(물리 씬이 막는 경우의 테스트는 없다 — W4) |
| W3 | 보일 때만 칠하기(절두체 판정) — 월드 위젯이 수십 개를 넘을 때. 지금은 보이지 않을 때도 칠하기가 돈다(내용이 바뀔 때만 다시 그리는 것은 그대로) | 남음(조건부 — 위젯이 수십 개를 넘을 때) |
| W4 | 실기 확인과 가림 테스트 — 마우스로 월드 단추를 누르는 화면 확인(Shooter3D 월드 패널, 에디터 게임 뷰의 `pointerOrigin` 포함), 물리 콜라이더가 있는 씬에서 막힌 광선 · 가려진 마커 테스트 | 남음 |

## 2. 정한 것

- **사각형은 만든 그대로 계산한다.** 월드 사각형은 오브젝트의 주 씬 컴포넌트 월드 행렬에 `_worldSize` 스케일을 곱한 단위 스프라이트 사각형이다(`createWorldQuad`). 위젯 좌표는 그 사각형에 붙인 렌더 텍스처의 픽셀(= UI 단위)이고, 앞면은 u 가 +X 로, 뒷면은 −X 로 는다(메시 UV 와 같은 규약).
- **마우스 입력은 한 곳에서 만든다.** `UIInputRouter::processPointer` 가 보낸 이벤트를 `getFramePointerEvents` 로 남기고, 월드 입력은 그 가운데 화면 위젯 아래가 아닌 것만 카메라 광선으로 바꾼다. 같은 이벤트를 두 번 만들지 않는다.
- **마우스를 잠근 1인칭은 광선이 없다.** 마우스 잠금 중에는 월드 입력도 풀린다(호버 해제). 크로스헤어로 누르려면 게임 코드가 `routeWorldPointer` 에 자기 광선과 `Down` · `Up` 이벤트를 넣는다.
- 카메라가 가만히 있고 포인터도 안 움직이면 호버는 갱신되지 않는다(이동 이벤트가 있을 때만 광선을 쏜다).
