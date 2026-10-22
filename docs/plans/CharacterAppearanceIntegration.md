# 캐릭터 외형의 통합 · 쿠킹 · 장비 잇기

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: D4-8 · 진행: 2/6

형상 쪽(`Source/Engine/Character`, README "외형 조립 순서")과 GPU 모프 풀(`Mesh::setGPUMorphEnabled`), 장비 해석 데이터 · 해석기(`GameFramework/Base/Gameplay/Appearance`)와 외형 컴포넌트(`CharacterAppearanceComponent`)는 들어갔습니다. 이어 붙이는 일이 남았습니다.
스켈레톤 에셋(`.skeleton.xml`)의 편집 창은 [EditorCharacterEditor](EditorCharacterEditor.md) 입니다.

## 1. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| E1 | 장비 해석 첫 조각 — 외형 컴포넌트가 해석을 `CharacterAppearanceState` 하나로 한다(`setEquipment` 로 빌린 `Equipment` 의 슬롯이 프리셋을 덮음, 버전이 그대로이면 다시 해석하지 않음). 지난 해석에 붙어 있다 떨어져 나간 소켓 부품은 지우지 않고 `SocketBindingComponent::release( Physics )`(충격 힌트가 시작 속도), 물리 바디가 없으면 알리고 지운다 | 코드, 빌드 · 테스트 미확인 |
| E2 | 통합 — `Mesh` · 포즈 ↔ `AppearanceGeometry` · `CharacterBoneArray` 변환, 체형 모프 · 피팅 델타(`FitPartResult::_listVertexDelta`)를 GPU 모프 풀에(스키닝 앞), 병합 결과(`MeshMerger`)를 인덱스 · 정점 버퍼와 구간 그리기로, 표면 상태(`CharacterSurfaceState`)를 머티리얼 파라미터 · 마스크 텍스처로 | 남음(GPU) |
| E3 | 쿠킹 — 장비 정점 → 몸 전이(`SurfaceTransferUtil` — 모프 · 스킨 가중치)를 임포트 · 쿠킹 단계에, 아틀라스 굽기(`IMeshMergeHooks` 구현) | 남음(임포트) |
| E4 | 데이터 핫 리로드 — `AppearanceDataWatcher` 가 외형 폴더(낱개 파일)를 감시하고 `AppearanceDatabase::reloadFromFolder`(임시로 먼저 읽어 검사를 통과할 때만 바꾼다)를 부르면, 구독한 외형 컴포넌트(`registerReloaded`)가 다시 조립한다(Shooter3D 가 건다) | 코드, 빌드 · 실행 미확인 |
| E4b | 에셋 핫 리로드 — 소켓 · 레퍼런스 포즈 · 체형 · 피팅 표 · 부품 피팅 · 표면 채널 파일을 고치면 외형을 다시 조립(외형 컴포넌트가 그 캐시의 `IAssetCache` 다시 읽기 알림을 구독) | 남음 — 그 파일들을 읽는 캐시가 다시 읽기 알림을 내는지부터 본다 |
| E5 | 장비 해석의 나머지 — 부품 풀(스폰 대신 숨겨 둔 인스턴스), 같은 인스턴스를 다른 소켓으로, 결과 해시로 병합 결과 캐시(같은 차림 NPC 공유, 다시 조립하는 동안 이전 외형 유지), `finishLoad` 에 몸 영역 표 이름, 시퀀서 트랙 · 스폰 · 프리팹이 프리셋을 이름으로, 프리로드 세트가 프리셋 참조를 따라 모으기, 편집 창의 "지금 모습을 프리셋으로 저장" · 썸네일 · 규칙 설명(`_listTrace`) | 남음 |

## 남은 것

- E1 확인: 빌드 뒤 `TestAppearance` 필터와 Shooter3D 캐릭터 편집 시나리오. 떼어 낸 부품 오브젝트의 수명(잔해 정리)은 게임이 정한다 — 아직 정하는 곳이 없다.
- 애니메이션 시스템이 본이 움직인 프레임에만 `SocketBindingComponent::updateSocketTransform` 를 부르는 일은 단계 작업(`CharacterAppearanceSocketTask`)이 이미 한다.
- 나중: 천 시뮬레이션(Jolt 소프트 바디)이 같은 겹 정보를 충돌체로(Mutable 의 Clip with Mesh · Clip Deform).
- 함정: 칸 점유(양손 무기)는 아직 외형만 본다 — 게임플레이에서 보조 손을 막을지는 게임이 정한다. 참고: 언리얼 Mutable · Skin Weights 전이, Character Creator 스마트 핏, Daz 오토핏.
