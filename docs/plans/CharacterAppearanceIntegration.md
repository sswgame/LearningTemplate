# 캐릭터 외형의 통합 · 쿠킹 · 장비 잇기

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: D4-8 · 진행: 5/6

형상 쪽(`Source/Engine/Character`, README "외형 조립 순서")과 GPU 모프 풀(`Mesh::setGPUMorphEnabled`), 장비 해석 데이터 · 해석기(`GameFramework/Base/Gameplay/Appearance`)와 외형 컴포넌트(`CharacterAppearanceComponent`)는 들어갔습니다. 이어 붙이는 일이 남았습니다.
스켈레톤 에셋(`.skeleton.xml`)의 편집 창은 [EditorCharacterEditor](EditorCharacterEditor.md) 입니다.

## 1. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| E1 | 장비 해석 첫 조각 — 외형 컴포넌트가 해석을 `CharacterAppearanceState` 하나로 한다(`setEquipment` 로 빌린 `Equipment` 의 슬롯이 프리셋을 덮음, 버전이 그대로이면 다시 해석하지 않음). 지난 해석에 붙어 있다 떨어져 나간 소켓 부품은 지우지 않고 `SocketBindingComponent::release( Physics )`(충격 힌트가 시작 속도), 물리 바디가 없으면 알리고 지운다 | 코드, 빌드 · 테스트 미확인 |
| E2 | 통합 — 메시 ↔ 형상 다리(`CharacterMeshUtil` — 위치로 잇기 · 구운 사본), 몸 부품의 `fitTables` · 스킨드 부품의 `fit` 데이터, 피팅 · 해석의 숨김 영역 · 체형 모프를 구운 사본으로(`AppearanceMeshAssembly`, 결정 5-8 "외형의 체형 모프와 장비 피팅은 …"), 유닛의 스킨 원본 바꾸기(`SkeletalMeshComponent::setSkinSourceMesh`), 빈 구간(`Mesh::setSections`), 본 비율 연결(몸 부품의 `bodyShapes`(`*.bodyshape.xml`)가 축을 정하고 꾸미기 `BoneProportion` 드라이브의 값이 축 값이며, 외형 컴포넌트가 `BodyShapeSet::evaluate` 로 풀어 본 비율을 몸 포즈 위에 겹치고(`CharacterAppearanceProportionTask` — 비율이 있을 때만 등록해 군중 공유를 지킨다) 축의 모프를 구운 메시에 더한다. 체형 축 파일 + 몸 부품 `bodyShapes` 한 줄은 게임 데이터 일), **부품 병합**(아래 "E2 가 한 것"), **표면 상태 → 머티리얼**(아래) | 코드 · 테스트(`CharacterMeshUtilTest` · `AppearanceMeshAssemblyTest` · `CharacterSurfaceBindingTest`), 화면 미확인 |
| E3 | 쿠킹 — 임포트 규칙 `body_surface` · `body_surface_distance` 가 장비 메시에 몸의 스킨 · 체형 모프를 옮긴다(`ModelImporter::transferBodySurface` → `CharacterMeshUtil::transferBodySurface`, 먼 정점은 임포트 오류). **아틀라스 굽기**(아래 "E3 가 한 것") | 코드 · 테스트(`CharacterMeshUtilTest` · `OutfitAtlasTest` · `OutfitAtlasBakerTest`), 실제 장비 임포트 · 굽기 미확인 |
| E4 | 데이터 핫 리로드 — `AppearanceDataWatcher` 가 외형 폴더(낱개 파일)를 감시하고 `AppearanceDatabase::reloadFromFolder`(임시로 먼저 읽어 검사를 통과할 때만 바꾼다)를 부르면, 구독한 외형 컴포넌트(`registerReloaded`)가 다시 조립한다(Shooter3D 가 건다) | 코드, 빌드 · 실행 미확인 |
| E5 | 장비 해석의 나머지 — 부품 풀(병합에서 씬 서비스 `SocketPartPool` 하나로 합쳤다 — [ShooterCharacterIntegration](ShooterCharacterIntegration.md) S6, 결정 기록 5-8), 결과 키로 구운 메시 캐시(`AppearanceBakedMeshCache`, 다시 조립하는 동안 이전 외형 유지), 몸 영역 이름을 피팅 표에서(`AppearanceDatabase::loadFromFolder` → `finishLoad`), 프리셋 참조를 따라 미리 올릴 경로(`AppearancePreloadUtil`), **시퀀서 외형 항목**(아래 "E5 가 한 것"). 편집 창의 프리셋 저장 · 썸네일 · 규칙 설명은 [EditorCharacterEditor](EditorCharacterEditor.md) 에서 끝났다 | 코드 · 테스트(`SequencerTest` · `SequenceAppearanceTest`), 빌드 미확인 |
| E6 | 표면 셰이더 — 표면 상태가 올리는 이름(`Surface_<채널>` · `SurfaceRegion<영역>_<덩어리>` · `Surface_<채널>_Mask`)을 읽어 젖음(어둡게 + 거칠기) · 흙 · 피 · 찢김(알파 컷)을 그리는 머티리얼과 셰이더 | 남음 — 아래 "남은 것" |

## E2 가 한 것

- **부품 병합**: `CharacterMeshUtil::mergeMeshes` 가 같은 스켈레톤을 따르는 스킨드 부품(없는 부품은 스킨 없는 부품끼리)을 구간이 있는 메시 하나로 합친다 — 구간은 머티리얼 그룹마다 하나(그룹은 병합 훅 `IMeshMergeHooks` 가 고르고 훅이 없으면 부품 이름), 정점 · 스킨 · 모프 타깃(이름으로 합침)을 옮긴다. 이미 구간이 있거나 정점 애니메이션이 있는 부품, 스킨 유무 · 본 수가 섞인 부품은 거절한다.
  외형 컴포넌트의 프로퍼티 `Merge Parts`(기본 꺼짐)를 켜면 자기 리그가 없고 몸과 같은 스켈레톤인 스킨드 부품은 오브젝트를 만들지 않는다 — 구운 사본(없으면 원본)을 합쳐 몸 유닛에 걸고(`SkeletalMeshComponent::setSkinSourceMesh`) 몸은 자기 구간만 그리며(`setSectionName`),
  부품마다 몸 오브젝트의 `MeshSectionComponent` 가 자기 머티리얼로 그린다. 스키닝 · 포즈가 부품 수만큼 돌던 것이 한 번이다. 합친 메시는 같은 차림끼리 `AppearanceBakedMeshCache` 로 나눈다. 합치지 못하면 오류를 남기고 몸 원본만 그린다(폴백으로 다른 길을 타지 않는다). 탐침 `Appearance.MergedSections`.
  한계: 합친 메시는 LOD 를 갖지 못한다(구간과 LOD 는 함께 쓰지 못한다). 데이터에 합칠 스킨드 부품이 든 차림이 아직 없다(Shooter3D 의 스킨드 부품은 자기 리그가 있는 망토뿐이다) — 켜는 칸은 프리팹의 프로퍼티다.
- **표면 상태 → 머티리얼**: 외형 컴포넌트의 프로퍼티 `Surface Channels`(`*.surfacechannels.xml`)가 있으면 컴포넌트가 `CharacterSurfaceState` 를 쥐고(`getSurfaceState` — 게임플레이가 영역 값을 정하고 `stampHit` 으로 맞은 자리를 찍는다) 틱에서 감쇠시킨다.
  상태 번호(`getRevision`)가 바뀐 프레임에만 `CharacterSurfaceBinding` 이 부품의 머티리얼 인스턴스(염색과 같은 인스턴스)에 올린다 — 채널 스칼라 `Surface_<채널>`(모든 영역의 최댓값), 영역 벡터 `SurfaceRegion<영역>_<덩어리>`(채널 순서로 네 개씩, 영역은 피팅 표의 영역 순서),
  마스크 CPU 텍스처 `Surface_<채널>_Mask`(`TextureCache::createCPUTexture` 의 `cpu/surface/…`, R 채널, 바뀌면 같은 텍스처의 픽셀만 갈아 끼운다). 부품 칸이 늘면 상태를 다시 만든다(장비를 줄이는 것은 값을 지킨다). 이름을 읽어 그리는 쪽은 E6 이다.

## E3 가 한 것

- **아틀라스 굽기**: 엔진의 순수 계산 셋 — 배치 `OutfitAtlasLayout`(선반 채우기, 번짐 폭 포함, 결정적), 병합 훅 `OutfitAtlasHooks`(배치에 있는 부품을 한 그룹으로 모으고 UV 를 아틀라스 안의 자리로 옮긴다, 0..1 밖 UV 는 세어 알린다), 합성 `OutfitAtlasComposer`(텍스처를 자리에 복사하고 가장자리 번짐).
  에디터 `OutfitAtlasBaker` 가 명세 `*.outfitatlas.xml`(부품 메시 · 텍스처 종류별 부품 이미지 · 출력 경로)을 읽어 병합 메시(`.mesh`, 구간 하나)와 아틀라스 PNG 를 쓴다 — 합성을 메모리에서 끝낸 뒤에야 쓰고, 0..1 밖 UV · 크기가 다른 텍스처 · 아틀라스에 안 앉는 부품은 아무것도 쓰지 않고 멈춘다.
  개발 명령 `appearance.bakeatlas <명세>`. 결과 PNG 는 원본 이미지 폴더에 쓰므로 `--import-textures` 가 보통 텍스처처럼 임포트하고, 결과 메시를 그리는 머티리얼은 사람이 만든다(아틀라스 텍스처를 가리킨다).

## E5 가 한 것

- **시퀀서 외형 항목**: 엔진 트랙 종류 `SequenceTrackKind` 가 GameFramework 를 모르는 문제는, 카메라 컷을 `CinematicCameraComponent` 가 받는 것과 같은 나눔으로 풀었다. 항목 종류 표에 `SequenceItemKind::Appearance` 를 더했다(시작 프레임을 지날 때 이벤트로 알림 `_bFiresOnCross`, 대상 · 프리셋 이름이 비면 읽기 오류 `_bNeedsPreset`).
  GameFramework 의 `SequenceAppearanceComponent`(시퀀스 플레이어와 같은 오브젝트)가 이벤트를 받아 항목의 대상 오브젝트의 `CharacterAppearanceComponent::setPreset( _preset, 대상의 시드 )` 를 부른다 — 같은 캐릭터가 프리셋만 갈아입는다. 대상이 없거나 외형 컴포넌트가 없으면 오류를 남기고 아무것도 바꾸지 않는다.

## 남은 것

- 확인(통합에서 확인): 매니페스트 id `appearance-preload-test`(체형 축 `bodyShapes` 읽기 · 미리 올리기), 매니페스트 id `editor-charactereditor-scenario`(슬라이더 · 피팅 확인 · 썸네일 · 기즈모 · 리그 화면).
- E1 확인: 빌드 뒤 `TestAppearance` 필터와 Shooter3D 캐릭터 편집 시나리오. 떼어 낸 부품 오브젝트의 수명(잔해 정리)은 게임이 정한다 — 아직 정하는 곳이 없다.
- E2 · E3 · E5 확인(통합에서 확인): 매니페스트 id `mesh-merge-test`(`CharacterMeshUtilTest` — 병합 · 훅 UV), 매니페스트 id `surface-binding-test`(`CharacterSurfaceBindingTest` — 이름 규칙 · 마스크 픽셀 · 상태 번호), 매니페스트 id `outfit-atlas-test`(`OutfitAtlasTest` — 배치 · 훅 · 합성),
  매니페스트 id `outfit-atlas-baker-test`(`OutfitAtlasBakerTest` — 명세 읽기), 매니페스트 id `sequence-appearance-test`(`SequenceAppearanceTest` — 외형 항목이 프리셋을 바꿈), 매니페스트 id `sequencer-test`(`SequencerTest` — 외형 항목 이벤트 · 읽기 오류).
  화면: 매니페스트 id `appearance-merge-parts-check`(합칠 스킨드 부품이 든 차림을 만들어 `Merge Parts` 를 켜고 `Appearance.MergedSections` 와 사진으로 부품이 제 머티리얼로 그려지는지 본다 — 그 차림의 데이터가 아직 없다), 매니페스트 id `outfit-atlas-bake-run`(실제 장비 부품으로 명세를 만들어 `appearance.bakeatlas` 를 돌린다 — 명세와 부품 텍스처가 사람 일이다).
- **E6(표면 셰이더)**: 이름 규칙대로 읽어 그리는 머티리얼 셰이더(`engine/shaders/` 의 새 파일 + `engine/materials/` 의 새 머티리얼)가 남았다. 새 셰이더는 네 백엔드의 쿠킹된 바이너리(`engine/shaders/bin/`)가 같이 들어가야 하는데, 이 일은 빌드로만 만들 수 있고 이 단계는 빌드를 하지 않는다 — 병합 뒤 통합에서
  셰이더를 쓰고 쿠킹한다. 정점 영역 칸(영역 벡터를 영역마다 고르는 정점 속성)은 지금 영역 값이 캐릭터 전체 스칼라(`Surface_<채널>`)와 영역 벡터로만 나가므로, 부위별로 다르게 그리려면 셰이더가 읽을 영역 번호 칸(정점 색 알파나 UV 마스크)을 정해야 한다 — 그 선택도 E6 이다.
  확인: 매니페스트 id `surface-shader-check`.
- 에셋 핫 리로드(옛 E4b)는 들어갔다 — 소켓은 엔진 공유 캐시(`SharedAssetCache<SocketSet>`)를 쥐고, 원본 메시 · 피팅 표 · 피팅 데이터는 다시 읽은 횟수 · 내용 번호로 다시 조립한다(`CharacterAppearanceComponent::onTick`). 체형 축 파일(`bodyShapes`)은 읽는다. 표면 채널 파일은 `Surface Channels` 프로퍼티로 읽는다(고치면 프로퍼티를 다시 정해야 반영). 레퍼런스 포즈 파일은 외형 조립이 읽지 않는다.
- 애니메이션 시스템이 본이 움직인 프레임에만 `SocketBindingComponent::updateSocketTransform` 를 부르는 일은 단계 작업(`CharacterAppearanceSocketTask`)이 이미 한다.
- 나중: 천 시뮬레이션(Jolt 소프트 바디)이 같은 겹 정보를 충돌체로(Mutable 의 Clip with Mesh · Clip Deform).
- 함정: 칸 점유(양손 무기)는 아직 외형만 본다 — 게임플레이에서 보조 손을 막을지는 게임이 정한다. 참고: 언리얼 Mutable · Skin Weights 전이, Character Creator 스마트 핏, Daz 오토핏.
