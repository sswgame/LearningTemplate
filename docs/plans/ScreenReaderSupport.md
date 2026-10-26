# 스크린 리더

> 상태: 측정대기 · 선행: 없음 · 영역: ui · 옛번호: G-9 · 진행: 6/6

**남은 확인(통합에서 확인):** 매니페스트 id `uia-client-test`(`Test/QA/VerifyBatch.json`). 확인이 끝나면 통합 담당이 이 문서를 닫는다.

런타임 UI 의 접근성은 글자 크기 · 자막 · 색각 · 고대비까지였다. 위젯의 이름 · 역할 · 값 · 상태를 운영체제 스크린 리더(Windows 내레이터)가 읽게 잇는다(언리얼 Slate 접근성 · Xbox 접근성 지침 107).

## 1. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| A1 | 위젯의 접근성 값 — `AccessibleRole` · `AccessibleInfo`(이름 · 값 · 범위 · 상태 비트), `Widget::_accessibleName`(PROPERTY, 현지화 가능) · `describeAccessible`, 글 · 단추 · 체크 상자 · 슬라이더 · 진행 막대 · 콤보 · 로테이터 · 입력 칸 · 그림 · 목록이 자기 역할을 낸다 | 끝남(빌드 · 테스트 확인) |
| A2 | 접근성 트리 — `UIAccessibilityTree`(보이는 위젯만, 트리 순서, 역할 없는 패널은 건너뜀, 단추 안의 글은 단추의 이름), `UISystem::populateAccessibilitySnapshot`(입력을 받는 화면은 위에서 아래로 모달에서 멈추고 그다음 오버레이) | 끝남(빌드 · 테스트 확인) |
| A3 | 브리지 계약 — `IUIAccessibilityBridge`(`isClientActive` · `onUIUpdated` · `notifyFocusChanged` · `announce`), `UISystem::setAccessibilityBridge` · `announce`, 포커스가 옮겨 가면 알림(`updateAccessibilityFocus`), 알림 서비스(`UINotificationService`)가 보이는 알림의 글을 낭독, 스크린 리더가 시키는 일 `UIAccessibilityActions`(누르기 · 포커스 · 슬라이더 값) | 끝남(빌드 · 테스트 확인) |
| A4 | Windows UI Automation 브리지 — `UiaAccessibilityBridge`(창의 `WM_GETOBJECT` 훅 `IWindow::bindAccessibilityQueryHandler`, 루트 · 요소 제공자, Invoke · Toggle · Value · RangeValue 패턴, 포커스 이벤트 · `UiaRaiseNotificationEvent` 낭독), `App::installRuntimeUIHooks` 가 런타임(비에디터)에서 끼운다 | 끝남(내레이터 실기 미확인 — A6) |
| A5 | 리눅스 AT-SPI 브리지는 단위에서 뺐다 — [Candidates-ui](Candidates-ui.md) `AtSpiBridge`(D-Bus 의존성 · Orca 가 있는 세션이 있어야 하는 별도 일) | 후보로 옮김 |
| A6 | 내레이터 실기 확인을 자동화로 — 진짜 창에 브리지를 달고 같은 프로세스의 UIA 클라이언트(내레이터가 쓰는 같은 `IUIAutomation` 길)가 다른 스레드에서 메뉴의 이름 · 컨트롤 타입을 읽고 Invoke · RangeValue · Toggle 을 부르고 포커스 이동 알림을 받는지 `UiaClientTest.ClientReadsAndDrivesTheMenuThroughTheRealWindow`. 에디터 게임 뷰는 내보내지 않는다. 낭독 음성 자체(내레이터가 어떻게 읽는지)는 귀로만 들린다 — 읽을 글(이름 · 값 · 알림 문구)이 UIA 로 맞게 오는 데까지를 본다 | 끝남(빌드 미확인) |
| A7 | 옵션 메뉴 이름 채우기 — 슬라이더 · 콤보 · 로테이터 옆의 설명 글을 그 위젯의 이름으로 잇는다(`Widget::setAccessibleLabelledBy`, `OptionsMenuScreen`). 설명 글은 이미 현지화돼 있어 이름 문자열을 따로 두지 않는다 | 끝남(테스트 확인) |

## 2. 정한 것

- **접근성 트리는 스냅샷이고 브리지는 위젯을 쥐지 않는다.** UIA 제공자는 (화면 핸들, 위젯 번호)로 스냅샷에서 노드를 찾아 값을 읽는다. 위젯이 사라지면 "요소 없음" 으로 답한다. 스냅샷은 UI 갱신마다 낡은 것으로 표시되고 처음 물을 때 새로 뽑는다.
- **붙은 스크린 리더가 없으면 일하지 않는다.** `UiaClientsAreListening` 이 false 면 트리 뽑기 · 포커스 알림 · 낭독이 모두 건너뛰어진다(평소 비용 없음). 붙은 뒤 첫 프레임에 지금 포커스를 알린다.
- **UIA 호출은 메인 스레드로 옮겨 처리한다**(`ProviderOptions_UseComThreading`) — UI 상태를 읽고 쓰는 것은 게임(메인) 스레드뿐이다.
- 이름은 위젯이 알려 주는 글이다: 글 위젯은 보이는 글, 단추는 안의 글, 슬라이더 · 콤보 · 로테이터 · 입력 칸은 `accessibleName`(안의 글은 값이라 이름이 아니다). 꾸밈 그림(이름 없는 `ImageWidget`)과 역할 없는 패널은 읽지 않는다.
- 에디터는 창을 ImGui 가 쓰므로 게임 UI 브리지를 끼우지 않는다(`-EnableEditor` 일 때).
