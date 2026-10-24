# 캐릭터 편집 창 구현 계획 (D4-8 ⑥)

> 상태: 측정대기 · 선행: 없음 · 영역: gameplay · 옛번호: D4-8 · 진행: 6/6

캐릭터 외형 편집 창의 설계 근거입니다(외형 통합 쪽은 [CharacterAppearanceIntegration](CharacterAppearanceIntegration.md)). 다른 PC 에서 병합할 때 판단 기준으로 씁니다. 코드는 아직 바꾸지 않았습니다.
파일 · 함수 이름은 2026-10-14 `wt/editor` 기준으로 읽은 것입니다.

## 1. 선행 조건

- [EditorMultiWorld.md](EditorMultiWorld.md) 의 단계 1~4(미리보기 씬 · 씬별 GPUScene · 호스트 뷰 목록 · `EditorPreviewPanel` 틀과 창별 선택 · Undo)가 끝나야 합니다.
  캐릭터 창은 그 틀 위의 창 하나입니다. 메인 씬을 빌리는 임시 구현은 하지 않습니다(프리팹 격리가 겪은 "씬이 더러우면 막는다" 문제를 되풀이하게 됩니다).
  단계 1~4 는 main 에 들어왔다(같은 플러그인의 `PairedAnimationPanel` 이 이미 `EditorPreviewPanel` 을 잇는다). `EditorMultiWorld` 에 남은 것(스카이 패스 · 풀 공유 측정 · 렌더러 내리기와 백엔드 교체 시나리오 실행)은 이 창을 막지 않는다.
- `D4-8` ①~⑤ 의 **통합** 항목 중(단계 3 · 5 만 막는다) "체형 모프 · 피팅 델타를 GPU 모프 풀에 싣기" 와 "`Mesh` · 포즈 ↔ `AppearanceGeometry` · `CharacterBoneArray` 변환" 이 있어야 체형 슬라이더와 피팅 결과가 화면에 보입니다. 없으면 단계 3 이후를 미룹니다.

## 2. 현재 있는 것

### 2.1 엔진 — `Source/Engine/Character`
| 영역 | 파일 | 창에서 쓰는 것 |
|---|---|---|
| 소켓 | `Socket/SocketSet.h`(`SocketDef` · `SocketKindTable`), `SocketSetComponent.h`(`*.sockets.xml` 을 `SharedAssetCache<SocketSet>` 에서 받고 파일이 바뀌면 다시 읽음), `SocketBindingComponent`, `ResolvedSocketTable`, `SocketImportUtil` | 소켓 목록 · 추가 · 이동 · 저장 |
| 체형 | `Fit/BodyShape.h`(`BodyShapeSet`, `*.bodyshape.xml`, `BoneProportion`) | 체형 슬라이더 |
| 피팅 | `Fit/FitSolver.h` — `FitSolver::solve( FitTables, FitInput, FitResult& )`, 결과 `FitPartResult`(삼각형 보임 마스크 · 바인드 공간 정점 델타 `_listVertexDelta`), `FitPenetration`, `FitReport` | 관통 표시 · 잘린 면 · 보고 |
| 형상 | `Fit/CharacterGeometry`, `GeometryCut`, `MeshMerger`, `SurfaceTransfer`, `SurfaceState` | 병합 미리보기 · 표면 상태 |
| 포즈 | `PoseModifier/ReferencePoseOverride`, `CharacterPoseUtil`, `PoseModifierComponent` | 레퍼런스 포즈 편집 |
| 데이터 | `CharacterDataReader` · `CharacterDataCache` | 파일 로드 · 핫 리로드 |

스켈레톤 에셋은 `Engine/Animation/Skeletal/Skeleton.h`(`SkeletonBone` · `SkeletonAttachment`, `.skeleton.xml`)이며 본 · 레퍼런스 포즈 · 역 바인드와 임포트가 적은 본 부착 메시 테이블뿐입니다. 편집 진입점은 없습니다.

### 2.2 게임프레임워크 — `Source/GameFramework/Base/Gameplay/Appearance`
- `CharacterAppearanceComponent` — 프리셋(`CharacterAppearance`)을 펼쳐 `AppearanceResolver` 로 해석하고 몸 메시 · 장비 부품 · 소켓 부착 · 염색을 조립합니다. 소켓 위치 갱신은 `CharacterAppearanceSocketTask`(포즈가 끝난 프레임).
- `AppearanceResolver` 의 결과에 `_listTrace`(`AppearanceTraceEntry`: `_message` · `_source` · `_step`)가 있습니다 — "규칙 설명" 표시의 원천입니다.
- `CustomizationSchema` — 슬라이더 · 색이 움직이는 대상 `CustomizationDriveKind`(`Morph` · `BoneProportion` · `MaterialScalar` · `MaterialColor` · `DyeChannel` · `PaletteSwap`), 카테고리("Hair" · "Face" · "Dye").
- `SlotTable` · `EquipSetCatalog` · `ItemVisual` · `AppearanceRule` · `AppearanceSocketRig` · `UserAppearancePresetStore` · `AppearanceDatabase`.

Engine 은 GameFramework 를 모르므로, 창은 **에디터 쪽에서 GameFramework 의 외형 컴포넌트를 미리보기 씬에 스폰**합니다(에디터는 둘 다 봅니다).

## 3. 창 구성

`CharacterEditorPanel`(가칭, `Source/Editor/Panels/Character/`)은 `EditorPreviewPanel` 을 상속합니다. 미리보기 씬에는 캐릭터 오브젝트 하나(`SkeletalMeshComponent` + `SocketSetComponent` + `CharacterAppearanceComponent`)와 기본 조명 · 바닥을 스폰합니다. 틱 정책은 "틱 함 · 물리 안 함"(애니메이션 재생용).

| 구역 | 내용 | 쓰는 것 | 저장 대상 |
|---|---|---|---|
| 본 트리 | `Skeleton` 본 계층, 선택 본에 기즈모 | `EditorViewportGizmo`, `ReferencePoseOverride` | 레퍼런스 포즈 덮어쓰기 파일 |
| 소켓 | 목록 · 추가 · 이동 · 부모 본 바꾸기, 뷰포트에 소켓 축 표시 | `SocketSetComponent`, `SocketDef` | `*.sockets.xml` |
| 체형 · 얼굴 | 슬라이더(`CustomizationDriveKind::Morph` · `BoneProportion`), 카테고리별 탭 | `CustomizationSchema`, `BodyShapeSet` | 프리셋 |
| 장비 입히기 | 슬롯별 아이템 · 세트 입히기 | `SlotTable`, `EquipSetCatalog`, `ItemVisual` | 프리셋 |
| 피팅 확인 | 관통 표시(점 · 색), 잘린 면 · 숨김 영역 · 조임 강도 · 보정 조각 켜고 끄기 | `FitResult` · `FitPenetration` · `FitReport` | 없음(보기 전용) |
| 규칙 설명 | 어느 규칙이 무엇을 숨기고 바꿨는지, 규칙 충돌 | `AppearanceResolver::_listTrace` | 없음 |
| 좌우 대칭 | 본 · 소켓 편집을 반대편 이름에 적용(`_L`/`_R` 등 이름 규칙 테이블) | 새 도우미 `CharacterMirrorUtil` | 위와 같음 |
| 재생 | 애니메이션 재생 · 정지 · 프레임 이동, 제약 리그 미리보기 | 애니메이션 컴포넌트 | 없음 |
| 프리셋 | "지금 모습을 프리셋으로 저장" · 썸네일 렌더 | `UserAppearancePresetStore`, 렌더 타깃 읽기 | 프리셋 파일 |

편집은 모두 창의 `CommandStack` 에 들어가므로 Undo 는 창 안에서만 돕니다. 파일 저장은 `EditorDocumentPanel` 의 더티 · 저장 계약을 따릅니다.

## 4. 단계 분할

각 단계는 끝나면 Debug `all AllTests` 가 경고 0 으로 빌드되고 테스트가 통과하는 크기입니다.

### 단계 1 — 빈 창과 캐릭터 스폰
- 파일: `Editor/Panels/Character/CharacterEditorPanel.h/.cpp`(새), 에디터 패널 등록, `EditorAssetType`(스켈레톤 · 프리셋 포커스 연결).
- 위험: GameFramework 서비스(`AppearanceDatabase`)가 미리보기 씬에서도 찾아지는지.
- 확인: 에디터 시나리오 — 프리셋을 열면 창이 뜨고 메인 씬 오브젝트 수가 그대로. 스모크는 `-gv_profileFrames=60` 을 붙여 스스로 끝나게.

### 단계 2 — 본 트리 · 소켓 편집 · 저장
- 파일: 위 패널, `Character/Socket/SocketSet.*`(쓰기 함수 — 지금은 읽기만), `Animation/Skeletal/Skeleton.*`(레퍼런스 포즈 쓰기 여부 판단), `PoseModifier/ReferencePoseOverride.*`.
- 위험: `*.sockets.xml` 을 쓰면 핫 리로드가 같은 프레임에 다시 읽습니다 — 편집 중 다시 읽기와 겨루지 않게 저장 직후 감시 무시 한 번.
- 확인: 소켓 쓰기 → 읽기 왕복 단위 테스트, Undo 시나리오.

### 단계 3 — 체형 · 얼굴 슬라이더
- 선행: `CharacterAppearanceIntegration`(옛 D4-8)의 GPU 모프 풀 연결.
- 파일: 패널, `CustomizationSchema` 를 읽어 슬라이더 생성(GameFramework 는 바꾸지 않음).
- 확인: 슬라이더 값 → 프리셋 값 왕복, 이미지 비교(값을 바꾸면 이미지가 달라야 함).

### 단계 4 — 장비 입히기 · 규칙 설명
- 파일: 패널(슬롯 · 세트 UI, `_listTrace` 표시).
- 확인: 규칙이 숨기는 세트를 입혔을 때 설명 줄이 나옴을 에디터 테스트로.

### 단계 5 — 피팅 확인 표시
- 파일: 패널, 디버그 그리기로 관통 점 표시, `FitSolver` 결과 보관.
- 위험: 피팅은 무겁습니다 — 슬라이더를 끄는 동안은 돌리지 않고 놓을 때 한 번.
- 확인: 일부러 관통하는 테스트 에셋에서 `FitPenetration` 개수 > 0 이고 표시가 켜짐.

### 단계 6 — 좌우 대칭 · 애니메이션 재생 · 프리셋 저장 · 썸네일
- 파일: `CharacterMirrorUtil`(새, 이름 규칙 테이블은 데이터 파일), 패널.
- 확인: 대칭 이름 매칭 단위 테스트, 프리셋 저장 왕복.

## 5. 결정과 위험

- **편집 대상은 원본 데이터 파일**(소켓 XML · 프리셋 · 레퍼런스 포즈)입니다. 조립 결과(병합 메시)는 저장하지 않습니다.
- **피팅 표시는 보기 전용**입니다. 보정 조각 작성은 이 창의 범위 밖(DCC 도구)입니다.
- **좌우 대칭은 이름 규칙으로** 짝을 찾습니다. 위치 기반 짝 찾기는 비대칭 리그에서 틀리므로 쓰지 않습니다.
- 위험: 스켈레톤 `.skeleton.xml` 은 임포트 산출물이라, 레퍼런스 포즈를 여기에 직접 쓰면 다시 임포트할 때 덮어씁니다 — 그래서 `ReferencePoseOverride` 파일에 씁니다.

## 남은 것

첫 단계(소켓 파일 · 외형 선택 편집 모델과 패널, 스켈레톤 본 트리 · 레퍼런스 포즈 숫자 편집 `BonePoseEditModel`, 좌우 이름 표, 장비 칸 콤보 · 세트 입히기 · 규칙 설명, 프리셋 파일 저장, 피팅 요약 `FitCheckSummary`, 자기 저장 뒤 핫 리로드 무시 `SaveReloadGuard`)는 `GameFramework/PlugIn/Editor/CharacterEditor` 에 들어갔다. 기본 경로는 shooter3d 데이터다(경로 칸에서 바꾼다).

- **단계 1 은 들어갔다(2026-10-22, 빌드 미확인)**: `CharacterEditorPanel` 이 `EditorPreviewPanel` 을 상속한다.
  - 문서는 소켓 파일이다(`EditorAssetType::Data` 가운데 `SocketSet::kExtension` 만 받는다). 열면 팩 데이터의 소켓 파일을 읽고, 콘텐츠 브라우저에서 소켓 파일을 고르면 그 파일로 바뀐다(미저장 확인 포함). 저장 · 닫기 확인은 `EditorDocumentPanel` 계약이고 창의 Save 단추도 `saveDocumentAndClearDirty` 한 길이다.
  - 미리보기 몸: 팩 데이터에 필수 칸 `previewPreset`(몸 프리셋 id, Shooter3D · Empty 는 `Adventurer` = 기사 몸)을 더했다. 몸은 프리팹이 아니라 외형 조립이다 — 미리보기 씬에 `SkeletalMeshComponent` + `CharacterAppearanceComponent` 오브젝트 하나를 세우고 `CharacterAppearanceComponent::previewSelection( 선택, 창의 AppearanceDatabase )` 로 입힌다.
    그 길을 고르면 컴포넌트는 게임 서비스와 프리셋 id 를 보지 않는다(두 길이 섞이지 않는다). 선택에 기준 프리셋이 없으면 `previewPreset` 위에 입히고, 선택 · 외형 데이터의 세대가 바뀔 때만 다시 입힌다. 미리보기 씬은 플레이를 돌리지 않아 시작을 기다리지 않고 조립한다.
  - 미리보기 탭(다른 탭의 줄을 밀지 않는다)과 소켓 축 표시(몸이 조립한 소켓 = 저장된 파일, 고른 소켓은 길고 밝게). 탐침 `Editor.CharacterEditorPreviewAssembleCount`, 시나리오 `engine/automation/editor/charactereditor` 에 미리보기 탭 · 스크린샷을 더했다.
- **애니메이션 재생은 들어갔다(2026-10-22, Debug 빌드 · 시나리오 `charactereditor` 로 확인 — 클립을 고르고 Play 하면 T 자세의 몸이 그 클립의 자세로 움직이는 것을 화면으로 봤다)**: 미리보기 탭 위의 줄에서 팩 데이터의 클립 폴더(필수 칸 `clipFolder`, 비면 읽기 오류)에 있는 `.animclip` 을 콤보로 고르고 반복 · Play · Stop 한다. 미리보기 몸에 `SkeletalAnimatorComponent` 를 더했다.
  함정 둘이 드러났다 — (1) 애니메이터는 `onBeginPlay` 에서 같은 오브젝트의 유닛(`SkeletalMeshComponent`)에 붙는데 미리보기 씬은 기본 정책에서 플레이를 돌리지 않는다. 그래서 이 창은 정책 `_bPlay` 를 켠다(`getPreviewPolicy`). (2) 컴포넌트는 유닛을 먼저 더하고 애니메이터를 나중에 더한다(순서가 거꾸로면 붙지 못한다). 탐침 `Editor.CharacterEditorPreviewClipPlaying`.
- dirty 는 들어갔다(2026-10-21): 모델의 미저장 상태를 패널 dirty 로 옮기고(`syncDocumentDirty`, 이제 미리보기 창 스택의 편집도 더한다) "Don't Save" 는 두 모델을 저장된 상태로 돌린다(`revertDocument` → `revertToSaved`).
- **단계 3 · 5 · 6 의 막힌 선행이 풀렸다(2026-10-24, 빌드 미확인)**:
  - 단계 3 슬라이더: 미리보기 탭의 "Customize" 칸이 꾸미기 스키마(`CustomizationSchema`, 선택의 `_schema` 또는 기준 프리셋의 것)의 매개변수마다 슬라이더 · 색 · 고르기를 카테고리로 가르고 조건이 맞지 않는 것은 숨겨 그린다
    (`CharacterEditorPanel::drawCustomizationSliders`). 몸은 CPU 에서 모프를 굽는 길(`AppearanceMeshAssembly`)로 바뀌므로 GPU 모프 풀을 기다릴 이유가 없었다. 끄는 동안(`_bCustomizationActive`)은 다시 입히지 않고 놓을 때 한 번 입힌다.
  - 단계 5 피팅 확인: 미리보기 입히기(`previewSelection`)는 구운 메시 캐시를 건너뛰고 조립이 푼 `FitResult` 를 컴포넌트에 남긴다(`getLastFitResult` · `getFitResultRevision`, `AppearanceMeshAssembly::assemble` 의 출력 인자).
    창은 세대가 바뀔 때 `setFitResult` 로 올려 요약(잘린 면 · 조임 강도 · 관통 수 · 깊이)을 보인다.
  - 단계 6 썸네일: `EditorPreviewViewport::requestImageFile`(렌더 타깃 한 장을 파일로, `RenderThread::requestScreenshot`)을 더했다. "Save thumbnail" 이 같은 이름의 프리셋 옆에 `<프리셋 파일>.<이름>.png` 를 쓰고
    `UserAppearancePresetStore::setThumbnailPath` 로 잇는다(프리셋을 먼저 저장해야 하고, 현재 선택으로 다시 저장한다).
- **남았던 것이 들어갔다(2026-10-24, 빌드 미확인)**:
  - 단계 5 의 관통 점 표시: 풀이기(`FitOperator::countInside`)가 관통 점의 바인드 공간 위치를 `FitPenetration::_listPoint` 에 모으고(상한 `kMaxPointCount`), 요약(`FitPenetrationSummary::_listPoint`)이 받아, 미리보기 탭이 몸의 월드 변환으로 옮겨 빨간 십자로 긋는다(`drawPenetrationPoints`, 탐침 `Editor.CharacterEditorPenetrationMarks`).
  - 뷰포트 기즈모 포즈 편집: 스켈레톤 탭에서 고른 본 자리에 표시 오브젝트를 놓고 창 선택에 올려 기즈모가 잡는다(`syncBoneGizmo`, 탐침 `Editor.CharacterEditorBoneGizmoPlaced`). 옮기면 부모 월드의 역으로 본의 로컬을 구해 레퍼런스 포즈 덮어쓰기에 넣는다(끄는 동안은 되돌리기 한 단계).
  - 슬라이더의 대칭 그룹(`_symmetry`) 동시 편집: 슬라이더 · 색을 바꾸면 같은 대칭 그룹의 같은 종류 짝을 같은 값으로(자기 범위로 잘라) 함께 바꾼다(`drawCustomizationSliders` — 모델이 아니라 스키마를 아는 패널이 한다).
  - 편집 중(저장 전) 소켓을 미리보기에 바로 보이기: 소켓 축을 조립한 소켓 파일이 아니라 편집 중인 소켓 정의로 구한다(`ResolvedSocketTable` 에 풀어 몸의 지금 포즈에 얹는다). 모르는 부모 본 같은 편집 오류는 상태 줄에 나오고 축은 그리지 않는다.
  - 핫 리로드 결과를 모델에 다시 반영: 고치지 않은 창은 바뀐 소켓 파일을 바로 다시 읽는다. 고친 것이 있으면 "Reload from disk (discard edits)" 단추로 사람이 정한다(말없이 덮지 않는다).
  - 제약 리그 미리보기: 팩 데이터의 선택 칸 `rig`(`CharacterEditorPackData::_rigPath`, 비면 단추가 없다)가 있으면 미리보기 탭에 "Preview constraint rig" 가 생기고, 켜면 미리보기 몸에 `PoseModifierComponent` 를 얹어 그 리그(스트라이드 워핑 · 양손 IK)를 돌린다. Shooter3D · Empty 데이터가 플레이어 몸 리그를 가리킨다.
- 확인(통합에서 확인): 매니페스트 id `editor-charactereditor-scenario`(슬라이더 · 기즈모 표식 · 리그 사진 포함 — 관통 점은 피팅 데이터가 겹치는 차림에서만 찍혀 시나리오 기본 차림에서는 0 이다), 매니페스트 id `fit-check-summary-test`(`FitCheckSummaryTest` — 관통 점이 요약까지 온다), 매니페스트 id `game-pack-data-test`(`GamePackDataTest` — 선택 칸 `rig` 읽기).
