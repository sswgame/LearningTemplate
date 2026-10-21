# 게임플레이 후보

> 상태: 후보 · 선행: 없음 · 영역: gameplay · 옛번호: D4-2,D4-4,D4-6,D4-7,D4-8,D4-9,D4-10,D4-11,D4-12,D4-13,D4-14,D4-15,D4-16,D4-17,G-4,G-6,G-7 · 진행: 해당없음

GameFramework 키트와 게임, 캐릭터, 카메라, 애니메이션 영역의 착수 전 후보 목록입니다. 착수하면 그 절을 계획 문서로 옮기고 여기서 지웁니다.
이미 계획 문서가 있는 일의 남은 것은 그 문서에 있습니다: [PairedAnimation](PairedAnimation.md) · [EditorCharacterEditor](EditorCharacterEditor.md) · [MotionMatching](MotionMatching.md).

## 기능

### CharacterAppearanceIntegration
제목: 캐릭터 외형의 통합 · 쿠킹 · 장비 잇기
옛번호: D4-8
왜 필요한가: 형상 쪽(`Source/Engine/Character`, README "외형 조립 순서")과 GPU 모프 풀(`Mesh::setGPUMorphEnabled`), 장비 해석 데이터 · 해석기(`GameFramework/Base/Gameplay/Appearance`)와 외형 컴포넌트(`CharacterAppearanceComponent`)는 들어갔다. 이어 붙이는 일이 남았다. 스켈레톤 에셋(`.skeleton.json`)은 소켓 파일을 처음 만들 근거(임포트가 적은 본 부착 메시 표)뿐이고 편집 창구가 없다(편집 창은 [EditorCharacterEditor](EditorCharacterEditor.md)).
남은 것:
- 통합: `Mesh` · 포즈 ↔ `AppearanceGeometry` · `CharacterBoneArray` 변환, 체형 모프 · 피팅 델타(`FitPartResult::_listVertexDelta`)를 GPU 모프 풀에 싣기(스키닝 앞), 병합 결과(`MeshMerger`)를 인덱스 · 정점 버퍼와 구간 그리기로, 애니메이션 시스템이 본이 움직인 프레임에만 `SocketBindingComponent::updateSocketTransform`, 표면 상태(`CharacterSurfaceState`)를 머티리얼 파라미터 · 마스크 텍스처로.
- 쿠킹: 장비 정점 → 몸 전이(`SurfaceTransferUtil` — 모프 · 스킨 가중치)를 임포트 · 쿠킹 단계에, 아틀라스 굽기(`IMeshMergeHooks` 구현).
- 핫 리로드: 소켓 · 레퍼런스 포즈 · 체형 · 피팅 표 · 부품 피팅 · 표면 채널 파일을 고치면 외형을 다시 조립 — 소비자(외형 컴포넌트)가 `IAssetCache` 로 올린다(로더는 다 있다). 데이터 파일 감시 → `AppearanceDatabase` 다시 읽기.
- 장비 해석: 장비(`Equipment`)를 `CharacterAppearanceState` 로 잇기, 부품 풀(스폰 대신 숨겨 둔 인스턴스), 외형 상태가 바뀌면 같은 인스턴스를 다른 소켓으로, 떨어져 나감 이벤트를 `SocketBindingComponent` ReleasedPhysics 로, 결과 해시로 병합 결과를 캐시해 같은 차림의 NPC 가 나눠 쓰고 다시 조립하는 동안 이전 외형 유지(비동기), `finishLoad` 에 몸 영역 표 이름 넘기기, 시퀀서 트랙 · 스폰 · 프리팹이 프리셋을 이름으로 가리키기 · 프리로드 세트가 프리셋 참조를 따라 모으기, 편집 창의 "지금 모습을 프리셋으로 저장" · 썸네일 렌더 · 규칙 설명(`_listTrace`) 표시.
- 나중: 천 시뮬레이션(Jolt 소프트 바디)이 같은 겹 정보를 충돌체로(Mutable 의 Clip with Mesh · Clip Deform 이 같은 문제를 푼다).
함정: 칸 점유(양손 무기)는 아직 외형만 본다 — 게임플레이에서 보조 손을 막을지는 게임이 정한다. 참고: 언리얼 Mutable(Customizable Object) · Skin Weights 전이, Character Creator 스마트 핏, Daz 오토핏.

### FeatureQueue
제목: 남은 기능 대기열(빠른 순)
옛번호: D4-9
왜 필요한가: 2026-10-04 사용자 지시로 빠른 순으로 둔 기능 대기열. 진행 방식은 [작업 방식](../11_Workflow.md) — 결함 먼저 · 겹치는 파일 없게 나누고, 병합은 준비되는 대로, 전체 검증은 큰 묶음 뒤 한 번.
착수 조건 / 남은 것: 착수하는 것부터 이름 있는 절로 떼어 낸다. **2D · 3D 에 다 쓰이는 기능은 공통 코어로**(사용자 지시). 다른 절에 있는 것(메시 LOD · PSO 미리 만들기 · 밉 스트리밍은 PreloadAndLod, 카메라는 CameraRendererIntegration, 리플레이 · 킬캠은 IntentNetworkWiring, 하늘 · 높이 안개 옛 D2-5, 천 · 머리카락은 PoweredRagdoll, DPI 실물 확인 옛 B-8)은 여기 다시 적지 않는다.
- 중간(M): 대역폭 프로파일러 · 게임플레이 디버거 · 비주얼 로거 · 모듈 패키지 관리(옛 D3-8) · HZB 가림 컬링 · 로딩 흐름의 남은 것(로딩 화면 · 페이드는 `LoadingScreenController` — 진행률(씬 매니저에 진행 질의가 없다 — 스트리밍 큐 바이트로) · 프리로드 세트 연결 · 팁 목록 데이터(`LoadingScreenSettings::_listTip` 은 있으나 gamesettings 칸 · 글 수집 규칙이 없다)) · 에셋 DCC 내보내기 · SSR · 업스케일러 · HDR 출력 · 데칼 · 2D 스켈레탈 · 학습용 몫(장르 시작 템플릿 · 튜토리얼 · API 문서).
- 큼(L): 제약 · 파티클/VFX · 에셋 레지스트리 · DDC · 증분 쿠킹 · 월드 편집 도구 · 전술 AI · 볼류메트릭 안개/빛/구름 · 캐릭터 셰이딩 · 모션 캡처 공정 · 대규모 좌표 · PCG 저작 그래프 · GI/반사 프로브 · 플랫폼 서비스 · 패치/DLC · 모드/UGC.
- 아주 큼(XL): 비주얼 스크립팅 · 월드 파티션/스트리밍/HLOD · 음성 채팅(온라인 구성은 옛 D7-5).

### AbilitySystemExtensions
제목: 어빌리티 시스템의 다음 조각
옛번호: D4-11
왜 필요한가: 언리얼 GAS 에 있고 여기 없는 조각이다. 에디터의 런타임 상태 패널(`AbilityDebugPanel` — `GF_EditorPlugIn_Base`, `ability_debugger`, 읽기 전용, 플레이 중에만 값이 있다)은 들어갔다.
착수 조건 / 남은 것: 쓰는 게임이 생기면. 이펙트가 주는 어빌리티(장비가 스킬을 준다), 걸린 동안의 태그 조건(`OngoingTagRequirements` — 기절 중 버프 정지), 태그가 붙을 때 발동(`OwnedTagAdded` 트리거), 큐를 데이터로 이어 주는 큐 매니저(큐 태그 → 프리팹 · 사운드), 어트리뷰트를 `SaveGame` 에 싣는 도우미.
함정: 넣을 때마다 `AbilitySystemTest` 에 테스트 하나. 무적 API 는 없다(옛 D3-4).

### InteractionHighlightAndMontage
제목: 상호작용 강조 패스와 몽타주 실행 확인
옛번호: D4-12
왜 필요한가: 상호작용 · 기믹(`GameFramework/Base/Gameplay/Interaction` · `Gimmick` · `Spline` README)의 남은 두 조각이다.
남은 것:
- 렌더러: `InteractableComponent::getHighlightRequest`(Outline · Sense)를 읽는 외곽선 · 감각 모드 패스.
- 실행 확인: 상호작용 단계 몽타주가 애니메이터를 실제로 부르는지(그래프 에셋이 있는 씬). 3D 기믹 변형의 겹침은 `GimmickGenreTest.Gimmick3DTriggerVariantsReceiveOverlapsInA3DScene` 이 본다.

### IntentNetworkWiring
제목: 의도 기록 · 원격 조종자의 남은 연결
옛번호: D4-13
왜 필요한가: 입력 의도를 기록 · 재생하고 원격 폰을 조종하는 길이 서버 쪽에서 아직 이어지지 않았다. 재생 진입점(`IntentPlayback::start` · `startFromFile`)은 있다.
남은 것: `NetClientServer` 의 입력 바이트(`ReplicationClient::sendInput` 이 게임이 준 바이트를 싣는 자리)를 `ControlIntent::write` 로, 서버 폰은 `RemoteControllerComponent` 로 — 폰을 쓰는 넷 테스트 게임이 생기면(지금 0). 의도 기록을 켜고 `.swintent` 를 쓰는 명령줄 · 에디터 단추와 재생 UI · 킬캠 카메라 · 되감기는 아직 없다(호출하는 쪽의 일).

### PhotoModePauseUpdate
제목: 포토 모드 갱신을 씬 틱 밖으로
옛번호: D4-14
왜 필요한가: 에디터 Simulate 중 Pause 는 씬 틱이 멈춰 포토 모드(`Source/GameFramework/Base/Actor/Camera/PhotoModeCamera` · `CameraDirectorComponent`)가 갱신되지 않는다.
착수 조건 / 남은 것: 게임 카메라가 없는 상황이라 고치지 않았다 — 포토 모드 갱신을 씬 틱 밖으로 옮길지는 에디터 쪽 요구가 생기면.
설계 메모: 포토 모드를 켤 때 롤은 이어받고 기울이기 입력을 둔다(`PhotoModeCamera.h` 에 이미 있다). 조작은 `EngineDefaultAssets::_photoModeInputMap` 에서 읽어 게임 모듈에 읽기 전용으로 보인다(`CameraDirectorComponent.h` 에 이미 있다).

### WildWestHorseMount
제목: WildWest 승마의 말 모델
옛번호: D4-15
왜 필요한가: 말 모델이 저장소에 없어 말과 기수가 큐브 · 캡슐 자리 표시다(`Source/Games/WildWest`, 2026-10-16).
남은 것: 네발 말 glTF(걸음새 클립 넷: 걷기 · 속보 · 구보 · 질주, 안장 소켓)를 `Resource/game/wildwest/models_raw/` 에 들이고 `MountMovementComponent` 의 `_gaitParameter` · 좌석 `_socketName` 을 잇는다.
함정: KayKit · Kenney 에 말이 없다.

### GameSceneCc0Meshes
제목: 게임별 씬이 새로 들인 CC0 메시를 부르게 한다
옛번호: D4-17
왜 필요한가: 2026-10-17 `res3` 가 여섯 게임에 CC0 모델 팩(KayKit Dungeon · Space Base, Kenney Nature · Fantasy Town)의 원본을 배치했지만 씬 · 프리팹 · 코드는 아직 자리 표시 큐브를 쓴다.
남은 것: 게임마다 씬이 새 메시를 부르게 바꿔 에셋 검증 `orphans` 경고를 없앤다. 배치 목록은 `ModelImportConfig.json` 과 게임별 `credits.md`.

### MovingGroundFollowUps
제목: 움직이는 바닥 위 캐릭터의 남은 일
옛번호: 없음
왜 필요한가: `moveCharacter` 가 딛은 바닥의 속도(선속도 + 회전)를 실어 주게 됐지만(2026-10-20, 결정은 `IPhysicsCharacterScene.h` 머리말과 `JoltPhysicsSceneQuery.cpp`), 아래 셋은 하지 않았다.
남은 것: (1) 바닥이 돌 때 캐릭터의 방향(yaw)이 따라 돌지 않는다 — 위치만 돈다. (2) 점프로 바닥을 떠난 뒤의 관성이 없다 — 바닥 속도는 딛고 있는 스텝에만 얹히므로, 컴포넌트가 점프 순간의 바닥 속도를 `_airVelocity` 로 이어야 한다. (3) 네트워크 틱 동기: 서버와 클라이언트가 바닥 위치를 같은 틱에 맞춰야 캐릭터 예측이 어긋나지 않는다.
함정: 큰 Kinematic 메시(공중 요새)는 캐릭터 쓸기 충돌 비용이 크다 — 측정 전에 단순 상자로 바닥을 대신할 수 있는지 먼저 본다.

## 조건부

### InteractionSpatialGrid
제목: 상호작용 대상의 공유 공간 격자
옛번호: G-4
왜 필요한가: 지금 하는 쪽은 등록된 대상 목록을 한 번 돈다(대상 16 · 10k 오브젝트에서 +2~6 us).
착수 조건 / 남은 것: 하는 쪽 · 대상이 모두 수천일 때. 대상이 움직일 수 있어 격자를 프레임마다 맞추는 비용이 목록을 도는 비용과 같으므로, 하는 쪽이 여럿일 때만 씬당 하나(`SpatialHashGrid2D`, 셀 = 최대 상호작용 반경)를 틱 앞에 맞춘다.

### TilemapCellDataLayers
제목: 타일맵 칸 데이터를 일반 레이어로
옛번호: G-6
왜 필요한가: 지금 레이어는 0/1(`kArrTileFlagLayerInfo`)이고 워프 · 역할 · 스폰은 전용 원소다.
착수 조건 / 남은 것: 두 번째 장르가 칸마다 다른 값(지형 비용 · 발소리)을 원하면. 값 종류를 정수로 넓히고 에디터 페인트 · 형식 테스트를 같이 바꾼다(Godot TileSet custom data 모양).

### StepEncounterRules
제목: 걸음 조우 판정 둘
옛번호: G-7
왜 필요한가: Overworld `shouldEncounterOnStep` 의 결정적 주기와 ClassicJRPG `JRPGEncounterWalker` 의 확률 + 유예가 따로 있다.
착수 조건 / 남은 것: 오버월드 위에 JRPG · 몬스터 수집 게임이 서면 기반 `World/` 로 하나를 올린다.
