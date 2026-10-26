# 캐릭터 편집 창 구현 계획 (D4-8 ⑥)

> 상태: 측정대기 · 선행: 없음 · 영역: gameplay · 옛번호: D4-8 · 진행: 6/6

캐릭터 외형 편집 창의 설계 근거입니다(외형 통합 쪽은 [CharacterAppearanceIntegration](CharacterAppearanceIntegration.md)). 코드는 `Source/GameFramework/PlugIn/Editor/CharacterEditor` 에 들어갔고 기본 경로는 shooter3d 데이터입니다(경로 칸에서 바꾼다).

## 1. 선행 조건 (충족)

[EditorMultiWorld.md](EditorMultiWorld.md) 의 단계 1~4(미리보기 씬 · 씬별 GPUScene · 호스트 뷰 목록 · `EditorPreviewPanel` 틀)가 main 에 들어왔다. 캐릭터 창은 그 틀 위의 창 하나이고 메인 씬을 빌리는 임시 구현은 하지 않는다(프리팹 격리가 겪은 "씬이 더러우면 막는다" 문제를 되풀이한다). 체형 슬라이더 · 피팅 결과가 화면에 보이는 데 필요한 모프 · 피팅 연결은 CPU 에서 모프를 굽는 길(`AppearanceMeshAssembly`)로 풀렸다.

## 2. 현재 있는 것

### 2.1 엔진 — `Source/Engine/Character`

| 영역 | 파일 | 창에서 쓰는 것 |
|---|---|---|
| 소켓 | `Socket/SocketSet.h`(`SocketDef`), `SocketSetComponent.h`(`*.sockets.xml` 을 `SharedAssetCache<SocketSet>` 에서 받고 파일이 바뀌면 다시 읽음), `SocketBindingComponent`, `ResolvedSocketTable`, `SocketImportUtil` | 소켓 목록 · 추가 · 이동 · 저장 |
| 체형 | `Fit/BodyShape.h`(`BodyShapeSet`, `*.bodyshape.xml`) | 체형 슬라이더 |
| 피팅 | `Fit/FitSolver.h` — `FitSolver::solve`, `FitPartResult`, `FitPenetration`, `FitReport` | 관통 표시 · 잘린 면 · 보고 |
| 형상 · 포즈 · 데이터 | `Fit/CharacterGeometry` · `GeometryCut` · `MeshMerger`, `PoseModifier/ReferencePoseOverride`, `CharacterDataReader` · `CharacterDataCache` | 병합 미리보기 · 레퍼런스 포즈 · 핫 리로드 |

스켈레톤 에셋(`Engine/Animation/Skeletal/Skeleton.h`, `.skeleton.xml`)은 임포트 산출물이고 편집 진입점이 없다.

### 2.2 게임프레임워크 — `Source/GameFramework/Base/Gameplay/Appearance`

`CharacterAppearanceComponent`(프리셋을 `AppearanceResolver` 로 해석해 몸 메시 · 장비 부품 · 소켓 부착 · 염색 조립), `AppearanceResolver` 결과의 `_listTrace`("규칙 설명"의 원천), `CustomizationSchema`(`CustomizationDriveKind`: `Morph` · `BoneProportion` · `MaterialScalar` · `MaterialColor` · `DyeChannel` · `PaletteSwap`), `SlotTable` · `EquipSetCatalog` · `ItemVisual` · `UserAppearancePresetStore` · `AppearanceDatabase`.
Engine 은 GameFramework 를 모르므로 창은 **에디터 쪽에서 외형 컴포넌트를 미리보기 씬에 스폰**한다.

## 3. 창 구성

`CharacterEditorPanel` 은 `EditorPreviewPanel` 을 상속합니다. 미리보기 씬에는 캐릭터 오브젝트 하나(`SkeletalMeshComponent` + `SocketSetComponent` + `CharacterAppearanceComponent`)와 기본 조명 · 바닥. 틱 정책은 "틱 함 · 물리 안 함" + 플레이 켬(`_bPlay` — 애니메이터가 `onBeginPlay` 에서 유닛에 붙고, 컴포넌트는 유닛을 먼저 더하고 애니메이터를 나중에 더한다).

| 구역 | 내용 | 저장 대상 |
|---|---|---|
| 본 트리 | 본 계층, 선택 본에 기즈모(`syncBoneGizmo`, 부모 월드의 역으로 로컬을 구해 덮어쓰기에 넣음) | 레퍼런스 포즈 덮어쓰기 파일 |
| 소켓 | 목록 · 추가 · 이동 · 부모 본 바꾸기, 뷰포트 소켓 축(고른 것은 길고 밝게, 편집 중 정의로 바로) | `*.sockets.xml` |
| 체형 · 얼굴 | `CustomizationSchema` 의 매개변수마다 슬라이더 · 색 · 고르기(카테고리 탭, 대칭 그룹 `_symmetry` 짝 동시 편집). 끄는 동안은 다시 입히지 않고 놓을 때 한 번 | 프리셋 |
| 장비 입히기 · 규칙 설명 | 슬롯별 아이템 · 세트, `_listTrace` 표시 | 프리셋 |
| 피팅 확인 | 잘린 면 · 조임 강도 · 관통 수 · 깊이 요약(`FitCheckSummary`), 관통 점 빨간 십자(`drawPenetrationPoints`). 보기 전용 | 없음 |
| 좌우 대칭 | 이름 규칙 표(`_L`/`_R`, 데이터 파일)로 짝 찾기 | 위와 같음 |
| 재생 | 팩 데이터 `clipFolder` 의 `.animclip` 콤보 · 반복 · Play · Stop, 선택 칸 `rig` 가 있으면 "Preview constraint rig" | 없음 |
| 프리셋 | "지금 모습을 프리셋으로 저장" · "Save thumbnail"(`<프리셋 파일>.<이름>.png`) | 프리셋 파일 |

편집은 창의 `CommandStack` 에 들어가 Undo 는 창 안에서만 돕니다. 저장 · 닫기 확인은 `EditorDocumentPanel` 계약(미저장 상태는 `syncDocumentDirty`, "Don't Save" 는 `revertDocument`)이고 문서는 소켓 파일입니다. 미리보기 몸은 프리팹이 아니라 외형 조립이다: 팩 데이터의 필수 칸 `previewPreset` 위에 `CharacterAppearanceComponent::previewSelection` 으로 입히고, 이 길에서는 컴포넌트가 게임 서비스와 프리셋 id 를 보지 않는다.

## 4. 결정과 위험

- **편집 대상은 원본 데이터 파일**(소켓 XML · 프리셋 · 레퍼런스 포즈)이다. 조립 결과(병합 메시)는 저장하지 않는다.
- **피팅 표시는 보기 전용**이다. 보정 조각 작성은 범위 밖(DCC 도구).
- **좌우 대칭은 이름 규칙으로** 짝을 찾는다(위치 기반은 비대칭 리그에서 틀림).
- `.skeleton.xml` 은 임포트 산출물이라 레퍼런스 포즈를 직접 쓰면 재임포트에 덮이므로 `ReferencePoseOverride` 파일에 쓴다.
- `*.sockets.xml` 을 쓰면 핫 리로드가 다시 읽으므로 저장 직후 감시를 한 번 무시한다(`SaveReloadGuard`). 고치지 않은 창은 바뀐 파일을 바로 다시 읽고, 고친 것이 있으면 "Reload from disk (discard edits)" 로 사람이 정한다.
- 피팅은 무거우므로 슬라이더를 끄는 동안은 돌리지 않는다.

## 5. 남은 것

단계 1 ~ 6 은 모두 코드가 들어갔다(2026-10-22 ~ 24). 애니메이션 재생은 Debug 빌드 · `charactereditor` 시나리오로 화면까지 확인했고 나머지는 빌드 미확인이다.
확인(통합에서 확인): 매니페스트 id `editor-charactereditor-scenario` · `fit-check-summary-test` · `game-pack-data-test` (시나리오는 슬라이더 · 기즈모 표식 · 리그 사진 — 관통 점은 피팅 데이터가 겹치는 차림에서만 찍혀 기본 차림에서는 0, `FitCheckSummaryTest`, `GamePackDataTest` 는 선택 칸 `rig` 읽기). 탐침 `Editor.CharacterEditorPreviewAssembleCount` · `PreviewClipPlaying` · `PenetrationMarks` · `BoneGizmoPlaced`.
