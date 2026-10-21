# 게임플레이 후보

> 상태: 후보 · 선행: 없음 · 영역: gameplay · 옛번호: D4-2,D4-4,D4-6,D4-7,D4-8,D4-9,D4-10,D4-11,D4-12,D4-13,D4-14,D4-15,D4-16,D4-17,G-4,G-6,G-7 · 진행: 해당없음

GameFramework 키트와 게임, 캐릭터, 카메라, 애니메이션 영역의 착수 전 후보 목록입니다. 착수하면 그 절을 계획 문서로 옮기고 여기서 지웁니다.
이미 계획 문서가 있는 일의 남은 것은 그 문서에 있습니다: [PairedAnimation](PairedAnimation.md) · [EditorCharacterEditor](EditorCharacterEditor.md) · [MotionMatching](MotionMatching.md) · [CharacterAppearanceIntegration](CharacterAppearanceIntegration.md) · [AbilitySystemExtensions](AbilitySystemExtensions.md) · [IntentNetworkWiring](IntentNetworkWiring.md) · [MovingGroundFollowUps](MovingGroundFollowUps.md) · [InteractionHighlightAndMontage](InteractionHighlightAndMontage.md) · [WildWestHorseMount](WildWestHorseMount.md) · [GameSceneCc0Meshes](GameSceneCc0Meshes.md) · [AnimationLodAndCrowd](AnimationLodAndCrowd.md).

## 기능

### FeatureQueue
제목: 남은 기능 대기열(빠른 순)
옛번호: D4-9
왜 필요한가: 2026-10-04 사용자 지시로 빠른 순으로 둔 기능 대기열. 진행 방식은 [작업 방식](../11_Workflow.md) — 결함 먼저 · 겹치는 파일 없게 나누고, 병합은 준비되는 대로, 전체 검증은 큰 묶음 뒤 한 번.
착수 조건 / 남은 것: 착수하는 것부터 이름 있는 절로 떼어 낸다. **2D · 3D 에 다 쓰이는 기능은 공통 코어로**(사용자 지시). 다른 절에 있는 것(메시 LOD · PSO 미리 만들기 · 밉 스트리밍은 PreloadAndLod, 카메라는 CameraRendererIntegration, 리플레이 · 킬캠은 IntentNetworkWiring, 하늘 · 높이 안개 옛 D2-5, 천 · 머리카락은 PoweredRagdoll, DPI 실물 확인 옛 B-8)은 여기 다시 적지 않는다.
- 중간(M): 대역폭 프로파일러 · 게임플레이 디버거 · 비주얼 로거 · 모듈 패키지 관리(옛 D3-8) · HZB 가림 컬링 · 로딩 흐름의 남은 것(로딩 화면 · 페이드는 `LoadingScreenController` — 진행률(씬 매니저에 진행 질의가 없다 — 스트리밍 큐 바이트로) · 프리로드 세트 연결 · 팁 목록 데이터(`LoadingScreenSettings::_listTip` 은 있으나 gamesettings 칸 · 글 수집 규칙이 없다)) · 에셋 DCC 내보내기 · SSR · 업스케일러 · HDR 출력 · 데칼 · 2D 스켈레탈 · 학습용 몫(장르 시작 템플릿 · 튜토리얼 · API 문서).
- 큼(L): 제약 · 파티클/VFX · 에셋 레지스트리 · DDC · 증분 쿠킹 · 월드 편집 도구 · 전술 AI · 볼류메트릭 안개/빛/구름 · 캐릭터 셰이딩 · 모션 캡처 공정 · 대규모 좌표 · PCG 저작 그래프 · GI/반사 프로브 · 플랫폼 서비스 · 패치/DLC · 모드/UGC.
- 아주 큼(XL): 비주얼 스크립팅 · 월드 파티션/스트리밍/HLOD · 음성 채팅(온라인 구성은 옛 D7-5).

### PhotoModePauseUpdate
제목: 포토 모드 갱신을 씬 틱 밖으로
옛번호: D4-14
왜 필요한가: 에디터 Simulate 중 Pause 는 씬 틱이 멈춰 포토 모드(`Source/GameFramework/Base/Actor/Camera/PhotoModeCamera` · `CameraDirectorComponent`)가 갱신되지 않는다.
착수 조건 / 남은 것: 게임 카메라가 없는 상황이라 고치지 않았다 — 포토 모드 갱신을 씬 틱 밖으로 옮길지는 에디터 쪽 요구가 생기면.
설계 메모: 포토 모드를 켤 때 롤은 이어받고 기울이기 입력을 둔다(`PhotoModeCamera.h` 에 이미 있다). 조작은 `EngineDefaultAssets::_photoModeInputMap` 에서 읽어 게임 모듈에 읽기 전용으로 보인다(`CameraDirectorComponent.h` 에 이미 있다).

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
