# 에디터 기능 후보의 남은 것

> 상태: 측정대기 · 선행: 없음 · 영역: editor · 옛번호: D3-1, D3-3, D3-4 · 진행: 10/10

**남은 확인(통합에서 확인):** 매니페스트 id `ui-preview-logic-test`, `inspector-handle-scenario`(`Test/QA/VerifyBatch.json`). 리눅스 쪽 두 테스트는 CI 의 xvfb swrender 묶음이 돈다(푸시 뒤 `ci-jobs --summary`). 확인이 끝나면 통합 담당이 이 문서를 닫는다.

에디터 후보 문서의 기능 절 넷(지형 칠하기 · UI 미리보기 디자이너 · 콘솔 치트 · 타입 핸들 인스펙터)을 착수하며 옮겼다(2026-10-21). 단위가 끝나면 그 절을 지운다.
끝난 단위는 빌드 없이 구현했다 — 확인 목록은 2절이다.

## 1. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| UIPreview.U2 | 앵커 프리셋 직후 부모 여백에서 위젯이 밀리는지 확인 — 계산이 정말 밀었다: 위젯 사각형은 슬롯 안쪽 여백(`_padding`)만큼 들어간 곳이라 프리셋이 위젯 사각형을 그대로 슬롯 사각형으로 삼으면 여백만큼 밀리고 줄었다. `applyAnchorPreset` 이 여백을 도로 더해 슬롯 사각형을 구한다. 테스트 `UIPreviewLogicTest.AnchorPresetKeepsTheWidgetInPlaceWithSlotPadding` | 끝남(빌드 미확인) |
| Cheat.C2 | 리눅스 오버레이: `XDrawString`(Latin-1)을 `XDrawString16` + iso10646-1 코어 글꼴로 바꿔 UTF-8 글이 깨지지 않는다(글꼴이 없으면 콘솔을 세우지 않는다 — Latin-1 로 되돌아가지 않는다). 모양은 X 서버의 글꼴이 그 글자를 가진 만큼만 나온다 — 한글 글리프 전체는 Xft 가 있어야 해서 [Candidates-ui](Candidates-ui.md) `X11ConsoleXft` 로 옮겼다. 실기 확인은 xvfb 위 `X11DevConsoleTest.OverlayDrawsLatinLineAndSurvivesUtf8Lines`(픽셀을 읽어 줄이 그려졌는지) | 끝남(빌드 미확인) |
| TypedHandle.H3 | 핸들 선택기 화면 확인을 시나리오로: 콤보 · `(none)` · 되돌리기 · Paste 비활성은 기존 `editor.inspectorhandle`, CallInEditor 인자 선택기(`InspectorPropertyManager::drawMethodArg`)가 프로퍼티와 같은 콤보인지는 같은 시나리오의 ⑥(메서드 `Assign Target` 의 인자 콤보로 고르고 Invoke → `_target` 에 같은 값). 맵 값이 원소 프로퍼티로 그려지는지는 `InspectorPropertyLayoutTest` 가 본다(H1) | 끝남(빌드 미확인) |

- Cheat.C2 의 `Vitality` 를 직접 쓰는 다른 키트의 치트는 아직 없다. 생기면 `Vitality::setDamageImmune` 을 그대로 쓴다.
- 함정: 무적(`cheat.god`)은 어빌리티 시스템의 태그(`State.Invulnerable`, 치트는 하위 `.Cheat`)로 플레이어 조종 폰에만 건다. `cheat.heal` · `cheat.attribute` 는 적을 포함한 모든 어빌리티 시스템에 적용된다(플레이어만 가리는 길이 필요하면 태그나 팀을 본다).
- Reset to Default 는 코드로 확인했다: 기본 인스턴스(CDO)는 씬에 없어 핸들이 비어 있고, `resetPropertyToDefault` 가 그 글(빈 핸들)을 `applyPropertyTextAsEdit` 로 써서 "(none)" 으로 돌아간다 — 타입 확인을 건너는 값이 들어갈 자리가 없다.

## 2. 끝난 단위의 확인 목록

모두 빌드 전이다(빌드 미확인).

- **UIPreview.U1 손잡이의 가정** — 상자 순서는 첫 · 끝 자식 가운데로 방향을 읽어 RTL 거울에서도 맞는다(`findBoxInsertIndex`). 격자 칸은 자식 사각형으로 추정하지 않고 배치가 정한 줄 크기(`GridPanel::getArrangedColumnSizes` · `getArrangedRowSizes`)를 격자 로컬 좌표로 비교한다(`UIPreviewLogic::findGridCell`) — 걸친 칸이 줄을 흐리지 않고 자식 없는 줄에도 놓을 수 있다. `EditorTest --test_filter=UIPreviewLogicTest.*`.
- **Cheat.C1 Shooter3D 무적** — `cheat.shooter.god`(뒤집기). `Vitality::setDamageImmune` 은 시간 없는 피해 면역이라 리스폰 · 상태 복원 뒤에도 남고 `kill` 은 막지 않는다. `EngineTest --test_filter=CombatStateTest.*:BaseValueStateArchiveTest.Vitality*`.
- **TypedHandle.H1 집합 막기 · 맵 값 손 복사** — 파서가 `set` · `unordered_set` 의 타입 핸들을 생성 오류로 막는다(C++ 컴파일 오류 대신). 맵 값 프로퍼티도 `makeElementProperty` 로 만들어 손 복사(`makeValueProperty`)를 지웠다. `ReflectionTest --test_filter=*Handle*`, `EditorTest --test_filter=InspectorPropertyLayoutTest.*`.
- **TypedHandle.H2 `FUNCTION` 인자의 타입 핸들** — 파서(`resolveComponentHandleParameters`)가 인자 `TComponentHandle<T>` 를 내장 `ComponentHandle` 로 정규화하고 대상 `T` 를 `ParsedParameterInfo::_handleTargetTypeName` 에 싣는다(컨테이너 안 타입 핸들 · 생성자 · 정적 함수 · 컴포넌트가 아닌 타입은 생성 오류 — 확인에 그 씬의 매니저가 든다). 생성 코드: `FunctionParameterInfo::_handleTargetTypeName`, 호출 썽크가 `ComponentHandleArgument::cast`(그 컴포넌트의 씬에서 `makeTypedHandle`)로 확인해 지역 타입 핸들을 넘기고, 틀리면 오류를 남기고 부르지 않는다. 그런 인자가 있는 `.gen.cpp` 만 확인 헤더를 넣는다. 인스펙터: CallInEditor 인자가 프로퍼티와 같은 선택기(`ComponentHandleProperty::drawPicker`)다. `ReflectionTest --test_filter=TypedComponentHandleTest.*`.
