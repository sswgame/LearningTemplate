# CharacterEditor — 캐릭터 편집 에디터 플러그인

`GF_EditorPlugIn_CharacterEditor` 는 캐릭터 편집 창(`docs/plans/EditorCharacterEditor.md`)의 **첫 단계**입니다. 화면 없이 테스트할 수 있는 편집 모델과, 그 모델을 그리는 도구 패널 뼈대까지 들어 있습니다.
미리보기 씬이 필요한 부분(캐릭터 스폰, 뷰포트 기즈모, 피팅 표시, 애니메이션 재생, 썸네일)은 `EditorMultiWorld` 단계가 끝난 뒤 이어 붙입니다 — 남은 일은 `docs/06_Backlog.md` 의 `D4-8` ⑥ 에 있습니다.

## 구조

| 자리 | 하는 일 |
|---|---|
| `Model/EditSnapshotHistory.h` | 스냅샷 되돌리기 기록. 같은 키의 연속 편집(끌기)을 한 단계로 합친다 |
| `Model/SocketEditModel.{h,cpp}` | 소켓 에셋(`*.sockets.xml`) 편집. 추가 · 삭제 · 이름 바꾸기(후보 목록이 따라감) · 부모 · 종류 · 변환 · 앵커 · 후보, 좌우 대칭, 검증, 읽기 · 쓰기 |
| `Model/CharacterMirrorUtil.{h,cpp}` | 좌우 이름 규칙 표(`MirrorNameRules`, 데이터 파일 `editor/character/default.mirrornames.xml`, 종류 등록 `Model/MirrorNameRulesDataKind.cpp`)와 위치 · 회전 뒤집기 |
| `Model/BonePoseEditModel.{h,cpp}` | 스켈레톤(읽기 전용)의 본 목록 · 트리와 레퍼런스 포즈 덮어쓰기(`*.refpose.xml`) 편집. 위치 · 회전 · 스케일, 지우기, 좌우 대칭, 읽기 · 쓰기, 되돌리기 |
| `Model/FitCheckSummary.{h,cpp}` | 푼 피팅 결과(`FitResult`)를 부품별 잘린 면 · 조임 강도, 관통 점 수 · 깊이로 요약(보기 전용) |
| `Model/SaveReloadGuard.h` | 우리가 쓴 파일의 핫 리로드를 외부 변경으로 착각하지 않게 거르는 판정기 |
| `Model/AppearanceEditModel.{h,cpp}` | 외형 선택(`AppearanceSelection`) 편집. 슬롯 아이템 · 세트 입히기 · 체형 · 얼굴 · 꾸미기 값 · 시드, 입혀 보기(규칙 설명), 프리셋 저장소 · 파일 저장 |
| `CharacterEditorPanel` / `CharacterEditorCommands.cpp` | 패널 `charactereditor.main`(소켓 탭, 스켈레톤 탭, 외형 탭)과 메뉴 커맨드 `charactereditor.openPanel` |

- **편집 대상은 원본 데이터 파일**이다. 조립 결과(병합 메시, 펼친 외형)는 저장하지 않는다.
- **되돌리기는 모델마다 따로** 돈다(창 안에서만). 전역 `CommandStack` 에 섞지 않는다. 문서 패널 계약(`EditorDocumentPanel`)으로 옮기는 일은 `EditorMultiWorld` 뒤의 일이다.
- **저장은 적은 필드만 쓴다.** `SocketSet::saveToXMLText` 가 `_fieldMask` 로 쓸 필드를 고르므로, 값을 고치는 편집은 그 필드 비트를 함께 켠다. 비트를 켜지 않으면 고친 값이 저장되지 않는다.
- **좌우 짝은 이름 규칙으로만** 찾는다(`_L`/`_R`, `Left`/`Right` 등). 토큰은 이름의 앞 · 뒤나 구분자(`_ . - 공백`), CamelCase 경계에 놓일 때만 인정한다 — `Shoulder_Pad` 의 `r_` 은 짝이 아니다.
- 모델은 ImGui 와 씬을 모른다. `Model/*.cpp` 는 EngineTest 가 직접 컴파일한다(`Test/EngineTest/Character/TestCharacterEditorModel.cpp`).

## 함정

- 파일을 쓰면 소켓 캐시의 핫 리로드가 같은 파일을 다시 읽는다. 패널은 읽은 결과를 모델에 다시 반영하지 않고, 저장 뒤 3초 안의 다시 읽기(`SaveReloadGuard`)만 외부 변경이 아닌 것으로 거른다.
  횟수는 모든 소켓 에셋이 나눠 쓰므로 다른 파일의 다시 읽기와는 구별하지 못한다.
- 부모 본 검증은 스켈레톤을 읽은 뒤에 켜진다(`Load skeleton` → `SocketEditModel::setBoneNames`).
- 스켈레톤은 재임포트가 다시 쓴다 — 포즈 편집은 `*.refpose.xml` 에만 간다. 좌우 대칭은 덮어쓰기가 있는 본만 된다.
- 프리셋 저장소는 프리셋을 공유 코드로 싣기 때문에 외형 데이터(`Load data`)가 있어야 저장 · 읽기가 된다.
- 화면 확인은 `Resource/engine/automation/editor/charactereditor.scenario.xml` 이 탭마다 스크린샷을 남긴다. 미리보기 씬이 필요한 일(기즈모 · 썸네일 · 피팅 계산 연결)은 백로그 `D4-8` ⑥ (a).
