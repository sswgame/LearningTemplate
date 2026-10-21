# 에디터 기능 후보의 남은 것

> 상태: 구현중 · 선행: 없음 · 영역: editor · 옛번호: D3-1, D3-3, D3-4 · 진행: 4/10

에디터 후보 문서의 기능 절 넷(지형 칠하기 · UI 미리보기 디자이너 · 콘솔 치트 · 타입 핸들 인스펙터)을 착수하며 옮겼다(2026-10-21). 단위가 끝나면 그 절을 지운다.
끝난 단위는 빌드 없이 구현했다 — 확인 목록은 2절이다.

## 1. 단위

| 단위 | 내용 | 선행 |
|---|---|---|
| TerrainBrush.T2 | 높이장 크기 바꾸기 | 없음 |
| TerrainBrush.T3 | 구멍 칠하기 | 없음 |
| UIPreview.U2 | 앵커 프리셋 직후 부모 여백에서 위젯이 밀리는지 확인 | 없음 |
| Cheat.C2 | 리눅스 오버레이 실기 확인과 한글 | 없음 |
| TypedHandle.H2 | `FUNCTION` 인자의 타입 핸들 선택기 | 없음 |
| TypedHandle.H3 | Reset to Default 의 핸들 글 적용 확인 · 화면 확인 | 없음 |

### TerrainBrush.T2 높이장 크기 바꾸기
- `TerrainBrushTool`(씬 뷰 Terrain 토글)에 해상도를 바꾸는 단추가 없다. `TerrainHeightfield::initialize` 를 다시 부르고 청크(`TerrainComponent::createChunks`) · 충돌 · 스플랫을 새 크기로 다시 세워야 해서 엔진 쪽 API(재표본화 포함)가 먼저다.
- 되돌리기는 사각형 기록(`EditRecord`)으로 못 하므로 크기 바꾸기 전후 전체를 담는 기록이 따로 든다. 쓰기 쪽 남은 것은 옛 `D2-5` (7).

### TerrainBrush.T3 구멍 칠하기
- 엔진 지형에 구멍 마스크가 없다(렌더 · 충돌 · 내비 모두). 마스크 형식(스플랫처럼 샘플마다 1 비트, 저장 파일)과 렌더 · 충돌의 건너뛰기를 엔진에 먼저 둔다. 브러시는 `TerrainBrushMode` 에 한 줄과 이름 표 두 곳(`static_assert` 가 맞춘다)이면 된다.
- 브러시 원이 보이지 않는 문제는 [EditorCaptureFollowUps](Candidates-editor.md#editorcapturefollowups) 의 지형브러시원.

### UIPreview.U2 앵커 프리셋과 부모 여백
- `UIPreviewLogic::applyAnchorPreset` 은 사각형을 앵커 × 부모 크기 + 오프셋으로 되돌려 구한다. 부모에 여백(padding)이 있을 때 고른 직후 위젯이 약간 밀리는지 화면으로 본다(`gpu-verifier`).

### Cheat.C2 리눅스 오버레이
- 리눅스 오버레이(`X11DevConsoleWindow`)를 실기로 띄워 보지 않았다. X11 그리기는 `XDrawString`(Latin-1)이라 한글이 깨진다 — Xft 나 UTF-8 글꼴 집합으로 바꾼다.
- `Vitality` 를 직접 쓰는 다른 키트의 치트는 아직 없다. 생기면 `Vitality::setDamageImmune` 을 그대로 쓴다.
- 함정: 무적(`cheat.god`)은 어빌리티 시스템의 태그(`State.Invulnerable`, 치트는 하위 `.Cheat`)로 플레이어 조종 폰에만 건다. `cheat.heal` · `cheat.attribute` 는 적을 포함한 모든 어빌리티 시스템에 적용된다(플레이어만 가리는 길이 필요하면 태그나 팀을 본다).

### TypedHandle.H2 `FUNCTION` 인자의 타입 핸들
- CallInEditor 의 타입 핸들 인자는 아직 정수 입력이다(`InspectorPropertyManager.cpp` 의 `drawMethodArgFor`). 파서의 인자(`ParsedParameterInfo`)에 대상 타입이 없고, 인자 값(`TaskValue`)이 `TComponentHandle<T>` 를 담는지 `ComponentHandle` 로 정규화하는지도 정해야 한다.
- 순서: 파서가 인자도 `resolveComponentHandleProperty` 와 같이 정규화하고 대상 이름을 `ParameterInfo` 에 싣는다 → 생성 코드의 호출 썽크가 `ComponentHandle` 을 대상 타입 핸들로 바꾼다 → 인자 위젯이 `ComponentHandleProperty` 의 선택기를 쓴다.

### TypedHandle.H3 Reset to Default · 화면
- Reset to Default 가 기본 인스턴스의 핸들 글을 어떻게 적용하는지 확인하지 못했다.
- 화면 확인: 콤보 · `(none)` · 되돌리기 · Paste 비활성(`gpu-verifier`), 맵 값이 이제 원소 프로퍼티(`InspectorPropertyLayout::makeElementProperty`)로 그려지는지.

## 2. 끝난 단위의 확인 목록

넷 다 빌드 전이다(빌드 미확인).

- **TerrainBrush.T1 스무드 브러시** — `TerrainBrushMode::Smooth`(이웃 3 × 3 평균, 프레임 시작 높이로 목표를 먼저 구함), 설정 창 · `terrain.brush smooth`. `EditorTest --test_filter=TerrainBrushTest.*`.
- **UIPreview.U1 손잡이의 가정** — 상자 순서는 첫 · 끝 자식 가운데로 방향을 읽어 RTL 거울에서도 맞는다(`findBoxInsertIndex`). 격자 칸은 자식 사각형으로 추정하지 않고 배치가 정한 줄 크기(`GridPanel::getArrangedColumnSizes` · `getArrangedRowSizes`)를 격자 로컬 좌표로 비교한다(`UIPreviewLogic::findGridCell`) — 걸친 칸이 줄을 흐리지 않고 자식 없는 줄에도 놓을 수 있다. `EditorTest --test_filter=UIPreviewLogicTest.*`.
- **Cheat.C1 Shooter3D 무적** — `cheat.shooter.god`(뒤집기). `Vitality::setDamageImmune` 은 시간 없는 피해 면역이라 리스폰 · 상태 복원 뒤에도 남고 `kill` 은 막지 않는다. `EngineTest --test_filter=CombatStateTest.*:BaseValueStateArchiveTest.Vitality*`.
- **TypedHandle.H1 집합 막기 · 맵 값 손 복사** — 파서가 `set` · `unordered_set` 의 타입 핸들을 생성 오류로 막는다(C++ 컴파일 오류 대신). 맵 값 프로퍼티도 `makeElementProperty` 로 만들어 손 복사(`makeValueProperty`)를 지웠다. `ReflectionTest --test_filter=*Handle*`, `EditorTest --test_filter=InspectorPropertyLayoutTest.*`.
