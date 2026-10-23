# 게임플레이 후보

> 상태: 후보 · 선행: 없음 · 영역: gameplay · 옛번호: D4-8,D4-9,D4-10,D4-11,D4-12,D4-13,D4-14,D4-15,D4-17,G-4,G-6,G-7 · 진행: 해당없음

GameFramework 키트와 게임, 캐릭터, 카메라, 애니메이션 영역의 착수 전 후보 목록입니다. 착수하면 그 절을 계획 문서로 옮기고 여기서 지웁니다.
이미 계획 문서가 있는 일의 남은 것은 그 문서에 있습니다: [PairedAnimation](PairedAnimation.md) · [EditorCharacterEditor](EditorCharacterEditor.md) · [MotionMatching](MotionMatching.md) · [CharacterAppearanceIntegration](CharacterAppearanceIntegration.md) · [AbilitySystemExtensions](AbilitySystemExtensions.md) · [IntentNetworkWiring](IntentNetworkWiring.md) · [MovingGroundFollowUps](MovingGroundFollowUps.md) · [InteractionHighlightAndMontage](InteractionHighlightAndMontage.md) · [WildWestHorseMount](WildWestHorseMount.md) · [GameSceneCc0Meshes](GameSceneCc0Meshes.md) · [AnimationLodAndCrowd](AnimationLodAndCrowd.md).
남은 기능 대기열은 [FeatureQueue](FeatureQueue.md) 계획 문서로 옮겼다.

## 기능

### PhotoModePauseUpdate
제목: 포토 모드 갱신을 씬 틱 밖으로
옛번호: D4-14
왜 필요한가: 에디터 Simulate 중 Pause 는 씬 틱이 멈춰 포토 모드(`Source/GameFramework/Base/Actor/Camera/PhotoModeCamera` · `CameraDirectorComponent`)가 갱신되지 않는다.
착수 조건 / 남은 것: 게임 카메라가 없는 상황이라 고치지 않았다 — 포토 모드 갱신을 씬 틱 밖으로 옮길지는 에디터 쪽 요구가 생기면.
설계 메모: 포토 모드를 켤 때 롤은 이어받고 기울이기 입력을 둔다(`PhotoModeCamera.h` 에 이미 있다). 조작은 `EngineDefaultAssets::_photoModeInputMap` 에서 읽어 게임 모듈에 읽기 전용으로 보인다(`CameraDirectorComponent.h` 에 이미 있다).
못 하는 이유(2026-10-24): 에디터 쪽 요구(Simulate 중 Pause 에서 포토 모드를 쓰는 일)가 없고 무엇을 요구로 칠지 정해지지 않았다. 풀리는 조건: 에디터 Simulate 에서 Pause 중 포토 모드가 필요한 작업 흐름이 생길 때.
