# 에디터 기능 후보의 남은 것

> 상태: 구현중 · 선행: 없음 · 영역: editor · 옛번호: D3-1, D3-3, D3-4 · 진행: 7/10

에디터 후보 문서의 기능 절 넷(지형 칠하기 · UI 미리보기 디자이너 · 콘솔 치트 · 타입 핸들 인스펙터)을 착수하며 옮겼다(2026-10-21). 단위가 끝나면 그 절을 지운다.
끝난 단위는 빌드 없이 구현했다 — 확인 목록은 2절이다.

## 1. 단위

| 단위 | 내용 | 선행 |
|---|---|---|
| UIPreview.U2 | 앵커 프리셋 직후 부모 여백에서 위젯이 밀리는지 확인 | 없음 |
| Cheat.C2 | 리눅스 오버레이 실기 확인과 한글 | 없음 |
| TypedHandle.H3 | 핸들 선택기 화면 확인(프로퍼티 · CallInEditor 인자) | 없음 |

### UIPreview.U2 앵커 프리셋과 부모 여백
- `UIPreviewLogic::applyAnchorPreset` 은 사각형을 앵커 × 부모 크기 + 오프셋으로 되돌려 구한다. 부모에 여백(padding)이 있을 때 고른 직후 위젯이 약간 밀리는지 화면으로 본다(`gpu-verifier`).

### Cheat.C2 리눅스 오버레이
- 리눅스 오버레이(`X11DevConsoleWindow`)를 실기로 띄워 보지 않았다. X11 그리기는 `XDrawString`(Latin-1)이라 한글이 깨진다 — Xft 나 UTF-8 글꼴 집합으로 바꾼다.
- `Vitality` 를 직접 쓰는 다른 키트의 치트는 아직 없다. 생기면 `Vitality::setDamageImmune` 을 그대로 쓴다.
- 함정: 무적(`cheat.god`)은 어빌리티 시스템의 태그(`State.Invulnerable`, 치트는 하위 `.Cheat`)로 플레이어 조종 폰에만 건다. `cheat.heal` · `cheat.attribute` 는 적을 포함한 모든 어빌리티 시스템에 적용된다(플레이어만 가리는 길이 필요하면 태그나 팀을 본다).

### TypedHandle.H3 화면 확인
- Reset to Default 는 코드로 확인했다: 기본 인스턴스(CDO)는 씬에 없어 핸들이 비어 있고, `resetPropertyToDefault` 가 그 글(빈 핸들)을 `applyPropertyTextAsEdit` 로 써서 "(none)" 으로 돌아간다 — 타입 확인을 건너는 값이 들어갈 자리가 없다.
- 화면 확인이 남았다: 콤보 · `(none)` · 되돌리기 · Paste 비활성, 맵 값이 원소 프로퍼티(`InspectorPropertyLayout::makeElementProperty`)로 그려지는지, CallInEditor 인자 선택기(`InspectorPropertyManager::drawMethodArg`)가 프로퍼티와 같은 콤보인지(`gpu-verifier`).

## 2. 끝난 단위의 확인 목록

모두 빌드 전이다(빌드 미확인).

- **TerrainBrush.T1 스무드 브러시** — `TerrainBrushMode::Smooth`(이웃 3 × 3 평균, 프레임 시작 높이로 목표를 먼저 구함), 설정 창 · `terrain.brush smooth`. `EditorTest --test_filter=TerrainBrushTest.*`.
- **TerrainBrush.T2 높이장 크기 바꾸기** — 엔진: `TerrainHeightfield::resampleHeights`(렌더 메시와 같은 셀 삼각형 보간) · `resampleHoles`(새 셀 중심의 원본 셀) · `replaceSamples`, `TerrainComponent::replaceHeightfield`(청크 배치 · 세대 · 내비를 새로 세움, 청크 칸으로 나뉘지 않으면 거절) · `resampleHeightfield` · `isResolutionUsable`. 상태 스냅샷은 버전 2 — 크기를 바꾼 편집(`_bResolutionReplaced`)만 다른 해상도로 되살리고, 디스크 에셋을 다른 해상도로 다시 가져온 경우는 예전처럼 거절한다. 도구: 설정 창의 Resolution 콤보(나뉘는 해상도만) · Resize, `terrain.resize <해상도>`, 되돌리기는 전후 전체(`ResizeRecord`). `EngineTest --test_filter=TerrainTest.ResampleKeepsPlaneAndHoleCells:TerrainTest.WriteHolesAndResizeSurviveStateSnapshot:TerrainTest.UnsavedEditsSurviveStateSnapshot`.
- **TerrainBrush.T3 구멍 칠하기** — 구멍 마스크는 이미 렌더(`TerrainMeshBuilder`) · 충돌(`TerrainColliderComponent`) · 내비(`TerrainNavGeometry`) · 파일(`HeightfieldData`)에 있었고 쓰기 API 만 없었다. `TerrainHeightfield::readHoleCells` · `writeHoleCells`, `TerrainComponent::writeHoles`(셀이 든 청크의 메시 · 세대 · 내비). 높이 · 구멍 쓰기의 뒤처리는 `markChunksEdited` 한 길이다. 브러시: `CutHole` · `FillHole`(셀 중심 격자 `TerrainBrush::makeHoleGrid`, 감쇠 가중치 반 이상인 셀), 모드가 고치는 배열은 `TerrainBrushTarget` 하나로 갈린다(드래그 사본 · 되돌리기 · 쓰기). `EditorTest --test_filter=TerrainBrushTest.*`.
- **UIPreview.U1 손잡이의 가정** — 상자 순서는 첫 · 끝 자식 가운데로 방향을 읽어 RTL 거울에서도 맞는다(`findBoxInsertIndex`). 격자 칸은 자식 사각형으로 추정하지 않고 배치가 정한 줄 크기(`GridPanel::getArrangedColumnSizes` · `getArrangedRowSizes`)를 격자 로컬 좌표로 비교한다(`UIPreviewLogic::findGridCell`) — 걸친 칸이 줄을 흐리지 않고 자식 없는 줄에도 놓을 수 있다. `EditorTest --test_filter=UIPreviewLogicTest.*`.
- **Cheat.C1 Shooter3D 무적** — `cheat.shooter.god`(뒤집기). `Vitality::setDamageImmune` 은 시간 없는 피해 면역이라 리스폰 · 상태 복원 뒤에도 남고 `kill` 은 막지 않는다. `EngineTest --test_filter=CombatStateTest.*:BaseValueStateArchiveTest.Vitality*`.
- **TypedHandle.H1 집합 막기 · 맵 값 손 복사** — 파서가 `set` · `unordered_set` 의 타입 핸들을 생성 오류로 막는다(C++ 컴파일 오류 대신). 맵 값 프로퍼티도 `makeElementProperty` 로 만들어 손 복사(`makeValueProperty`)를 지웠다. `ReflectionTest --test_filter=*Handle*`, `EditorTest --test_filter=InspectorPropertyLayoutTest.*`.
- **TypedHandle.H2 `FUNCTION` 인자의 타입 핸들** — 파서(`resolveComponentHandleParameters`)가 인자 `TComponentHandle<T>` 를 내장 `ComponentHandle` 로 정규화하고 대상 `T` 를 `ParsedParameterInfo::_handleTargetTypeName` 에 싣는다(컨테이너 안 타입 핸들 · 생성자 · 정적 함수 · 컴포넌트가 아닌 타입은 생성 오류 — 확인에 그 씬의 매니저가 든다). 생성 코드: `FunctionParameterInfo::_handleTargetTypeName`, 호출 썽크가 `ComponentHandleArgument::cast`(그 컴포넌트의 씬에서 `makeTypedHandle`)로 확인해 지역 타입 핸들을 넘기고, 틀리면 오류를 남기고 부르지 않는다. 그런 인자가 있는 `.gen.cpp` 만 확인 헤더를 넣는다. 인스펙터: CallInEditor 인자가 프로퍼티와 같은 선택기(`ComponentHandleProperty::drawPicker`)다. `ReflectionTest --test_filter=TypedComponentHandleTest.*`.
