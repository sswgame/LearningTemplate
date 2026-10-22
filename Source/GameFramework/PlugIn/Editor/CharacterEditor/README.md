# CharacterEditor — 캐릭터 편집 에디터 플러그인

`GF_EditorPlugIn_CharacterEditor` 는 캐릭터 편집 창(`docs/plans/EditorCharacterEditor.md`)입니다. 화면 없이 테스트할 수 있는 편집 모델과, 그 모델을 그리고 선택을 입은 몸을 미리보기 씬에 세우는 창이 들어 있습니다.
남은 일(체형 슬라이더 · 피팅 계산 연결 · 썸네일 · 뷰포트 기즈모)은 `docs/plans/EditorCharacterEditor.md` 의 "남은 것" 절에 있습니다.

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
| `Model/PairedAnimationEditModel.{h,cpp}` | 페어드 애니메이션(`*.pairedanim.xml`) 편집. 길이 · 역할 시작 · 블렌드 · 정렬 오프셋(기즈모 역변환 포함) · 워프 끝 · 공유 알림, 되돌리기, 한 시계 미리보기 계산. 쓰기는 `PairedAnimationDef::makeXMLText`, 규칙 검사는 그 글을 런타임 로더로 다시 읽는다 |
| `PairedAnimationPanel` | 미리보기 창 `charactereditor.pairedanimation`(커맨드 `charactereditor.openPairedAnimation`). 역할마다 프리팹을 놓고(저장하지 않는 미리보기 설정) 스크럽, 정렬 역할을 기즈모로 옮기면 오프셋으로 들어간다 |
| `CharacterEditorPanel` / `CharacterEditorCommands.cpp` | 미리보기 창 `charactereditor.main`(소켓 탭, 스켈레톤 탭, 외형 탭, 미리보기 탭)과 메뉴 커맨드 `charactereditor.openPanel`. 문서는 소켓 파일, 미리보기 몸은 팩 데이터의 `previewPreset` 위에 외형 선택을 입힌 조립(`CharacterAppearanceComponent::previewSelection`). 미리보기 탭의 재생 줄은 팩 데이터 `clipFolder` 의 `.animclip` 을 콤보로 고르고 재생 · 정지(반복 선택)한다 |

- **편집 대상은 원본 데이터 파일**이다. 조립 결과(병합 메시, 펼친 외형)는 저장하지 않는다.
- **씬이 쓰는 결과는 데이터 프리셋이다.** 외형 탭의 "Export data preset" 이 선택을 프리셋 에셋(`<폴더>/<id>.appearance.xml`, 기본 폴더는 외형 폴더의 `presets/`)으로 쓰고
  창과 실행 중인 게임의 `AppearanceDatabase` 에 올린다(`loadPresetFile`). 씬의 `CharacterAppearanceComponent` 는 그 id 를 적는다. "Save as preset" 은 플레이어 세이브 형식(`UserAppearancePresetStore`)이라 씬이 읽지 않는다.
  확인은 `Resource/game/shooter3d/automation/charactereditorscene.scenario.xml`(내보내기는 임시 폴더 `editor/automationtemp/` 로)이다.
- **되돌리기는 모델마다 따로** 돈다(창 안에서만). 전역 `CommandStack` 에 섞지 않는다. 미저장 · 저장 · 닫기 확인은 문서 패널 계약(`EditorDocumentPanel`)이다 — 패널 dirty 는 그리기마다 모델(과 미리보기 창 스택)을 정본으로 다시 맞춘다.
- **재생할 클립 폴더는 팩 데이터의 필수 칸 `clipFolder` 다**(`charactereditor.xml` 의 `clipFolder`, 비면 읽기 오류 — 코드에 기본 폴더를 두지 않는다). 미리보기 몸에는 `SkeletalAnimatorComponent` 가 붙고 클립 이름은 그 폴더의 `.animclip` 파일 이름이다.
- **미리보기 몸은 창의 외형 데이터만 본다.** 게임 서비스 `AppearanceDatabase` 는 메인 씬의 것이라 미리보기 씬이 쓰지 않는다. 외형 데이터를 읽기(`Load data`) 전에는 몸이 없다.
- **저장은 적은 필드만 쓴다.** `SocketSet::saveToXMLText` 가 `_fieldMask` 로 쓸 필드를 고르므로, 값을 고치는 편집은 그 필드 비트를 함께 켠다. 비트를 켜지 않으면 고친 값이 저장되지 않는다.
- **좌우 짝은 이름 규칙으로만** 찾는다(`_L`/`_R`, `Left`/`Right` 등). 토큰은 이름의 앞 · 뒤나 구분자(`_ . - 공백`), CamelCase 경계에 놓일 때만 인정한다 — `Shoulder_Pad` 의 `r_` 은 짝이 아니다.
- 모델은 ImGui 와 씬을 모른다. `Model/*.cpp` 는 EngineTest 가 직접 컴파일한다(`Test/EngineTest/Character/TestCharacterEditorModel.cpp`).

## 함정

- 파일을 쓰면 소켓 캐시의 핫 리로드가 같은 파일을 다시 읽는다. 패널은 읽은 결과를 모델에 다시 반영하지 않고, 저장 뒤 3초 안의 다시 읽기(`SaveReloadGuard`)만 외부 변경이 아닌 것으로 거른다.
  횟수는 모든 소켓 에셋이 나눠 쓰므로 다른 파일의 다시 읽기와는 구별하지 못한다.
- 부모 본 검증은 스켈레톤을 읽은 뒤에 켜진다(`Load skeleton` → `SocketEditModel::setBoneNames`).
- 스켈레톤은 재임포트가 다시 쓴다 — 포즈 편집은 `*.refpose.xml` 에만 간다. 좌우 대칭은 덮어쓰기가 있는 본만 된다.
- 프리셋 저장소는 프리셋을 공유 코드로 싣기 때문에 외형 데이터(`Load data`)가 있어야 저장 · 읽기가 된다.
- 화면 확인은 `Resource/engine/automation/editor/charactereditor.scenario.xml` 이 탭마다(미리보기 포함) 스크린샷을 남긴다. 창을 열면 소켓 파일이 바로 읽힌다(문서 계약의 첫 읽기).
- 소켓 축은 몸이 **조립한** 소켓 파일의 자리다 — 편집 중인 값은 저장한 뒤 다음 조립에 보인다.
