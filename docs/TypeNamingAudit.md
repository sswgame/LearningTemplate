# TypeNamingAudit

> 질문: Source 의 형식 이름(클래스 · 구조체 · 컴포넌트 · 열거)이 상용 엔진의 같은 일 이름과 맞는가, 인터페이스 규칙(`I` + PascalCase)을 지키는가, 기믹 · 키트를 장르로 나눈 구조가 맞는가?
> 독자: 2 단계(전수 개명 · 이동)를 실행할 통합 담당과 작업자.
> 결론: 바꿈 판정은 컴포넌트 이름 22 · 인터페이스 20(`I` 붙임 5 · 뗌 15) · 그 밖의 형식 · 열거 16 · 층 이동 8 · 파일 · 폴더 재배치 16 자리(기믹 10 · 키트 범용 조각 3 · 폴더 개명 2 · 콘텐츠 1)다. 리플렉션에 "컴포넌트가 내보내는 인터페이스" 표를 더하되 다중 상속 대신 포트 멤버(`exposeMember` 와 같은 모양)로 하고, `Gimmick/Genre` 는 기능 폴더로 쪼갠다. 2 단계는 아래 "실행 차례" 대로 `rename-symbol` · `move-source` 로 한다.

조사 기준: 2026-11-21 `b17d3dcaa7`(main). 추출 스크립트는 `Source/**/*.h` 에서 `class | struct | enum` 정의를 모아(테스트 · `Source/Games` 제외) 베이스 · 순수 가상 수 · `_` 멤버 수를 셌다. 형식 9079 개(구조체 5162 · 클래스 2527 · 열거 1228), 그중 내보낸(`SW_*_API`) 클래스 1615 개는 이름을 모두 읽었고, `Component` 를 이어받은 형식 262 개 중 `Source/Games` 와 `Editor/SelfTest` 를 뺀 184 개는 아래 표에 한 줄씩 적었다. 아래 표에 없는 내보낸 클래스는 "둠"이다 — 세 엔진 중 하나 이상의 같은 일 이름과 낱말 조합이 맞거나(예: `RenderGraph` · `DerivedDataCache` · `PrefabOverrides`), 서비스 업계 이름(`LeaderboardService` · `MatchMaker` · `ReceiptValidator`)이다.

`Source/Games/*` 의 컴포넌트 78 개(`Farm*` · `Nile*` · `Shooter*` · `Voxel*` · `*DemoComponent` …)는 허브 흡수(NEXT "허브 다음 단계") 때 Playground 로 옮기며 이름을 다시 짓는다. 이 문서의 개명 대상이 아니다.

판정의 근거는 하이브리드다. 영역마다 세 엔진(언리얼 · 유니티 · Godot)의 같은 일 이름을 견주고, 이 엔진의 층 이름(언리얼 모양)과 이미 고른 어휘(유니티 `GameObject` · `NavMesh*` · `*2D`)에 맞는 쪽을 고른다. 한 엔진의 관례만으로 정하지 않는다.

## 1. 층 기반 이름

| 지금 이름 | 하는 일 | 언리얼 | 유니티 | Godot | 제안 | 판정 · 까닭 |
| --- | --- | --- | --- | --- | --- | --- |
| `GameObject` | 컴포넌트를 담는 씬 개체. 트랜스폼은 주 씬 컴포넌트가 가진다 | `AActor`(루트 컴포넌트가 트랜스폼) | `GameObject`(Transform 늘 있음) | `Node` | — | 둠. 모양은 언리얼 Actor 지만 "Actor" 는 언리얼 고유 낱말이고 `GameObject` 는 업계에 가장 넓게 퍼진 일반 이름이다. 전수 개명 비용이 이득보다 크다 |
| `Component` | 트랜스폼 없는 기능 단위 | `UActorComponent` | `Component` | `Node` | — | 둠. 언리얼의 `Actor` 접두는 `AActor` 에 매인 이름이라 우리에게 뜻이 없다. 접두 없는 `Component` 는 유니티와 같고, 층 뜻(트랜스폼 없음)은 `SceneComponent` 와 짝으로 읽힌다 |
| `SceneComponent` | 트랜스폼 · 부착 계층 | `USceneComponent` | `Transform` | `Node3D` · `Node2D` | — | 둠. 언리얼과 같다 |
| `ScenePrimitive` | 렌더러가 아는 그리기 슬롯(메시 계열만) | `UPrimitiveComponent` | `Renderer` | `VisualInstance3D` | `PrimitiveComponent` | 바꿈. 층 이름이 `*Component` 로 끝나는데 이것만 빠졌다. 언리얼 "Primitive" 를 쓰되 충돌은 우리 쪽 `PhysicsComponent` 가 따로 맡는다(Godot 의 `VisualInstance3D` · `CollisionObject3D` 분리와 같은 모양) — 헤더 주석에 한 줄 |
| `ScenePrimitiveBatch` | 인스턴스 묶음의 그리기 슬롯 | `UInstancedStaticMeshComponent` 의 프록시 | `Graphics.RenderMeshInstanced` | `MultiMesh` | `PrimitiveBatch` | 바꿈. 위와 짝 |
| `PhysicsComponent` | 물리 몸 · 셰이프의 바탕 | `UPrimitiveComponent`(충돌 쪽) · `UShapeComponent` | `Collider` · `Rigidbody` | `CollisionObject3D` | — | 둠. 언리얼은 그리기와 충돌을 한 층에 둔다. 우리는 Godot 처럼 나눴고 "Physics" 가 그 층을 정확히 말한다 |
| `MeshComponent` | 메시를 그린다 | `UMeshComponent` · `UStaticMeshComponent` | `MeshRenderer` | `MeshInstance3D` | — | 둠 |
| `LightComponent` | 빛의 바탕 | `ULightComponent` | `Light` | `Light3D` | — | 둠 |
| `GameObjectManager` | 씬 하나의 개체 · 틱 · 레지스트리 | `UWorld` | `Scene` | `SceneTree` | — | 둠. `Scene` 은 이미 다른 형식(씬 자산 · 전환)이다 |

## 2. 컴포넌트 층 배치 감사

`Component` 직속 135 개(`Source/Games` · `Editor/SelfTest` 포함)를 "트랜스폼 · 모양을 쓰는가"로 다시 봤다. 아래에 없는 직속 컴포넌트는 같은 오브젝트의 다른 컴포넌트를 읽는 논리 · 데이터라 `Component` 가 맞다.

| 이름 | 지금 층 | 맞는 층 | 이름 제안 | 까닭 |
| --- | --- | --- | --- | --- |
| `ActivatorVolumeComponent` | Component | Component | `TriggerComponent` | 모양이 없다 — 같은 오브젝트의 트리거 콜라이더 겹침이나 `_radius` 거리를 읽는 논리다. 층은 맞고 "Volume"(언리얼에서 모양 있는 브러시 액터)이 틀렸다. 언리얼 `ATriggerBase` · 유니티 `OnTriggerEnter` · Godot `Area3D.body_entered` 의 일 |
| `ZoneVolumeComponent` | Component | SceneComponent | 둠 | 모양(상자 · 구 · 원기둥, `_halfExtents`)을 가진다. 같은 일인 `PostProcessVolumeComponent` · `AudioReverbZoneComponent` 는 SceneComponent 다 — 오프셋을 둘 수 없어 지금은 볼륨마다 빈 자식 오브젝트를 만든다 |
| `SplineComponent` | Component | SceneComponent | 둠 | 조절점이 오브젝트 로컬이다. 언리얼 `USplineComponent` 는 SceneComponent, Godot `Path3D` 는 `Node3D` 다 |
| `TileMapRendererComponent` | Component | SceneComponent | `TileMapComponent` | 그리기 · 충돌 · 이동 비용을 다 한다 — 렌더러만이 아니다. 언리얼 `UPaperTileMapComponent`(Primitive) · Godot `TileMapLayer`(`Node2D`). 그리기 오프셋이 필요하다 |
| `ShadowCaster2DComponent` | Component | SceneComponent | 둠 | 상자 모양(`_size`)을 가진다. 짝인 2D 빛(`LightComponent` 파생)이 SceneComponent 다(유니티 `ShadowCaster2D` 와 이름 같음) |
| `WidgetComponent` | Component | SceneComponent | 둠 | World 모드가 월드 사각형이다. 언리얼 `UWidgetComponent` 는 `UMeshComponent` 파생이다 |
| `MeleeHitboxComponent` | Component | SceneComponent | 둠 | 칼날 두 점이 오브젝트 기준 모양이다. 언리얼 히트박스는 `UShapeComponent` 다. 지금은 따로 움직일 판정마다 자식 오브젝트를 만든다 |
| `SpriteAnimatorComponent` | SceneComponent | Component | 둠 | 트랜스폼을 읽지도 쓰지도 않는다(.cpp 에 트랜스폼 호출 0). 같은 오브젝트 `SpriteComponent` 를 움직이는 논리다(유니티 `Animator` 는 Transform 이 아니다) |
| `HousingPlotComponent` | GameModeComponent | Component | 둠 | 부지 하나가 판 규칙 디렉터를 상속한다. 쓰는 것은 프리팹 스폰뿐이다 — `PrefabPool` 을 멤버로 들고 `Component` 를 상속한다 |
| `UnitStatsComponent` · `AbilityComponent` | HealthSourceComponent | Component + `IHealthSource` | — | 체력 원천은 역할이지 층이 아니다(3 절) |
| `HealthBarComponent` · `DismountOnDeathComponent` | HealthListenerComponent | Component + `IHealthListener` | — | 위와 같다 |

반대 방향("SceneComponent 인데 트랜스폼을 안 쓰는 것")은 `SpriteAnimatorComponent` 하나였다. `AudioListenerComponent` · `VFXComponent` · `FoliageComponent` · `WaterBodyComponent` 는 자리를 쓴다. 트랜스폼을 따로 흉내 내는 컴포넌트(자기 위치 · 회전 칸을 가진 것)는 찾지 못했다 — `InstancedMeshComponent` 의 인스턴스 자리는 컴포넌트 공간이라 맞다.

## 3. 컴포넌트 전수 대조

판정 열: **바꿈**은 제안 이름으로, **둠**은 까닭이 없으면 "세 엔진 중 같은 일 이름과 맞음"이다. 엔진 칸의 `—` 는 그 엔진에 같은 일 형식이 없다는 뜻이다.

### Engine

| 지금 이름 | 하는 일 | 언리얼 | 유니티 | Godot | 제안 | 판정 |
| --- | --- | --- | --- | --- | --- | --- |
| `CameraComponent` | 카메라 | `UCameraComponent` | `Camera` | `Camera3D` | — | 둠 |
| `TagComponent` | 태그 | 액터 Tags · `FGameplayTagContainer` | tag | groups | — | 둠 |
| `MissingComponent` | 모르는 형식의 원문 보존 | — | Missing Script | `MissingNode` | — | 둠 |
| `SkeletalAnimatorComponent` | 애니메이션 그래프 실행 | `UAnimInstance` | `Animator` | `AnimationTree` | — | 둠(Animator 는 유니티 어휘) |
| `SkeletalMeshComponent` | 스킨드 메시 | `USkeletalMeshComponent` | `SkinnedMeshRenderer` | `MeshInstance3D` + `Skeleton3D` | — | 둠 |
| `SpriteAnimatorComponent` | 스프라이트 클립 재생 | `UPaperFlipbookComponent` | `Animator` | `AnimatedSprite2D` | — | 둠(층만, 2 절) |
| `FacialAnimationComponent` | 표정 · 말하기 · 시선 | MetaHuman 얼굴 AnimBP | — | — | — | 둠 |
| `LiveLinkFaceComponent` | Live Link Face 입력 | `ULiveLinkComponent` | — | — | — | 둠 |
| `LocomotionWarpingComponent` | 보폭 · 방향 보정 | Orientation · Stride Warping | — | — | — | 둠 |
| `MotionWarpingComponent` | 루트 모션 목표 맞춤 | `UMotionWarpingComponent` | — | — | — | 둠 |
| `PoseAfterimageComponent` | 포즈 잔상 | 고스트 트레일(Niagara) | — | — | — | 둠 |
| `PoseRetargetComponent` | 실행 중 리타깃 | Retarget Pose From Mesh | — | — | — | 둠 |
| `AnimationNotifyComponent` | 애니메이션 알림을 컴포넌트로 | AnimNotify | Animation Event | 메서드 트랙 | — | 둠 |
| `AudioEmitterComponent` | 소리 내기 | `UAudioComponent` | `AudioSource` | `AudioStreamPlayer3D` | — | 둠 |
| `AudioAmbientEmitterComponent` | 영역 환경음 | `AAmbientSound` | — | — | — | 둠 |
| `AudioListenerComponent` | 귀 | 리스너 | `AudioListener` | `AudioListener3D` | — | 둠 |
| `AudioReverbZoneComponent` | 리버브 영역 | `AAudioVolume` | `AudioReverbZone` | Area 리버브 버스 | — | 둠 |
| `RagdollComponent` | 래그돌 | Physics Asset 시뮬레이션 | — | `PhysicalBoneSimulator3D` | — | 둠 |
| `PairedAnimationComponent` | 둘이 맞춘 애니메이션 | Contextual Animation | — | — | — | 둠(업계 "paired animation") |
| `PoseModifierComponent` | 절차 리그 | `UControlRigComponent` | Animation Rigging | `SkeletonModifier3D` | — | 둠 |
| `SocketBindingComponent` | 부품을 소켓에 붙임 | 소켓 부착 | — | `BoneAttachment3D` | — | 둠 |
| `SocketSetComponent` | 소켓 표 | 스켈레톤 소켓 | — | — | — | 둠 |
| `FractureComponentBase` · `FractureComponent` · `Fracture2DComponent` | 파괴 | `UGeometryCollectionComponent`(Chaos Fracture) | — | — | — | 둠 |
| `FogComponent` | 높이 · 거리 안개 | `UExponentialHeightFogComponent` | 렌더 설정 Fog | Environment Fog | — | 둠 |
| `FoliageComponent` | 식생 인스턴스 | 폴리지 ISM | Terrain Details | `MultiMeshInstance3D` | — | 둠 |
| `FoliageInfluencerComponent` | 풀을 눕히는 구 | Interactive Foliage | — | — | — | 둠 |
| `WindComponent` | 바람 | `UWindDirectionalSourceComponent` | `WindZone` | — | — | 둠 |
| `SkyComponent` | 하늘 | `USkyAtmosphereComponent` | Skybox | `Sky` | — | 둠 |
| `GroundSurfaceComponent` | 디딘 표면 계수 | Physical Material Surface Type | — | — | — | 둠 |
| `SurfaceStampComponent` | 표면 변형 찍기 | 랜드스케이프 스탬프 · RVT 쓰기 | — | — | — | 둠 |
| `TerrainComponent` | 지형 | `ALandscape` | `Terrain` | — | — | 둠 |
| `TerrainColliderComponent` | 지형 충돌 | 랜드스케이프 충돌 | `TerrainCollider` | `HeightMapShape3D` | — | 둠 |
| `BoatComponent` | 배 물리 | — | — | — | — | 둠 |
| `BuoyancyComponent` | 부력 | `UBuoyancyComponent` | — | — | — | 둠 |
| `BuoyancyVolumeComponent` | 물 부피 | Water Physics Volume | — | `Area3D` | — | 둠 |
| `WaterBodyComponent` | 물 | `UWaterBodyComponent` | — | — | — | 둠 |
| `NavMeshAgentComponent` · `NavMeshLinkComponent` · `NavMeshModifierComponent` · `NavMeshObstacleComponent` · `NavMeshSurfaceComponent` | 내비게이션 | NavLink Proxy · NavModifier | `NavMeshAgent` · `NavMeshLink` · `NavMeshModifier` · `NavMeshObstacle` · `NavMeshSurface` | `NavigationAgent3D` · `NavigationLink3D` | — | 둠(유니티와 같음) |
| `BoxCollider2DComponent` | 2D 상자 콜라이더 | `UBoxComponent` | `BoxCollider2D` | `CollisionShape2D` | — | 둠 |
| `CharacterControllerComponent` · `CharacterController2DComponent` | 캐릭터 이동 몸 | `UCharacterMovementComponent` | `CharacterController` | `CharacterBody3D` | — | 둠 |
| `JointComponent` · `Joint2DComponent` | 관절 | `UPhysicsConstraintComponent` | `Joint` | `Joint3D` | — | 둠 |
| `RigidBodyComponent` · `RigidBody2DComponent` | 강체 | 시뮬레이션 Primitive | `Rigidbody` | `RigidBody3D` | — | 둠 |
| `ClothComponent` | 옷감 | Chaos Cloth | `Cloth` | `SoftBody3D` | — | 둠 |
| `WheeledVehicleComponent` | 바퀴 차 물리 | `UChaosWheeledVehicleMovementComponent` | `WheelCollider` | `VehicleBody3D` | — | 둠 |
| `LaunchVelocityComponent` | 스폰 때 강체 첫 속도 | `UProjectileMovementComponent::InitialSpeed` | `Rigidbody.velocity` | — | — | 둠 |
| `SequencePlayerComponent` | 시퀀스 재생 | `ULevelSequencePlayer` | `PlayableDirector` | `AnimationPlayer` | — | 둠 |
| `TileMapRendererComponent` | 타일맵 그리기 · 충돌 · 비용 | `UPaperTileMapComponent` | `Tilemap` + `TilemapRenderer` | `TileMapLayer` | `TileMapComponent` | 바꿈(2 절) |
| `WidgetComponent` | 오브젝트에 붙는 UI | `UWidgetComponent` | World Space Canvas | `SubViewport` | — | 둠(층만) |
| `VFXComponent` | 파티클 | `UNiagaraComponent` | `VisualEffect` | `GPUParticles3D` | — | 둠 |
| `PointLight2DComponent` · `FreeformLight2DComponent` · `GlobalLight2DComponent` | 2D 빛 | — | `Light2D`(Point · Freeform · Global) | `PointLight2D` | — | 둠 |
| `ParallaxLayerComponent` | 시차 레이어 | — | — | `ParallaxLayer` | — | 둠 |
| `PixelPerfectCameraComponent` | 픽셀 퍼펙트 | — | `PixelPerfectCamera` | — | — | 둠 |
| `ShadowCaster2DComponent` | 2D 그림자 가림 | — | `ShadowCaster2D` | `LightOccluder2D` | — | 둠(층만) |
| `SpriteComponent` | 스프라이트 | `UPaperSpriteComponent` | `SpriteRenderer` | `Sprite2D` | — | 둠 |
| `DecalComponent` | 데칼 | `UDecalComponent` | `DecalProjector` | `Decal` | — | 둠 |
| `DirectionalLightComponent` · `PointLightComponent` · `SpotLightComponent` | 빛 | 같은 이름 | `Light`(Type) | `DirectionalLight3D` · `OmniLight3D` · `SpotLight3D` | — | 둠 |
| `InstancedMeshComponent` | 인스턴스 메시 | `UInstancedStaticMeshComponent` | `RenderMeshInstanced` | `MultiMeshInstance3D` | — | 둠 |
| `LightProbeVolumeComponent` | 프로브 격자 | Volumetric Lightmap | Adaptive Probe Volume | `LightmapGI` 프로브 | — | 둠 |
| `MeshSectionComponent` | 메시 구간 머티리얼 | Mesh Section | SubMesh | Surface | — | 둠 |
| `PostProcessVolumeComponent` | 후처리 영역 | `APostProcessVolume` | `Volume` | `WorldEnvironment` | — | 둠 |
| `ReflectionProbeComponent` | 반사 프로브 | `USphereReflectionCaptureComponent` | `ReflectionProbe` | `ReflectionProbe` | — | 둠 |

### GameFramework

| 지금 이름 | 하는 일 | 언리얼 | 유니티 | Godot | 제안 | 판정 |
| --- | --- | --- | --- | --- | --- | --- |
| `TitleMenuComponent` | 타이틀 메뉴 띄우기 | — | — | — | — | 둠 |
| `HUDControllerComponent` | HUD 문서 · 뷰모델 걸기 | `AHUD` | — | — | — | 둠 |
| `DamageNumberComponent` | 피해 숫자 | Lyra 데미지 넘버 | — | — | — | 둠 |
| `HealthBarComponent` | HP 바 | Lyra 체력 바 위젯 | — | — | — | 둠(베이스만 `IHealthListener`) |
| `ObjectiveMarkerComponent` · `QuestObjectiveMarkerComponent` | 목표 마커 | Lyra Indicator | — | — | — | 둠 |
| `DestructionNetSessionComponent` · `PairedAnimationNetSessionComponent` · `SequenceNetSessionComponent` · `HousingNetSessionComponent` | 씬 기능 복제 다리 | Replication Graph 노드 | — | `MultiplayerSynchronizer` | — | 둠 |
| `PairedAnimationNetStarterComponent` | 확인 씬의 반복 시작자 | — | — | — | — | 둠 |
| `HousingPlotComponent` | 하우징 부지 | — | — | — | — | 둠(층만, 2 절) |
| `OverworldGameModeComponent` · `OverworldWalkerViewComponent` | 오버월드 판 · 주인공 모습 | — | — | — | — | 둠(`walk` 는 게임 범위 낱말) |
| `MeleeHitboxComponent` | 근접 판정 칼날 | `UShapeComponent` 히트박스 | — | `Area3D` | — | 둠(층만) |
| `UnitStatsComponent` | 유닛 스탯 · 피해 | Lyra `ULyraHealthComponent` | — | — | — | 둠(베이스만, 키트 밖으로 이동은 7 절) |
| `HorseFollowAIController` | 말 따라오기 AI | `AAIController` | — | — | `HorseFollowAIControllerComponent` | 바꿈. 컴포넌트인데 `Component` 접미가 없는 유일한 형식 |
| `WesternWitnessComponent` · `WesternDeedComponent` · `WesternGameModeComponent` · `WesternHorseComponent` · `WesternHorseMountComponent` | 서부극 키트 | — | — | — | — | 둠(키트 접두) |
| `ScheduleNPCComponent` | 하루 일정 NPC 정체 | Mass Smart Object 일정 | — | — | `NPCScheduleComponent` | 바꿈. 영어 어순(명사 수식어가 앞) |
| `CameraDirectorComponent` | 카메라 프리셋 · 블렌드 | Camera Modifier · Gameplay Cameras | Cinemachine Brain | — | — | 둠 |
| `CameraManagerComponent` | 뷰 고르기 | `APlayerCameraManager` | Cinemachine Brain | — | — | 둠 |
| `CinematicCameraComponent` | 시퀀스 카메라 신호 전달 | `ACineCameraActor` | `CinemachineCamera` | — | — | 둠 |
| `FollowCameraComponent` · `FirstPersonCameraComponent` | 카메라 리그 논리 | `USpringArmComponent` | Cinemachine Follow | Phantom Camera | — | 둠 |
| `SequenceTriggerComponent` | 들어오면 컷씬 | Level Sequence 트리거 | Timeline 트리거 | — | — | 둠 |
| `HealthSourceComponent` | 체력 읽기 계약 | Lyra Health(UINTERFACE 자리) | `IHealth`(GetComponent) | — | `IHealthSource` | 바꿈(4 절) |
| `HealthListenerComponent` | 체력 변화 받기 계약 | `OnHealthChanged` 델리게이트 | 인터페이스 | signal | `IHealthListener` | 바꿈(4 절) |
| `KillcamComponent` | 킬캠 | ShooterGame 킬캠 | — | — | — | 둠 |
| `AIControllerComponent` · `ControllerComponent` · `PlayerControllerComponent` · `RemoteControllerComponent` · `IntentTrackControllerComponent` | 조종자 | `AController` · `AAIController` · `APlayerController` | — | — | — | 둠 |
| `PlayerStateComponent` | 플레이어 상태 | `APlayerState` | — | — | — | 둠 |
| `PawnComponent` | 폰 의도 | `APawn` · Lyra `ULyraPawnExtensionComponent` | — | — | — | 둠 |
| `CharacterPawnMovementComponent` · `CharacterPawnMovement2DComponent` | 의도 → 캐릭터 컨트롤러 | `UCharacterMovementComponent` | — | `CharacterBody3D` 스크립트 | `CharacterMovementComponent` · `CharacterMovement2DComponent` | 바꿈. "Character" 와 "Pawn" 이 겹친다. 언리얼 이름 그대로 |
| `MoveAnimationComponent` | 이동 속도 → 애니메이터, 이동 쪽 보기 | `bOrientRotationToMovement` + AnimBP | — | — | — | 둠 |
| `GameFlagsOwnerComponent` | 월드 플래그를 가진 쪽 계약 | `UInterface` | 인터페이스 | — | `IGameFlagsOwner` | 바꿈(4 절) |
| `ActivatorVolumeComponent` | 들어오면 한 번 일하는 바탕 | `ATriggerBase` | `OnTriggerEnter` | `Area3D` | `TriggerComponent` | 바꿈(2 절) |
| `GameModeBoundComponent` | 게임 모드를 핸들로 보는 바탕 | Lyra `UGameFrameworkComponent` | — | — | `GameFrameworkComponent` | 바꿈. "Bound" 는 무엇에 묶였는지 말하지 않는 조어다 |
| `GameModeComponent` | 판 규칙 · 스폰 디렉터 | `AGameModeBase` | — | — | — | 둠 |
| `MinigameTriggerComponent` · `AutosaveTriggerComponent` | 트리거 파생 | — | — | — | — | 둠 |
| `TutorialHintComponent` | 들어오면 튜토리얼 힌트 | — | — | — | `TutorialHintTriggerComponent` | 바꿈. 트리거 파생 셋이 모두 `*TriggerComponent` 인데 이것만 빠졌다 |
| `AbilityComponent` | 어트리뷰트 · 이펙트 · 어빌리티 | `UAbilitySystemComponent` | — | — | `AbilitySystemComponent` | 바꿈. 같은 폴더가 GAS 어휘(`AttributeSet` · `GameplayEffect` · `AbilityTask`)인데 이 이름만 줄였다 |
| `AbilityGateComponent` | 태그를 모두 가진 것이 오면 열리는 문 | — | — | — | `TagGateComponent` | 바꿈. 능력 태그 · 열쇠 카드 · 퀘스트 태그 모두 같은 일이다(장르 어휘 제거, 5 절) |
| `CharacterAppearanceComponent` · `CharacterBodyDynamicsComponent` · `CharacterEquipmentComponent` · `SequenceAppearanceComponent` | 외형 · 몸 · 장비 | — | — | — | — | 둠 |
| `SurfaceExposureComponent` | 비 · 물이 몸을 적시고 씻는다 | — | — | — | `CharacterWetnessComponent` | 바꿈. "Surface Exposure" 는 어느 엔진 · 게임도 쓰지 않는 조합이다. 업계는 wetness(젖음)로 부르고 씻김은 그 결과다. `SurfaceExposureSystem` → `CharacterWetnessSystem` |
| `DialogueRunnerComponent` | 대화 진행 | — | Yarn Spinner `DialogueRunner` | Dialogue Manager | — | 둠 |
| `DismembermentComponent` | 치명타 부위 절단 · 조각 · 피 | Lyra 없음. 게임은 Gore 시스템(KF2 `KFGoreManager`, Gears GoreMesh) | Gore 에셋 | — | `GoreComponent` | 바꿈. 한 컴포넌트가 자르기와 피 표면을 함께 한다 — 업계는 둘을 "고어"로 묶는다. "dismember" 는 동사로 함수 이름에만 남긴다. `DismembermentResult` → `GoreCutResult`, 폴더 `Gameplay/Dismemberment` → `Gameplay/Gore` |
| `GimmickCircuitComponent` | 기믹 회로 | 레벨 블루프린트 · Valve I/O | — | — | — | 둠 |
| `GimmickSensorComponent` | 회로 센서가 읽는 겹침 · 충격 · 신호 | — | — | `Area3D` | — | 둠 |
| `GimmickWeightComponent` | 눌림판이 재는 무게 | — | — | — | `PressurePlateWeightComponent` | 바꿈. 읽는 쪽(눌림판)을 이름에 넣는다 |
| `TickedPathMoverComponent` | 틱의 함수로 움직이는 바닥 | `UInterpToMovementComponent` | — | `AnimatableBody3D` + `PathFollow3D` | `InterpToMovementComponent` | 바꿈. "Ticked" 는 구현 방식이다. 하는 일은 언리얼 이름 그대로. `GimmickMoverMode` → `InterpToMovementMode` |
| `GrabberComponent` | 강체 집기 | `UPhysicsHandleComponent` | — | — | — | 둠(언리얼 예제 이름) |
| `InteractableComponent` · `InteractorComponent` | 상호작용 대상 · 하는 쪽 | Lyra Interaction | — | — | — | 둠 |
| `SmartObjectComponent` | 슬롯 차지 | `USmartObjectComponent` | — | — | — | 둠 |
| `WorldItemComponent` | 월드에 놓인 아이템 | Lyra `IPickupable` | Pickup | — | `PickupComponent` | 바꿈. "World Item" 은 놓인 자리를, 업계 이름 pickup 은 하는 일을 말한다 |
| `SessionStateComponent` | 판 상태(진행 플래그 · 날씨 · 세이브) | `AGameStateBase` | — | — | `GameStateComponent` | 바꿈. `GameModeComponent` 와 짝(언리얼 GameMode · GameState). `SessionStateSettings` · `SessionStateInitResult` 도 `GameState*` |
| `BoatHelmComponent` | 배 조타 | — | — | — | — | 둠 |
| `DismountOnDeathComponent` | 쓰러지면 내림 | — | — | — | — | 둠(베이스만) |
| `KinematicVehicleComponent` | 바디 없는 탈것 이동 | `UFloatingPawnMovement` | — | — | — | 둠 |
| `MountInteractionComponent` · `MountMovementComponent` | 탈것 타기 · 걸음새 | — | — | — | — | 둠 |
| `PhysicsCarComponent` | 의도 → 바퀴 차 입력 | Chaos Vehicle Pawn | Car Controller | — | `WheeledVehicleDriveComponent` | 바꿈. 대상 컴포넌트(`WheeledVehicleComponent`) 이름을 따른다 |
| `VehicleExitComponent` · `VehicleSeatComponent` | 내리기 · 좌석 | — | — | — | — | 둠 |
| `WeatherDirectorComponent` · `WeatherWaveComponent` | 날씨 열기 · 파도 | — | — | — | — | 둠 |
| `JumpEscapeReactionComponent` | 뛰면 가라앉음 스택 감소 | — | — | — | `JumpEscapeComponent` | 바꿈. 셋 다 "Reaction" 은 지역 효과의 반응이라는 폴더 문맥이다 — 이름에서 빼고 폴더(`Zone/Reaction/`)로 |
| `ScreenReactionComponent` | 상태 태그 동안 화면 효과 | Gameplay Cue(화면) | — | — | `StatusScreenEffectComponent` | 바꿈. 업계 "status effect" + "screen effect" |
| `VisualSinkReactionComponent` | 가라앉음 깊이만큼 외형 내림 | — | — | — | `SinkVisualOffsetComponent` | 바꿈. `SunkVisual` 은 그대로 |
| `ZoneContactComponent` · `ZoneVolumeComponent` | 지역 효과 받는 쪽 · 지역 볼륨 | Physics Volume | — | `Area3D` | — | 둠(볼륨은 층만) |
| `DaylightComponent` | 시각 → 해 방향광 | `ASunSky` | — | — | — | 둠 |
| `PropScatterComponent` | 소품 흩뿌리기 | PCG Scatter | — | — | — | 둠 |
| `FadeOutComponent` · `PersistAcrossScenesComponent` | 사라지기 · 씬 넘어 남기 | — | `DontDestroyOnLoad` | Autoload | — | 둠 |
| `SplineComponent` · `SplineRiverPathComponent` | 스플라인 · 강 경로 어댑터 | `USplineComponent` | `SplineContainer` | `Path3D` | — | 둠 |
| 장르 기믹 18 개 | 5 절 | | | | | 이름 둠, 파일 이동(5 절). 바꾸는 것은 `AbilityGateComponent` · `ScareTriggerComponent` 둘뿐 |

## 4. 인터페이스 규칙과 리플렉션 인터페이스

### 4.1 `I` 가 없는데 상태 없는 계약인 것 (→ `I` 를 붙인다)

| 지금 이름 | 근거 | 제안 |
| --- | --- | --- |
| `HealthSourceComponent` | 순수 가상 `getHealthReading` 하나 + 알림 도우미, 멤버 0. 파생이 이미 자기 수치를 가진다 | `IHealthSource` |
| `HealthListenerComponent` | 순수 가상 `onHealthChanged` 하나 + 정적 `broadcast`, 멤버 0 | `IHealthListener` |
| `GameFlagsOwnerComponent` | 순수 가상 `findOpenFlags` 하나 + 레지스트리 등록, 멤버 0. 헤더가 스스로 "언리얼 UInterface 자리"라 적는다 | `IGameFlagsOwner` |
| `AutomationStepContext` | 순수 가상 19 개, 멤버 0("좁은 문맥 인터페이스") | `IAutomationStepContext` |
| `ZoneModuleProcessor` | 순수 가상 3 개 + 인라인 도우미, 멤버 0. 처리기 계약 | `IZoneModuleProcessor` |

`RHIRenderResource` · `SaveGame` · `WesternGameModeComponent` 도 순수 가상을 가지지만 전역 목록 등록 · 직렬화 칸 · 디렉터 상태를 가진 베이스라 둔다(언리얼 `FRenderResource` · `USaveGame` 와 같다). `ISubsystem` 처럼 기본 구현이 있는 가상만 가진 상태 없는 형식(`IReplicationPolicy` · `IInterestPolicy` · `ITurnPolicy` · `INetSimSession` · `IInspectorComponent` · `IEditorAssetTypeActions`)과 표식 형식(`IConfig` · `IAutomationShutdownWatch` · `IAutomationStepState`)은 인터페이스로 둔다(언리얼 인터페이스도 기본 구현을 가진다).

### 4.2 `I` 가 붙었는데 상태가 있는 것 (→ `I` 를 뗀다)

| 지금 이름 | 가진 상태 | 제안 | 까닭 |
| --- | --- | --- | --- |
| `IEvent` | `_next`(관입 큐 고리) | `EventBase` | 고리는 큐의 본질이라 빼지 않는다. `Event` 는 `ZoneTracker::Event` 와 헷갈린다 |
| `ISubsystem` | `_memoryTag` | `Subsystem` | 언리얼 `USubsystem`. 메모리 태그는 구조 자리 규칙(memory-tag-structural)이라 남는다 |
| `IWindow` | 제목 · 델리게이트 슬롯 21 칸 | `GenericWindow` | 언리얼 `FGenericWindow`. `Window` 한 낱말은 개명 도구가 흔한 이름으로 거절한다 |
| `ISplashWindow` | 상태 · 그림 · 진행 | `SplashWindowBase` | `SplashWindow` 는 이미 감싸는 형식 이름이다 |
| `IEditorPanel` | 창 상태 12 칸 | `EditorPanel` | 언리얼 `SDockTab` · Godot `EditorPlugin` 패널의 바탕 |
| `IEditorDocumentPanel` | 더티 · 닫기 확인 | `EditorDocumentPanelBase` | `EditorDocumentPanel` 이 이미 파생 이름이다 |
| `IEditorPopup` | `_bOpen` | `EditorPopup` | |
| `IAudioEffect` | 형식 · 매개변수 · 채널 | `AudioEffect` | Godot `AudioEffect` 와 같다 |
| `IFileWatcher` | 폴더 · 뮤텍스 | `FileWatcher` | 파생 `WindowsFileWatcher` · `LinuxFileWatcher` 와 짝 |
| `IAllocationTracker` | `_trackerID` | `AllocationTracker` | |
| `IDataInheritanceSource` | `_maxDepth` | `DataInheritanceSource` | 파생 `DataInheritanceDiskSource` 와 짝 |
| `IServiceStore` | `_commitStats` | `ServiceStore` | |
| `IServiceStoreJob` | 로그 문맥 · 종류 · 취소 | `ServiceStoreJob` | |
| `IAccountPresence` | id 할당기 · 구독 | `AccountPresence` | |
| `IOnlineClientService` | 등록 클라이언트 목록 | `OnlineClientService` | |

상태를 빼서 인터페이스로 남기는 길은 고르지 않았다 — 위 상태는 모두 모든 파생이 같은 뜻으로 쓰는 공통 칸이라, 빼면 파생마다 같은 칸이 되풀이된다.

### 4.3 리플렉션이 아는 인터페이스

견줌:

| 엔진 | 모양 | 찾기 |
| --- | --- | --- |
| 언리얼 | `UINTERFACE` + `IInterface` 다중 상속. 리플렉션이 클래스의 인터페이스 표를 안다 | `FindComponentByInterface( UMyInterface::StaticClass() )` · `Implements<UMyInterface>()` · `TScriptInterface` |
| 유니티 | C# 인터페이스를 MonoBehaviour 가 구현 | `GetComponent<IInterface>()` · `GetComponents<IInterface>()` |
| Godot | 인터페이스 없음. `has_method` · 그룹 · 시그널 | 덕 타이핑 |

판단: 둔다. 지금은 RTTI 가 없어 역할을 리플렉션 베이스로 만들었고(`HealthSourceComponent` 헤더 주석), 그 탓에 역할 하나가 단일 베이스 자리를 차지한다 — 체력 원천이면서 플래그 주인인 컴포넌트는 만들 수 없다. 유니티 `GetComponent<IHealth>()` 의 쓰임새와 언리얼의 리플렉션 표를 가져오되, 이 저장소의 "기반 하나" 규칙(CppStyle "Inheritance: one base")에 맞춰 **다중 상속 대신 포트 멤버**로 내보낸다. 이것은 서브시스템의 `SubsystemScope::exposeMember` · CppStyle 의 "nested port class forwards to the owner" 와 같은 모양이라 규칙 예외가 필요 없다.

설계:

1. 인터페이스 형식: `struct IHealthSource { virtual ~IHealthSource() = default; virtual HealthReading getHealthReading() const = 0; };` 에 `REFLECT( Interface )` 를 단다. 리플렉션 파서는 `Interface` 형식에 `PROPERTY` · 데이터 멤버가 있으면 오류로 멈추고, `static_assert( std::is_abstract_v<I> && sizeof( I ) == sizeof( void* ) )` 를 생성 코드에 넣어 상태가 붙는 것을 형식으로 막는다(4.2 의 결함이 다시 생기지 않게).
2. 내보내기: 컴포넌트가 포트 멤버를 들고 한 줄로 등록한다.
   ```cpp
   class SW_GF_API AbilitySystemComponent : public Component
   {
       REFLECT_BODY();
       REFLECT_INTERFACE( IHealthSource, _healthSourcePort );   // TypeInfo 의 인터페이스 표에 (IHealthSource 형식, 멤버 오프셋)
   private:
       struct HealthSourcePort final : IHealthSource            // 주인에게 넘기는 포트(CppStyle 의 nested port)
       {
           HealthReading getHealthReading() const override;     // 주인 = SW_CONTAINER_OF( this, AbilitySystemComponent, _healthSourcePort )
       };
       HealthSourcePort _healthSourcePort;
   };
   ```
   포트가 주인을 찾는 길은 오프셋 하나(`offsetof` 를 리플렉션이 이미 칸마다 가진다)라 포인터 칸이 늘지 않는다.
3. `TypeInfo` 에 인터페이스 표: `{ const TypeInfo* pInterface; uint32 portOffset; }` 의 작은 고정 배열(상한은 `constant::reflection` 에 하나 — 4). 링크 때 조상 표를 합쳐 둬(조상 표 `_arrType` 을 합치는 길과 같다) 찾기에 사슬 걷기가 없다.
4. 찾기:
   - `GameObject::findInterface<I>()` → `I*`(첫 하나), `GameObject::findInterfaces<I>( outListInterface )`. `getComponent<T>` 와 같은 루프이고 컴포넌트마다 표 칸 몇 개를 비교한다. 병렬 틱 읽기 규칙도 `getComponent` · `getTickSafeComponent` 와 같게 둘로 둔다.
   - 씬 전체: `ComponentRegistry` 칸을 인터페이스 형식으로도 연다(`GameFlagsOwnerComponent` 가 지금 손으로 하는 등록을 리플렉션 표가 대신한다). `findFirstOpen` 은 `ComponentRegistry::findFirst<IGameFlagsOwner>()` 가 된다.
   - 역방향: `Component::findOwningComponent( const I* )` 는 두지 않는다 — 인터페이스를 받은 쪽은 계약만 쓴다(언리얼 `TScriptInterface::GetObject` 의 쓰임은 우리 코드에 없다).
5. 도우미의 자리: `HealthSourceComponent::notifyHealthChanged` · `deliverFatalHit` 는 인터페이스의 정적 함수(`IHealthSource::notifyHealthChanged( const IHealthSource&, GameObject& owner, bool bReset )`)로 옮긴다 — 상태 없는 정적 함수는 인터페이스 규칙을 어기지 않는다. `HealthListenerComponent::broadcast` 는 `IHealthListener::broadcast` 로, 안에서 `findInterfaces<IHealthListener>` 를 쓴다.
6. 에디터 · 직렬화: 인터페이스 표는 직렬화하지 않는다(형식의 성질). 인스펙터 머리에 "Implements: IHealthSource" 를 한 줄 보인다. 검증(`ReflectionValidation`)은 "같은 인터페이스를 두 번 내보냄"을 오류로 본다.

적용 대상(2 단계): `IHealthSource`(구현 `AbilitySystemComponent` · `UnitStatsComponent` · `ShooterEnemyComponent`), `IHealthListener`(`HealthBarComponent` · `DismountOnDeathComponent`), `IGameFlagsOwner`(`GameStateComponent`). 이 셋이 베이스 자리를 내놓으면 `UnitStatsComponent` 등은 `Component` 직속이 된다.

## 5. 기믹 구조

### 5.1 지금

`Gimmick/Genre/` 의 여섯 파일(`AdventureGimmicks` · `HorrorStealthGimmicks` · `PlatformerGimmicks` · `ProgressionGimmicks` · `RacingGimmicks` · `ShooterGimmicks`)에 컴포넌트 18 개, 공용 도구 `GenreGimmickUtil` 에 이벤트 다섯 · 시계 · 몸 켜기가 있다. 콘텐츠도 `Content/common/prefabs/gimmicks/{adventure,horror,metroidvania,platformer,racing,rpg,shooter,stealth,common}` 으로 같은 장르 나눔이다.

장르에 매인 것은 없다. 18 개 모두 같은 오브젝트의 센서 · 겹침 · 태그 · 상호작용만 읽고, 어느 키트도 이 파일을 include 하지 않는다(쓰는 곳은 프리팹 · Playground 맵 · `TurretComponent` 를 쓰는 Gameplay 한 곳 · `ExplosiveBarrelComponent` 를 쓰는 Feature 한 곳). 같은 일이 장르 이름 탓에 둘로 보이기도 한다 — 헤더가 스스로 "점프대는 `LaunchPadComponent` 로"(슈터) · "키카드 문은 상호작용과 회로로"라 적는다. 장르로 나누면 새 기믹을 어느 장르에 둘지가 매번 갈리고, 한 파일에 컴포넌트 3–5 개라 하나를 고치면 이웃이 다시 컴파일된다. 상용 엔진의 기본 제공 게임플레이 형식(언리얼 Lyra · 유니티 2D Platformer 마이크로게임 · Godot 데모)은 모두 기능으로 나누고 컴포넌트마다 파일 하나다.

### 5.2 재구성안

컴포넌트마다 `.h` · `.cpp` 한 쌍으로 쪼개고 기능 폴더에 둔다.

| 새 자리 | 컴포넌트 | 까닭 |
| --- | --- | --- |
| `Gimmick/Traversal/` | `CrumblePlatformComponent`(+`CrumbleState`) · `OneWayPlatformComponent` · `ClimbZoneComponent` | 서고 오르는 바닥(업계 "traversal") |
| `Gimmick/Movement/` | `LaunchPadComponent` · `BoostPadComponent` · `ConveyorComponent` · `PushBlockComponent` · `InterpToMovementComponent`(옛 `TickedPathMoverComponent`) | 몸을 옮기거나 쏘는 것 |
| `Gimmick/Loot/` | `ItemBoxComponent`(+`GimmickItemChoice` → `ItemBoxChoice`) · `GatheringNodeComponent` | 아이템을 주는 것(`GimmickItemEvent` 를 낸다) |
| `Gimmick/Trigger/` | `CueTriggerComponent`(옛 `ScareTriggerComponent`) · `TagGateComponent`(옛 `AbilityGateComponent`) · `GimmickSensorComponent` · `PressurePlateWeightComponent` | 들어옴 · 조건으로 일하는 것. `CueTriggerComponent` 는 `TriggerComponent` 를 상속해 겹침 · 거리 · 판정 자리 · 안에서 생김 규칙을 다시 쓰지 않는다("Scare" 는 공포 연출이라는 쓰임일 뿐 하는 일은 연출 신호 + 소리) |
| `Gimmick/Combat/` | `ExplosiveBarrelComponent` · `TurretComponent`(+`TurretShotTarget` · `TurretShotWorld`) · `DestructibleComponent` · `GimmickDamageUtil` | 피해를 받거나 주는 것 |
| `Gimmick/Element/` | `ElementStatusComponent` · `ElementGrid` · `ElementRuleCatalog` | 원소 규칙 한 벌(지금 Gimmick 뿌리와 Genre 에 흩어짐) |
| `Gimmick/Circuit/` | `GimmickCircuit*` · `GimmickNodeRegistry` · `GimmickStateBundle` | 회로(지금 Gimmick 뿌리) |
| `Character/AI/Perception/` | `NoiseEmitterComponent` · `LightExposure` | AI 감각 자극이다 — 언리얼 `UPawnNoiseEmitterComponent` 가 AI Perception 옆에 있다. 잠입 장르가 아니라 감각 시스템의 입력 |
| `Gameplay/Interaction/` | `HidingSpotComponent` | 스마트 오브젝트 슬롯(Activity.Hide)을 차지하는 일이라 `SmartObjectComponent` 옆 |
| `World/Environment/` | `FlickerLightComponent` | 같은 오브젝트 빛 세기를 패턴으로 바꾸는 환경 효과(`DaylightComponent` 옆) |

`GenreGimmickUtil` 은 없앤다: 이벤트 다섯은 이미 있는 `Gimmick/GimmickEvents.h` 로 합치고, `toSteps` · `makeClock` 은 주인인 `FixedStepTimer` 의 정적 멤버(`FixedStepTimer::makeGameplayClock` · `secondsToSteps`)로, `setBodyActive` · `setInteractableEnabled` 는 `GimmickUtil`(주인 없는 도구, Gimmick 뿌리)로 간다.

콘텐츠: `Content/common/prefabs/gimmicks/` 아래도 같은 이름의 기능 폴더(`traversal` · `movement` · `loot` · `trigger` · `combat` · `element` · `logic` — 지금 `common` 의 승강기 · 회전 발판 · 위험 구역과 `adventure` 의 스위치 · 레버 · 잠긴 문)로 `move-asset` 한다. 프리팹은 GUID 로 참조하므로 맵이 깨지지 않는다.

위험: `GimmickLaunchEvent` · `GimmickBoostEvent` · `GimmickItemEvent` · `GimmickNoiseEvent` · `GimmickCueEvent` 를 구독하는 코드가 `Gimmick/Genre` 밖 Source 에 없다(이벤트 이름으로 grep 0). 주석은 "캐릭터 컨트롤러 · 차량 몸 · 인벤토리 규칙이 받는다"고 적는다. 2 단계에서 옮기기 전에 받는 쪽이 이름 해시로 구독하는지, 아니면 내기만 하고 아무도 받지 않는지 확인한다 — 후자면 발사대가 이벤트만 내고 몸을 쏘지 않는 결함이다.

### 5.3 장르 · 게임 이름으로 나눴지만 범용인 것

| 지금 자리 | 형식 | 새 자리 | 까닭 |
| --- | --- | --- | --- |
| `Kits/Genre/RPG/MonsterHuntRPG/SkillTree.*` | `SkillTreeCatalog` · `SkillTreeState` | `Runtime/Gameplay/Progression/` | 스킬 · 랭크 · 선행 · 배타 그룹은 장르 무관(언리얼 Lyra 없음, 업계 일반 "skill tree") |
| `Kits/Genre/Strategy/TacticsSRPG/RunMap.*` | `RunMap` | `Runtime/Gameplay/Progression/` | 헤더가 "노드 종류는 데이터라 장르마다 바꿀 수 있다"고 적는다(슬레이 더 스파이어 · 메탈슬러그 택틱스) |
| `Kits/Genre/Action/ActionCombat/Component/UnitStatsComponent.*` · `MeleeHitboxComponent.*` | 두 컴포넌트 | `Runtime/Character/Combat/` | 피해 입구(`takeDamage`, 언리얼 `AActor::TakeDamage` 자리)와 근접 칼날 판정은 투사체 · 몬스터 사냥 · 플랫포머 공용이다. 클라이언트 `DamageNumberComponent` 주석도 피해를 내는 곳으로 이 키트 형식을 든다 |
| `Kits/Genre/Action/ActionPlatformer/ActionCombatSimulation.*` | `ActionCombatSimulation` | 같은 자리, 이름 `PlatformerCombatSimulation` | ActionCombat 키트와 이름이 겹쳐 다른 키트 것으로 읽힌다 |
| `Kits/Genre/Strategy/RealTimeStrategy/RTSVision.*` | `RTSVision` | 같은 자리, 이름 `FogOfWar` | 하는 일은 업계 이름 "fog of war"(RTS · MOBA · SRPG 공용). 둘째 쓰는 키트가 생기면 Runtime 으로 |
| `Runtime/Gameplay/Gimmick/Genre/` | 5.2 | 5.2 | |
| `Content/common/prefabs/gimmicks/<장르>/` | 프리팹 | 5.2 | |

`Kits/Genre/*` 자체(키트를 장르로 나눈 것)는 맞다 — 키트는 장르 규칙 묶음이 정의다(언리얼 템플릿 · 유니티 마이크로게임이 장르별인 것과 같다). 문제는 그 안의 범용 조각뿐이다.

법 · 라이선스 확인 거리(사용자에게 물을 것): `Kits/Genre/Casual/CardGame/UnoGame` — "UNO" 는 Mattel 상표다. 규칙 구현은 상표가 아니지만 형식 · 파일 · 화면 이름에 상표를 쓰는 것은 외부 공개 전에 판단이 필요하다(대안 이름 `SheddingCardGame`, 장르 일반 이름 "shedding-type card game").

## 6. 그 밖의 형식 · 열거

| 지금 이름 | 제안 | 까닭 |
| --- | --- | --- |
| `ActivatorAuthority` · `ActivatorAuthorityRule` · `ActivatorSpawnInside` · `ActivatorSpawnWindow` | `TriggerAuthority` · `TriggerAuthorityRule` · `TriggerSpawnInside` · `TriggerSpawnWindow` | 주인 `TriggerComponent` 를 따른다. "activator" 는 언리얼 `OtherActor` · 유니티 `other` · Godot `body` 어느 것과도 다른 조어 — 멤버 `_activator` 는 `_target` 으로(멤버 개명은 쌍 표에 함께) |
| `ScheduleNPCPhase` | `NPCSchedulePhase` | 주인을 따른다 |
| `GimmickMoverMode` | `InterpToMovementMode` | 주인을 따른다 |
| `GimmickItemChoice` | `ItemBoxChoice` | 주인을 따른다 |
| `SurfaceExposureSystem` | `CharacterWetnessSystem` | 주인을 따른다 |
| `SessionStateSettings` · `SessionStateInitResult` | `GameStateSettings` · `GameStateInitResult` | 주인을 따른다 |
| `DismembermentResult` | `GoreCutResult` | 주인을 따른다 |
| `FacingDir` | `FacingDirection` | 줄임말 금지(NamingCpp "No opaque abbreviations") |
| `GenreGimmickUtil` | (없앰, 5.2) | |

열거 1228 개 중 위 말고는 주인 형식 이름 + `Kind` · `Mode` · `Phase` · `Result` 꼴이라 둔다.

## 7. 실행 차례(2 단계)

모든 개명은 `py -3 -m Scripts rename-symbol --pairs Config/Rules/Renames/<표>.json` 로, 파일 · 폴더 이동은 `py -3 -m Scripts move-source` 로, 콘텐츠는 `move-asset` 으로 한다. 개명 도구가 옛 이름을 `_bannedNames` 에 자동으로 올린다(아래 목록은 대조용). 손으로 하는 코드 일(쪼개기 · 베이스 바꾸기 · 리플렉션 기능)은 차례 사이에 끼운다.

1. **기믹 쪼개기(손)**: `Gimmick/Genre/*.h` 를 컴포넌트마다 파일 한 쌍으로 같은 폴더 안에서 쪼갠다(이동 도구는 파일 단위라 먼저 쪼갠다). `GenreGimmickUtil` 을 5.2 대로 흩는다.
2. **기믹 이동**: `move-source` 로 5.2 표의 자리로. 콘텐츠 프리팹 `move-asset`. 키트 범용 조각(5.3)도 같은 차례.
   ```
   py -3 -m Scripts move-source GameFramework/Runtime/Gameplay/Gimmick/Genre/LaunchPadComponent.h=GameFramework/Runtime/Gameplay/Gimmick/Movement/LaunchPadComponent.h ...
   py -3 -m Scripts move-source GameFramework/Kits/Genre/RPG/MonsterHuntRPG/SkillTree.h=GameFramework/Runtime/Gameplay/Progression/SkillTree.h GameFramework/Kits/Genre/RPG/MonsterHuntRPG/SkillTree.cpp=GameFramework/Runtime/Gameplay/Progression/SkillTree.cpp
   ```
3. **단순 개명 표 A(컴포넌트 · 그 밖의 형식)**: `Config/Rules/Renames/TypeNamingAuditA.json`
   ```json
   [
     { "old": "ScenePrimitive", "new": "PrimitiveComponent", "why": "층 이름 *Component, 언리얼 UPrimitiveComponent" },
     { "old": "ScenePrimitiveBatch", "new": "PrimitiveBatch", "why": "PrimitiveComponent 와 짝" },
     { "old": "ActivatorVolumeComponent", "new": "TriggerComponent", "why": "모양 없는 트리거 논리 — Volume 아님" },
     { "old": "ActivatorAuthority", "new": "TriggerAuthority", "why": "주인 TriggerComponent" },
     { "old": "ActivatorAuthorityRule", "new": "TriggerAuthorityRule", "why": "주인 TriggerComponent" },
     { "old": "ActivatorSpawnInside", "new": "TriggerSpawnInside", "why": "주인 TriggerComponent" },
     { "old": "ActivatorSpawnWindow", "new": "TriggerSpawnWindow", "why": "주인 TriggerComponent" },
     { "old": "TileMapRendererComponent", "new": "TileMapComponent", "why": "그리기 · 충돌 · 비용 — UPaperTileMapComponent · TileMapLayer" },
     { "old": "HorseFollowAIController", "new": "HorseFollowAIControllerComponent", "why": "컴포넌트 접미" },
     { "old": "ScheduleNPCComponent", "new": "NPCScheduleComponent", "why": "영어 어순" },
     { "old": "ScheduleNPCPhase", "new": "NPCSchedulePhase", "why": "주인을 따름" },
     { "old": "CharacterPawnMovementComponent", "new": "CharacterMovementComponent", "why": "언리얼 UCharacterMovementComponent" },
     { "old": "CharacterPawnMovement2DComponent", "new": "CharacterMovement2DComponent", "why": "위와 짝" },
     { "old": "GameModeBoundComponent", "new": "GameFrameworkComponent", "why": "Lyra UGameFrameworkComponent, Bound 는 조어" },
     { "old": "TutorialHintComponent", "new": "TutorialHintTriggerComponent", "why": "트리거 파생 이름 맞춤" },
     { "old": "AbilityComponent", "new": "AbilitySystemComponent", "why": "GAS UAbilitySystemComponent" },
     { "old": "AbilityGateComponent", "new": "TagGateComponent", "why": "태그 조건 문 — 장르 어휘 제거" },
     { "old": "ScareTriggerComponent", "new": "CueTriggerComponent", "why": "연출 신호 + 소리 트리거 — 장르 어휘 제거" },
     { "old": "SurfaceExposureComponent", "new": "CharacterWetnessComponent", "why": "업계 wetness" },
     { "old": "SurfaceExposureSystem", "new": "CharacterWetnessSystem", "why": "주인을 따름" },
     { "old": "DismembermentComponent", "new": "GoreComponent", "why": "절단 + 피 = 업계 고어 시스템" },
     { "old": "DismembermentResult", "new": "GoreCutResult", "why": "주인을 따름" },
     { "old": "GimmickWeightComponent", "new": "PressurePlateWeightComponent", "why": "읽는 쪽을 이름에" },
     { "old": "TickedPathMoverComponent", "new": "InterpToMovementComponent", "why": "언리얼 UInterpToMovementComponent" },
     { "old": "GimmickMoverMode", "new": "InterpToMovementMode", "why": "주인을 따름" },
     { "old": "GimmickItemChoice", "new": "ItemBoxChoice", "why": "주인을 따름" },
     { "old": "WorldItemComponent", "new": "PickupComponent", "why": "업계 pickup" },
     { "old": "SessionStateComponent", "new": "GameStateComponent", "why": "GameMode · GameState 짝" },
     { "old": "SessionStateSettings", "new": "GameStateSettings", "why": "주인을 따름" },
     { "old": "SessionStateInitResult", "new": "GameStateInitResult", "why": "주인을 따름" },
     { "old": "PhysicsCarComponent", "new": "WheeledVehicleDriveComponent", "why": "대상 WheeledVehicleComponent 를 따름" },
     { "old": "JumpEscapeReactionComponent", "new": "JumpEscapeComponent", "why": "Reaction 은 폴더 문맥" },
     { "old": "ScreenReactionComponent", "new": "StatusScreenEffectComponent", "why": "status effect + screen effect" },
     { "old": "VisualSinkReactionComponent", "new": "SinkVisualOffsetComponent", "why": "하는 일" },
     { "old": "ActionCombatSimulation", "new": "PlatformerCombatSimulation", "why": "ActionCombat 키트와 겹침" },
     { "old": "RTSVision", "new": "FogOfWar", "why": "업계 fog of war" },
     { "old": "FacingDir", "new": "FacingDirection", "why": "줄임말" }
   ]
   ```
   폴더 `Gameplay/Dismemberment` → `Gameplay/Gore`, `Gameplay/SessionState` → `Gameplay/GameState` 는 `move-source` 로 같은 차례에.
4. **인터페이스 이름 표 B(상태 있는 `I` 떼기)**: `Config/Rules/Renames/TypeNamingAuditB.json` — 4.2 표의 15 쌍(`IEvent` → `EventBase` … `IOnlineClientService` → `OnlineClientService`). `IWindow` · `ISubsystem` · `IEvent` 는 쓰는 곳이 많아 표 B 를 한 커밋으로 따로 둔다. 개명 도구가 같은 줄기 파일(`IWindow.h` → `GenericWindow.h`)을 함께 옮긴다.
5. **리플렉션 인터페이스(손)**: 4.3 설계를 Engine 리플렉션에 넣고(`REFLECT( Interface )` · `REFLECT_INTERFACE` · `TypeInfo` 표 · `GameObject::findInterface` · `ComponentRegistry` 칸) 단위 테스트를 둔다.
6. **역할 베이스 → 인터페이스(손 + 표 C)**: `HealthSourceComponent` → `IHealthSource`, `HealthListenerComponent` → `IHealthListener`, `GameFlagsOwnerComponent` → `IGameFlagsOwner` 로 바꾸고 구현 컴포넌트에 포트 멤버를 단다(형식이 바뀌므로 이름만 바꾸는 표로는 안 된다 — 표 C 는 이름 치환만, 구조는 손). `AutomationStepContext` → `IAutomationStepContext`, `ZoneModuleProcessor` → `IZoneModuleProcessor` 는 표 C 의 단순 쌍.
7. **층 이동(손)**: 2 절 표의 베이스 바꾸기. SceneComponent 로 올리는 것(`ZoneVolumeComponent` · `SplineComponent` · `TileMapComponent` · `ShadowCaster2DComponent` · `WidgetComponent` · `MeleeHitboxComponent`)은 저장된 씬에 트랜스폼 칸이 없어도 항등으로 읽히는지, 내리는 것(`SpriteAnimatorComponent`)은 저장된 씬의 트랜스폼 칸을 읽기가 오류로 보는지 먼저 확인하고, 오류면 같은 커밋에서 데이터를 고친다. `HousingPlotComponent` 는 `Component` + `PrefabPool` 멤버로.
8. **검증**: 묶음 끝 `preflight`. 이름이 바뀐 시나리오 · 감시 값(리플렉션 칸 이름이 든 것)은 개명 도구가 데이터까지 옮기지만, 문자열 리터럴 안은 건너뛰고 파일을 알리므로 그 목록을 손으로 본다.

### `_bannedNames` 에 오를 옛 이름

표 A: `ScenePrimitive` · `ScenePrimitiveBatch` · `ActivatorVolumeComponent` · `ActivatorAuthority` · `ActivatorAuthorityRule` · `ActivatorSpawnInside` · `ActivatorSpawnWindow` · `TileMapRendererComponent` · `HorseFollowAIController` · `ScheduleNPCComponent` · `ScheduleNPCPhase` · `CharacterPawnMovementComponent` · `CharacterPawnMovement2DComponent` · `GameModeBoundComponent` · `TutorialHintComponent` · `AbilityComponent` · `AbilityGateComponent` · `ScareTriggerComponent` · `SurfaceExposureComponent` · `SurfaceExposureSystem` · `DismembermentComponent` · `DismembermentResult` · `GimmickWeightComponent` · `TickedPathMoverComponent` · `GimmickMoverMode` · `GimmickItemChoice` · `WorldItemComponent` · `SessionStateComponent` · `SessionStateSettings` · `SessionStateInitResult` · `PhysicsCarComponent` · `JumpEscapeReactionComponent` · `ScreenReactionComponent` · `VisualSinkReactionComponent` · `ActionCombatSimulation` · `RTSVision` · `FacingDir` · `GenreGimmickUtil`

표 B: `IEvent` · `ISubsystem` · `IWindow` · `ISplashWindow` · `IEditorPanel` · `IEditorDocumentPanel` · `IEditorPopup` · `IAudioEffect` · `IFileWatcher` · `IAllocationTracker` · `IDataInheritanceSource` · `IServiceStore` · `IServiceStoreJob` · `IAccountPresence` · `IOnlineClientService`

표 C: `HealthSourceComponent` · `HealthListenerComponent` · `GameFlagsOwnerComponent` · `AutomationStepContext` · `ZoneModuleProcessor`

### 통합 담당과 맞출 점

- 다른 갈래가 위 옛 이름을 새로 쓰고 있으면 병합 뒤 `rename-symbol --reapply` 로 다시 돌린다. 시작 전에 열린 갈래 목록을 받는다.
- 표 A · B 는 `--dry-run` 으로 먼저 돌려 문자열 리터럴 안이라 건너뛴 파일과 새 이름 겹침 목록을 본다. 새 이름은 2026-11-21 main 에 없는 것을 확인했다(`PrimitiveComponent` · `TriggerComponent` · `GameStateComponent` · `GameFrameworkComponent` · `CharacterMovementComponent` · `PickupComponent` · `GoreComponent` · `GenericWindow` · `EventBase` 등). 다른 갈래가 같은 이름을 새로 만들었으면 병합 뒤 겹침으로 멈춘다.
- 4.3 은 다중 상속을 쓰지 않으므로 CppStyle 규칙 예외가 필요 없다. 다중 상속 인터페이스(언리얼 모양)를 원하면 그때 `CppShape.json` `_multipleBase` 에 "Component + 상태 없는 I" 한 줄과 이유가 필요하다 — 이 문서의 판단은 포트 멤버다.
- 근본 원인: 낱말 표(naming-words)는 낱말마다 있는지만 봐서 "낱말은 다 업계 말인데 조합이 상용 어휘가 아닌" 이름(`ActivatorVolume` · `SurfaceExposure` · `GameModeBound`)과 "인터페이스인데 `I` 없음"(리플렉션 탓에 베이스로 만든 역할)을 막지 못했다. 앞의 것은 이 감사를 개명 단계마다 되풀이하고, 뒤의 것은 4.3 의 `REFLECT( Interface )` 가 형식으로 막는다(상태가 붙으면 컴파일 오류, 역할을 베이스로 만들 까닭이 사라짐).
