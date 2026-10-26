# 캐릭터 외형의 통합 · 쿠킹 · 장비 잇기

> 상태: 측정대기 · 선행: 없음 · 영역: gameplay · 옛번호: D4-8 · 진행: 6/6

형상 쪽(`Source/Engine/Character`, README "외형 조립 순서")과 GPU 모프 풀(`Mesh::setGPUMorphEnabled`), 장비 해석 데이터 · 해석기(`GameFramework/Base/Gameplay/Appearance`)와 외형 컴포넌트(`CharacterAppearanceComponent`)는 들어갔고, 아래 E1 ~ E6 은 코드가 들어갔습니다(빌드 · 화면 미확인). 스켈레톤 에셋 편집 창은 [EditorCharacterEditor](EditorCharacterEditor.md).

## 1. 단위

| 단위 | 내용 |
|---|---|
| E1 | 장비 해석 — 외형 컴포넌트가 해석을 `CharacterAppearanceState` 하나로 한다(`setEquipment` 로 빌린 `Equipment` 의 슬롯이 프리셋을 덮음, 버전이 그대로이면 다시 해석하지 않음). 떨어져 나간 소켓 부품은 지우지 않고 `SocketBindingComponent::release( Physics )`(충격 힌트가 시작 속도), 물리 바디가 없으면 알리고 지운다 |
| E2 | 통합 — 메시 ↔ 형상 다리(`CharacterMeshUtil`), 몸 부품의 `fitTables` · 스킨드 부품의 `fit` 데이터, 피팅 · 해석의 숨김 영역 · 체형 모프를 구운 사본으로(`AppearanceMeshAssembly`, 결정 5-8), 유닛의 스킨 원본 바꾸기(`SkeletalMeshComponent::setSkinSourceMesh`), 빈 구간(`Mesh::setSections`), 본 비율 연결(몸 부품의 `bodyShapes`(`*.bodyshape.xml`)가 축을 정하고 `BoneProportion` 드라이브의 값이 축 값, `BodyShapeSet::evaluate` 로 풀어 몸 포즈 위에 겹침 — `CharacterAppearanceProportionTask`는 비율이 있을 때만 등록해 군중 공유를 지킨다), 부품 병합, 표면 상태 → 머티리얼. 체형 축 파일 + 몸 부품 `bodyShapes` 한 줄은 게임 데이터 일 |
| E3 | 쿠킹 — 임포트 규칙 `body_surface` · `body_surface_distance` 가 장비 메시에 몸의 스킨 · 체형 모프를 옮긴다(`ModelImporter::transferBodySurface` → `CharacterMeshUtil::transferBodySurface`, 먼 정점은 임포트 오류). 아틀라스 굽기 |
| E4 | 데이터 핫 리로드 — `AppearanceDataWatcher` 가 외형 폴더를 감시하고 `AppearanceDatabase::reloadFromFolder`(임시로 먼저 읽어 검사를 통과할 때만 바꾼다)를 부르면 구독한 외형 컴포넌트(`registerReloaded`)가 다시 조립(Shooter3D 가 건다). 소켓은 `SharedAssetCache<SocketSet>`, 원본 메시 · 피팅 표 · 피팅 데이터는 다시 읽은 횟수 · 내용 번호로 다시 조립. 표면 채널 파일은 프로퍼티를 다시 정해야 반영, 레퍼런스 포즈 파일은 외형 조립이 읽지 않는다 |
| E5 | 장비 해석의 나머지 — 부품 풀(`SocketPartPool` 하나로 합침, [ShooterCharacterIntegration](ShooterCharacterIntegration.md) S6), 결과 키로 구운 메시 캐시(`AppearanceBakedMeshCache`, 다시 조립하는 동안 이전 외형 유지), 몸 영역 이름을 피팅 표에서, 프리셋 참조를 따라 미리 올릴 경로(`AppearancePreloadUtil`), 시퀀서 외형 항목 |
| E6 | 표면 셰이더 — 표면 상태 이름(`_surface<채널>` · `_surface<채널>Mask`)을 읽어 젖음 · 흙 · 피 · 상처 · 찢김을 그리는 `surfacelit.hlsl` + `surfacelit.material` |

## 2. 설계 결정 · 계약

- **부품 병합**: `CharacterMeshUtil::mergeMeshes` 가 같은 스켈레톤의 스킨드 부품(없는 부품은 스킨 없는 부품끼리)을 구간이 있는 메시 하나로 합친다 — 구간은 머티리얼 그룹마다 하나(병합 훅 `IMeshMergeHooks`가 고르고 훅이 없으면 부품 이름), 정점 · 스킨 · 모프 타깃(이름으로 합침)을 옮긴다. 이미 구간이 있거나 정점 애니메이션이 있는 부품, 스킨 유무 · 본 수가 섞인 부품은 거절. 외형 컴포넌트 프로퍼티 `Merge Parts`(기본 꺼짐)를 켜면 자기 리그가 없고 몸과 같은 스켈레톤인 스킨드 부품은 오브젝트를 만들지 않고 합쳐 몸 유닛에 걸며(`setSkinSourceMesh`), 부품마다 `MeshSectionComponent` 가 자기 머티리얼로 그려 스키닝 · 포즈가 한 번이다. 합치지 못하면 오류를 남기고 몸 원본만 그린다(폴백 금지). 한계: 합친 메시는 LOD 를 갖지 못하고, 합칠 스킨드 부품이 든 차림 데이터가 아직 없다(Shooter3D 의 스킨드 부품은 자기 리그가 있는 망토뿐). 탐침 `Appearance.MergedSections`.
- **표면 상태 → 머티리얼**: 프로퍼티 `Surface Channels`(`*.surfacechannels.xml`)가 있으면 컴포넌트가 `CharacterSurfaceState` 를 쥐고(`getSurfaceState`, `stampHit` 으로 맞은 자리를 찍음) 틱에서 감쇠시킨다. 상태 번호가 바뀐 프레임에만 `CharacterSurfaceBinding` 이 부품의 머티리얼 인스턴스에 `_surface<채널>`(영역 최댓값), `_surfaceRegion<영역>Chunk<덩어리>`(영역 벡터), 마스크 CPU 텍스처 `_surface<채널>Mask`(`TextureCache::createCPUTexture` 의 `cpu/surface/…`, R 채널, 픽셀만 갈아 끼움)를 올린다. 이름은 셰이더 필드 규칙(`_camelCase`, NamingScripts "Struct fields")에 맞춰 바꿨다(`CharacterSurfaceBindingTest.ParameterNamesFollowTheContract`).
- **아틀라스 굽기**: 엔진의 순수 계산 셋 — 배치 `OutfitAtlasLayout`(선반 채우기, 번짐 폭, 결정적), 병합 훅 `OutfitAtlasHooks`(0..1 밖 UV 는 세어 알림), 합성 `OutfitAtlasComposer`. 에디터 `OutfitAtlasBaker` 가 명세 `*.outfitatlas.xml` 을 읽어 병합 메시와 아틀라스 PNG 를 쓰며 합성을 메모리에서 끝낸 뒤에만 쓰고 0..1 밖 UV · 크기가 다른 텍스처 · 아틀라스에 안 앉는 부품은 아무것도 쓰지 않고 멈춘다. 개발 명령 `appearance.bakeatlas <명세>`. 결과 PNG 는 `--import-textures` 가 임포트하고 결과 메시의 머티리얼은 사람이 만든다.
- **시퀀서 외형 항목**: 엔진 `SequenceTrackKind` 가 GameFramework 를 모르는 문제는 카메라 컷과 같은 나눔으로 풀었다. `SequenceItemKind::Appearance`(시작 프레임을 지날 때 이벤트 `_bFiresOnCross`, 대상 · 프리셋 이름이 비면 읽기 오류 `_bNeedsPreset`)를 더했고 `SequenceAppearanceComponent` 가 이벤트를 받아 대상의 `CharacterAppearanceComponent::setPreset` 을 부른다. 대상이 없으면 오류를 남기고 아무것도 바꾸지 않는다.
- **`surfacelit.hlsl`**: forwardlit 의 PBR(`Lighting::shadeLights`)에 얹는다. 채널 스칼라 `_surfaceWet · Dirt · Blood · Wound · Tear` 와 마스크 `_surfaceDirtMask · BloodMask · WoundMask · TearMask`(`default.surfacechannels.xml` 의 다섯 채널 — 다른 채널은 구조체와 머티리얼에 같은 이름을 더한다). 젖음은 몸 전체(스칼라)에 알베도 0.55 배 + 거칠기 낮춤, 흙 · 피 · 상처는 마스크가 자리를 정하는 덮개, 찢김은 마스크 자리를 알파 컷(머티리얼이 `MATERIAL_ALPHA_CUTOFF` 를 늘 켬 — 깊이 · 그림자 패스도 뚫림)으로 구멍 낸다. 마스크가 흰색으로 읽히는 인스턴스(표면 상태 없음 · bindless 에뮬 백엔드)에서 스칼라가 0 이면 효과가 없도록 스칼라로 문을 건다. **영역 번호 칸은 두지 않았다** — 부위별 표현은 마스크(`stampHit`)가 하므로 정점 속성을 새로 정하지 않아 임포트 형식이 그대로이고, 영역 벡터는 오르지만 이 셰이더는 읽지 않는다. 한계: 캐릭터 머티리얼(`kaykit_*`)은 아직 forwardlit 이라 게임에서 켜려면 그 머티리얼을 `surfacelit` 으로 바꾸고 `Surface Channels` 를 정하는 데이터 일이 따로이고, 알파 컷이라 켠 캐릭터 수만큼 프리패스 비용이 는다. 쇼케이스 `game/empty/maps/surfaceshowcase.scene.xml` + `automation/surfaceshowcase.scenario.xml`(색 기울기 단언은 `provisional` 이라 첫 실행은 UNCONFIRMED, 사진은 눈으로).

## 3. 남은 것

- 확인(통합에서 확인): 매니페스트 id `appearance-preload-test` · `editor-charactereditor-scenario`(슬라이더 · 피팅 확인 · 썸네일 · 기즈모 · 리그 화면).
- E2 · E3 · E5 확인(통합에서 확인): 매니페스트 id `mesh-merge-test` · `surface-binding-test` · `outfit-atlas-test` · `outfit-atlas-baker-test` · `sequence-appearance-test` · `sequencer-test`.
- 화면: 매니페스트 id `appearance-merge-parts-check`(합칠 스킨드 부품이 든 차림을 만들어 `Merge Parts` 를 켜고 `Appearance.MergedSections` 와 사진으로 확인 — 그 차림의 데이터가 아직 없다), 매니페스트 id `outfit-atlas-bake-run`(실제 장비 부품으로 명세를 만들어 `appearance.bakeatlas` 실행 — 명세와 부품 텍스처가 사람 일이다).
- E6 표면 셰이더: 쿠킹 바이너리(`engine/shaders/bin/`, surfacelit 744 개 · highlightoutline 32 개)는 통합 O 가 구웠다. 남은 확인: 매니페스트 id `surface-shader-check`(쇼케이스 시나리오 네 백엔드 + 사진, 문턱 `provisional` → `measured`).
- E1 확인: 빌드 뒤 `TestAppearance` 필터와 Shooter3D 캐릭터 편집 시나리오. 떼어 낸 부품 오브젝트의 수명(잔해 정리)은 게임이 정하는데 아직 정하는 곳이 없다.
- 나중: 천 시뮬레이션(Jolt 소프트 바디)이 같은 겹 정보를 충돌체로(Mutable 의 Clip with Mesh · Clip Deform).
- 함정: 칸 점유(양손 무기)는 아직 외형만 본다 — 게임플레이에서 보조 손을 막을지는 게임이 정한다.
