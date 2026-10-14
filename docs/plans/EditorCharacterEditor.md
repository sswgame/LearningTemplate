# 캐릭터 편집 창 구현 계획 (D4-8 ⑥)

백로그 `D4-8` ⑥ 의 설계 근거입니다. 다른 PC 에서 병합할 때 판단 기준으로 씁니다. 코드는 아직 바꾸지 않았습니다.
파일 · 함수 이름은 2026-10-14 `wt/editor` 기준으로 읽은 것입니다.

## 1. 선행 조건

- [EditorMultiWorld.md](EditorMultiWorld.md) 의 단계 1~4(미리보기 씬 · 씬별 GPUScene · 호스트 뷰 목록 · `EditorPreviewPanel` 틀과 창별 선택 · Undo)가 끝나야 합니다.
  캐릭터 창은 그 틀 위의 창 하나입니다. 메인 씬을 빌리는 임시 구현은 하지 않습니다(프리팹 격리가 겪은 "씬이 더러우면 막는다" 문제를 되풀이하게 됩니다).
- `D4-8` ①~⑤ 의 **통합** 항목 중 "체형 모프 · 피팅 델타를 GPU 모프 풀에 싣기" 와 "`Mesh` · 포즈 ↔ `AppearanceGeometry` · `CharacterBoneArray` 변환" 이 있어야 체형 슬라이더와 피팅 결과가 화면에 보입니다. 없으면 단계 3 이후를 미룹니다.

## 2. 현재 있는 것

### 2.1 엔진 — `Source/Engine/Character`
| 영역 | 파일 | 창에서 쓰는 것 |
|---|---|---|
| 소켓 | `Socket/SocketSet.h`(`SocketDef` · `SocketKindTable`), `SocketSetComponent.h`(`*.sockets.xml` 을 `SocketSetCache` 에서 받고 파일이 바뀌면 다시 읽음), `SocketBindingComponent`, `ResolvedSocketTable`, `SocketImportUtil` | 소켓 목록 · 추가 · 이동 · 저장 |
| 체형 | `Fit/BodyShape.h`(`BodyShapeSet`, `*.bodyshape.xml`, `BoneProportion`) | 체형 슬라이더 |
| 피팅 | `Fit/FitSolver.h` — `FitSolver::solve( FitTables, FitInput, FitResult& )`, 결과 `FitPartResult`(삼각형 보임 마스크 · 바인드 공간 정점 델타 `_listVertexDelta`), `FitPenetration`, `FitReport` | 관통 표시 · 잘린 면 · 보고 |
| 형상 | `Fit/CharacterGeometry`, `GeometryCut`, `MeshMerger`, `SurfaceTransfer`, `SurfaceState` | 병합 미리보기 · 표면 상태 |
| 포즈 | `PoseModifier/ReferencePoseOverride`, `CharacterPoseUtil`, `PoseModifierComponent` | 레퍼런스 포즈 편집 |
| 데이터 | `CharacterDataReader` · `CharacterDataCache` | 파일 로드 · 핫 리로드 |

스켈레톤 에셋은 `Engine/Animation/Skeletal/Skeleton.h`(`SkeletonBone` · `SkeletonAttachment`, `.skeleton.json`)이며 본 · 레퍼런스 포즈 · 역 바인드와 임포트가 적은 본 부착 메시 테이블뿐입니다. 편집 진입점은 없습니다.

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
- 선행: `D4-8` 통합의 GPU 모프 풀 연결.
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
- 위험: 스켈레톤 `.skeleton.json` 은 임포트 산출물이라, 레퍼런스 포즈를 여기에 직접 쓰면 다시 임포트할 때 덮어씁니다 — 그래서 `ReferencePoseOverride` 파일에 씁니다.
