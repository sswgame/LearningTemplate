# CharacterEditor — 캐릭터 편집 에디터 플러그인

`GF_EditorPlugIn_CharacterEditor` 는 캐릭터 편집 창(`docs/plans/EditorCharacterEditor.md`)의 **첫 단계**입니다. 화면 없이 테스트할 수 있는 편집 모델과, 그 모델을 그리는 도구 패널 뼈대까지 들어 있습니다.
미리보기 씬이 필요한 부분(캐릭터 스폰, 뷰포트 기즈모, 피팅 표시, 애니메이션 재생, 썸네일)은 `EditorMultiWorld` 단계가 끝난 뒤 이어 붙입니다 — 남은 일은 `docs/06_Backlog.md` 의 `D4-8` ⑥ 에 있습니다.

## 구조

| 자리 | 하는 일 |
|---|---|
| `Model/EditSnapshotHistory.h` | 스냅샷 되돌리기 기록. 같은 키의 연속 편집(끌기)을 한 단계로 합친다 |
| `Model/SocketEditModel.{h,cpp}` | 소켓 에셋(`*.sockets.xml`) 편집. 추가 · 삭제 · 이름 바꾸기(후보 목록이 따라감) · 부모 · 종류 · 변환 · 앵커 · 후보, 좌우 대칭, 검증, 읽기 · 쓰기 |
| `Model/CharacterMirrorUtil.{h,cpp}` | 좌우 이름 규칙 표(`MirrorNameRules`)와 위치 · 회전 뒤집기 |
| `Model/AppearanceEditModel.{h,cpp}` | 외형 선택(`AppearanceSelection`) 편집. 슬롯 아이템 · 체형 · 얼굴 · 꾸미기 값 · 시드, 프리셋 저장소로 내보내기 |
| `CharacterEditorPanel` / `CharacterEditorCommands.cpp` | 패널 `charactereditor.main`(소켓 탭, 외형 탭)과 메뉴 커맨드 `charactereditor.openPanel` |

- **편집 대상은 원본 데이터 파일**이다. 조립 결과(병합 메시, 펼친 외형)는 저장하지 않는다.
- **되돌리기는 모델마다 따로** 돈다(창 안에서만). 전역 `CommandStack` 에 섞지 않는다. 문서 패널 계약(`EditorDocumentPanel`)으로 옮기는 일은 `EditorMultiWorld` 뒤의 일이다.
- **저장은 적은 필드만 쓴다.** `SocketSet::saveToXMLText` 가 `_fieldMask` 로 쓸 필드를 고르므로, 값을 고치는 편집은 그 필드 비트를 함께 켠다. 비트를 켜지 않으면 고친 값이 저장되지 않는다.
- **좌우 짝은 이름 규칙으로만** 찾는다(`_L`/`_R`, `Left`/`Right` 등). 토큰은 이름의 앞 · 뒤나 구분자(`_ . - 공백`), CamelCase 경계에 놓일 때만 인정한다 — `Shoulder_Pad` 의 `r_` 은 짝이 아니다.
- 모델은 ImGui 와 씬을 모른다. `Model/*.cpp` 는 EngineTest 가 직접 컴파일한다(`Test/EngineTest/Character/TestCharacterEditorModel.cpp`).

## 함정

- 파일을 쓰면 소켓 캐시의 핫 리로드가 같은 파일을 다시 읽는다. 지금 패널은 읽은 결과를 모델에 다시 반영하지 않으므로 겨루지 않지만, 미리보기 씬이 붙으면 저장 직후 감시를 한 번 무시해야 한다.
- 부모 본 검증은 `SocketEditModel::setBoneNames` 로 본 목록을 넣어야 켜진다. 패널은 아직 스켈레톤을 모르므로 부모 이름을 검증하지 않는다.
