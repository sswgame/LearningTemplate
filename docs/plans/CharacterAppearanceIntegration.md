# 캐릭터 외형의 통합 · 쿠킹 · 장비 잇기

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: D4-8 · 진행: 3/6

형상 쪽(`Source/Engine/Character`, README "외형 조립 순서")과 GPU 모프 풀(`Mesh::setGPUMorphEnabled`), 장비 해석 데이터 · 해석기(`GameFramework/Base/Gameplay/Appearance`)와 외형 컴포넌트(`CharacterAppearanceComponent`)는 들어갔습니다. 이어 붙이는 일이 남았습니다.
스켈레톤 에셋(`.skeleton.json`)의 편집 창은 [EditorCharacterEditor](EditorCharacterEditor.md) 입니다.

## 1. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| E1 | 장비 해석 첫 조각 — 외형 컴포넌트가 해석을 `CharacterAppearanceState` 하나로 한다(`setEquipment` 로 빌린 `Equipment` 의 슬롯이 프리셋을 덮음, 버전이 그대로이면 다시 해석하지 않음). 지난 해석에 붙어 있다 떨어져 나간 소켓 부품은 지우지 않고 `SocketBindingComponent::release( Physics )`(충격 힌트가 시작 속도), 물리 바디가 없으면 알리고 지운다 | 코드, 빌드 · 테스트 미확인 |
| E2 | 통합 — 들어간 것: 메시 ↔ 형상 다리(`CharacterMeshUtil` — 위치로 잇기 · 구운 사본), 몸 부품의 `fitTables` · 스킨드 부품의 `fit` 데이터, 피팅 · 해석의 숨김 영역 · 체형 모프를 구운 사본으로(`AppearanceMeshAssembly`, 결정 5-8 "외형의 체형 모프와 장비 피팅은 …"), 유닛의 스킨 원본 바꾸기(`SkeletalMeshComponent::setSkinSourceMesh`), 빈 구간(`Mesh::setSections`). **남은 것**: 부품을 한 메시로 병합(`MeshMerger` → 구간 그리기 — 그리기 줄이기), 표면 상태(`CharacterSurfaceState`)를 머티리얼 파라미터 · 마스크 텍스처로(셰이더 · 정점 영역 칸), 본 비율(`_listBoneProportion` ↔ `BodyShapeSet` 데이터 연결), 피팅 데이터를 가진 장비 에셋으로 화면 확인 | 코드 · 테스트(`CharacterMeshUtilTest` · `AppearanceMeshAssemblyTest`), 화면 미확인 |
| E3 | 쿠킹 — 들어간 것: 임포트 규칙 `body_surface` · `body_surface_distance` 가 장비 메시에 몸의 스킨 · 체형 모프를 옮긴다(`ModelImporter::transferBodySurface` → `CharacterMeshUtil::transferBodySurface`, 먼 정점은 임포트 오류). **남은 것**: 아틀라스 굽기(`IMeshMergeHooks` 구현 — E2 병합이 선행) | 코드 · 테스트(`CharacterMeshUtilTest.BodySurfaceTransfersSkinAndMorphsToAGarment`), 실제 장비 임포트 미확인 |
| E4 | 데이터 핫 리로드 — `AppearanceDataWatcher` 가 외형 폴더(낱개 파일)를 감시하고 `AppearanceDatabase::reloadFromFolder`(임시로 먼저 읽어 검사를 통과할 때만 바꾼다)를 부르면, 구독한 외형 컴포넌트(`registerReloaded`)가 다시 조립한다(Shooter3D 가 건다) | 코드, 빌드 · 실행 미확인 |
| E5 | 장비 해석의 나머지 — 들어간 것: 부품 풀(`_partPoolSize` — 벗은 소켓 부품을 숨겨 두고 같은 에셋이면 그 인스턴스, 다른 칸 · 소켓으로 옮겨 가는 무기도 같은 인스턴스), 결과 키로 구운 메시 캐시(`AppearanceBakedMeshCache`, 다시 조립하는 동안 이전 외형 유지), 몸 영역 이름을 피팅 표에서(`AppearanceDatabase::loadFromFolder` → `finishLoad`), 프리셋 참조를 따라 미리 올릴 경로(`AppearancePreloadUtil`). **남은 것**: 시퀀서 트랙이 프리셋을 이름으로(엔진 트랙 종류 `SequenceTrackKind` 는 GameFramework 를 모른다 — 확장 자리가 먼저), 편집 창의 "지금 모습을 프리셋으로 저장" · 썸네일 · 규칙 설명(`_listTrace`, [EditorCharacterEditor](EditorCharacterEditor.md) 쪽) | 일부 코드 · 테스트 |

## 남은 것

- E1 확인: 빌드 뒤 `TestAppearance` 필터와 Shooter3D 캐릭터 편집 시나리오. 떼어 낸 부품 오브젝트의 수명(잔해 정리)은 게임이 정한다 — 아직 정하는 곳이 없다.
- 에셋 핫 리로드(옛 E4b)는 들어갔다 — 소켓은 엔진 공유 캐시(`SharedAssetCache<SocketSet>`)를 쥐고, 원본 메시 · 피팅 표 · 피팅 데이터는 다시 읽은 횟수 · 내용 번호로 다시 조립한다(`CharacterAppearanceComponent::onTick`). 체형 · 표면 채널 · 레퍼런스 포즈 파일은 외형 조립이 아직 읽지 않는다(E2 남은 것과 같이 붙는다).
- 애니메이션 시스템이 본이 움직인 프레임에만 `SocketBindingComponent::updateSocketTransform` 를 부르는 일은 단계 작업(`CharacterAppearanceSocketTask`)이 이미 한다.
- 나중: 천 시뮬레이션(Jolt 소프트 바디)이 같은 겹 정보를 충돌체로(Mutable 의 Clip with Mesh · Clip Deform).
- 함정: 칸 점유(양손 무기)는 아직 외형만 본다 — 게임플레이에서 보조 손을 막을지는 게임이 정한다. 참고: 언리얼 Mutable · Skin Weights 전이, Character Creator 스마트 핏, Daz 오토핏.
