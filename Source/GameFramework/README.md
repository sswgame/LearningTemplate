# GameFramework (장르 공통 뼈대)

여러 게임에서 반복적으로 사용되는 장르별 공통 로직이나 키트(Kits)가 모여있는 곳입니다.

App은 이 라이브러리를 링크하지 않습니다. 게임플레이 입력은 `Engine/Input/InputMap.h`의 인스턴스를 사용합니다.

## 하위 폴더 구성

최상위에는 `Base/`(기반) · `Kits/`(키트)와 루트 파일(`GameFrameworkExports.h` · `GameFrameworkMinimal.h` · 매니페스트 · `CMakeLists.txt`)만 둡니다 —
다른 폴더가 생기면 `CheckGameFrameworkLayers` 가 실패합니다.

`GameFramework` 타겟(= 모든 키트가 깔고 앉는 기반, `Base/<폴더>`):

- **Ability**: 언리얼 Gameplay Ability System 과 같은 어빌리티 시스템 — `AbilitySystemComponent`(어트리뷰트 · 이펙트 · 어빌리티 · 태그 개수),
  `AttributeSet` · `CombatAttributeSet`, `GameplayEffectDef` · `GameplayEffectSpec`(즉시 · 지속 · 무한 · 주기 · 스택 · 실행 계산), `GameplayAbility`
  (태그 조건 · 비용 · 쿨다운 · 트리거 · 입력) · `AbilityTask`, XML 카탈로그(`AbilityCatalog`). 장르를 가리지 않아 키트가 아니라 기반에 있습니다(턴제는
  틱을 끄고 턴마다 `advanceTime( 1 )`). 체력 변화는 같은 오브젝트의 `HealthListenerComponent`(HP 바)에 알리고 피해는 `DamageNumberComponent` 로 띄웁니다. 자세한 것은 `Ability/README.md`,
  쓰는 예는 `Source/Games/AbilityArena`
- **Framework**: 게임 모듈의 수명과 배선 — `IGame`, `GameInstanceBase`, 서비스 로케이터(`GameService`), 다국어 창구(`GameStrings` — 엔진 `LocalizationManager` 를 게임 서비스로 부른다), 세이브 베이스(`SaveGame`),
  "game" 채널 이벤트(`GameEvents.h` · 내는 길 `GameEventUtil`), 화면 전환(`ScreenTransitionManager`), 소리(`GameSound` — 이벤트 라이브러리 올리기 · 내리기, 2D 이벤트 `postEvent`, 월드 자리 원샷 `postEventAt`(한 번 쓰는 에미터), 클립 `play( path, bus )`;
  따라 움직이는 · 루프 소리는 엔진의 `AudioEmitterComponent`, 자세한 것은 `Source/Engine/Audio/README.md`). `GameInstanceBase` 가 세이브 · 로드 완료와 씬 로드 요청 · 완료를 그 자리에서 낸다. `onInitialize` 뒤에 사용자 설정을 다시 넣는다(`UserSettingsManager::reapplyAll` — 언어 · 입력 맵이 그때 선다). 공유 타입은 루트의 `GameFrameworkMinimal.h`.
  PROPERTY 가 아닌 컴포넌트 상태(디렉터가 든 키트 시뮬레이션)는 `ComponentStateStore` 가 상태 봉투의 세 번째 섹션으로 실어 핫 리로드 · 세이브를 넘긴다 —
  게임 인스턴스가 생성자에서 `registerDirector<T>()`(상태 + 걷기) · `registerStatefulComponent<T>()`(상태만) · `registerViewOwner<T>()`(걷기만) 한 줄로 올리면
  저장 전에 모두 싣고 세운 것을 걷으며 복원 뒤 돌려준다(그 뒤에 `onBeforeStateSerialize` · `onAfterStateDeserialize` 훅이 돈다).
  컴포넌트는 `writeState( Archive& )` · `restoreState( vector<uint8>&& )`(시작 전이면 들고 있다가 `onBeginPlay` 에서 적용)를 둔다. 같은 실행은 컴포넌트 id,
  다른 실행의 세이브는 타입 안 순서로 짝짓는다(언리얼 `UObject::Serialize` · 유니티 `ISerializationCallbackReceiver` 의 자리). 살아 있는 씬 위에 다시 선
  인스턴스(핫 리로드 · 백엔드 교체)는 `requestFirstScene` 이 아무것도 하지 않는다 — 되살린 씬을 첫 씬이 덮지 않게.
  **세이브 틀은 하나**: 게임 상태(진행 · 세계 — 공유 상태 · 디렉터 · 키트 상태 바이트)는 스냅숏 봉투(`GameInstanceBase::saveStateToFile` · `loadStateFromFile`)로만 저장한다.
  `SaveGame`(바이트 쓰기 · 읽기 순수 가상 + 슬롯 API `saveToSlot` · `loadFromCompletion`)과 `SaveGameSerializer`(리플렉션 객체 하나 → 바이트, 머리 없음 — 판 · 체크섬은
  로컬 저장 봉투)는 **사용자 파일**(외형 프리셋 `UserAppearancePresetStore` · 키 바인딩 · 옵션)만. 옛 SAV1 파일은 읽지 않는다.
  디렉터 베이스(`GameDirectorComponent` — 언리얼 `AGameModeBase` · `AGameStateBase` 자리): 틱 그룹(PrePhysics) · 상태 바이트 보류와 적용 · 틱 뒤 플러시
  (`executeOrDeferPostTick` 한 번 → `onFlush`) · 세운 것 걷기(`spawnPrefab` · `trackSpawned` · `despawnViews`) · 자동 플레이(`_bAutoPlay` · `GameAutoplay`) ·
  디렉터 찾기(`resolve<T>`)를 들고, 게임은 `startGame` · `readState` · `tickGame` · `onFlush` 만 적는다(쓰는 법은 `Source/Games/README.md`). 틱 안에서 쌓아
  틱 뒤에 내는 소리는 `GameSoundQueue`, 색만 다른 모습은 `MaterialTintCache`(색 하나에 머티리얼 인스턴스 하나).
  자동 저장 정책(`AutosaveManager` · `AutosaveSettings` — `<Autosave>` XML): 간격 · 지역 이동 · 체크포인트 · 보스 앞 · 종료 까닭, 요청을 한 update 에 하나로 합치고
  더 중요한 까닭을 남김, 최소 간격 · 막기(전투 · 연출 — 기다렸다 저장, 종료만 무시), 돌림 칸(가장 새 칸을 건드리지 않고 다음 칸에 쓴 뒤 `.info` 기록 —
  파일 쓰기는 `FileUtil::writeFile` 이 원자적), 다른 실행의 칸 기록을 읽어 순번을 잇기, `restoreLatest` · `restoreCheckpoint`. 저장 · 불러오기는 게임이
  넘긴 델리게이트(보통 `saveStateToFile` · `loadStateFromFile`)이고 UI 는 없다. 씬에는 체크포인트 · 보스 앞 · 지역 경계 볼륨
  `AutosaveTriggerComponent`(태그가 맞는 활성자가 트리거에 들면 게임 서비스 `AutosaveManager` 에 까닭과 이름을 넘긴다, 한 번)를 놓는다
- **World**(씬 컴포넌트): 장르 무관 씬 컴포넌트 — `FadeOutComponent`, `GravityComponent`, `DontDestroyOnLoadComponent`, 장식 흩뿌리기
  (`PropScatterComponent` — 씨앗 고정 배치를 영역 가장자리 · 안쪽 격자 · 배치 규칙(`Rules` 모드 — Engine `Environment/Placement` 의 `PlacementRule`: 밀도 · 최소 거리 ·
  경사 · 높이 · 레이어 필터, 영역 아래 지형 위)에, 제외 원, 플레이 시작에 세우고 끝에 걷는다. 그릴 것만이면 GPU 인스턴스로 그리는 Engine `FoliageComponent`, 계산은 `PropScatterMath`).
  아래 **World** 절의 시계 · 날씨 · 지역 그래프 · 플래그 · 질의와 같은 폴더다
- **Camera**(카메라 컴포넌트): 비스듬히 내려다보는 직교 카메라
  (`OrthoCameraRigComponent` — 입력 맵 액션 `_panAction`(기본 `Camera.Pan`, 2D 벡터) 이동(초점 범위 묶기), 휠 확대(`setOrthoHeight` 도 같은 범위), `_rotateAction`(기본 `Camera.Rotate`, 1D 축) 90° 회전(단계 0 이면 끈다), 다른 컴포넌트가 앞 틱 그룹에서 넣는 원근 시점 덮어쓰기, 화면 점 → 땅 점 `findGroundPoint`(마우스 고르기)). 계산은 `OrthoCameraRigMath` 로
  떼어 씬 없이 시험한다. 1인칭 카메라(`FirstPersonCameraComponent` — 마우스(또는 입력 맵 액션 `_lookAction`) 시점 · 피치 한계 · 마우스 잠금(Esc) · 눈 자리(카메라의 부모 공간) · 손에 든 뷰 모델 자리, 계산은
  `FirstPersonCameraMath`). 시점 자체는 `Input/FirstPersonLook` 이고, 몸을 움직이는 게임 컴포넌트가 같은 오브젝트의 뒤 그룹에서 시점을 읽고 눈 자리를 넣는다.
  XY 평면 2D 씬의 따라가기 · 흔들림은 `Follow2DCameraComponent`(목표 자리 · 따라가는 비율 · 감쇠 흔들림)
- **Camera**: 데이터 카메라 — 프리셋(`CameraPresetDef` · `CameraPresetCatalog`), 모드 계산(`CameraMode` — 입력 · 제약 · 프레이밍 · 스프링 암 · 훑기),
  흔들림(`CameraShake` — 펄린 손떨림 · 충격), 암 충돌 질의(`ICameraCollisionProbe`), 포즈 섞기(`blendPoses`, 곡선은 엔진 `BlendCurveSpec` · `evaluateBlendWeight`) · 블렌드 진행(`CameraPoseBlender`), 상태 기계
  (`CameraDirector`), 그것을 카메라에 쓰는 `CameraDirectorComponent`, 플레이어마다 뷰 타깃을 바꾸는 `CameraManagerComponent`. 아래 "카메라" 절
- **Utility**: 장르 무관 계산 도구 — 씨앗 고정 난수 · 좌표 해시(`GameRandom` · `GameHash` — 가중치 고르기 `pickWeightedIndex` · 섞기 `shuffle`), 값 노이즈(`ValueNoise`),
  광선 판정(`RayMath` — 구 · 상자 · 캡슐 · 바닥 평면 · 원뿔), 앞 · 위 → 오일러(`OrientationUtil` — 롤이 있는 차량 · 카메라), 2D 네 방향(`FacingDir`),
  고정 스텝 누적기(`FixedStepTimer`), 게임 시간 타이머(`TimerQueue`), 쿨다운 · 지속 시간 · 반복 간격 값(`Countdown` — `tick` 이 끝난 걸음에 한 번 true,
  반복은 그 걸음에 `restart` 로 다시 걸어 지나친 몫을 한 간격까지 잇는다), 초당 비율 → 정수 발생(`RateAccumulator` — 손님 도착 · 운영비),
  수명 계산(`LifeSpanUtil` — 흐른 시간 · 수명으로 "다했나" · 흐림, 페이드 · 데미지 숫자 · 투사체),
  시뮬레이션 알림 버퍼(`EventBuffer<T>` — `drainEvents` 의 몸통 `drainTo`, 빈 목록이면 저장소를 맞바꿔 프레임마다 할당 · 복사하지 않는다),
  칸 격자의 모양(`GridTopology` — 칸 번호 · 경계 · 이웃 순서 하나: 직교 넷 → 대각선 넷, 내비 · 원소 격자 · 키트가 같은 표)과 너비 우선 탐색 ·
  "한 칸 한 번" 표시 스크래치(`GridSearchScratch` — 세대 번호로 비워 호출마다 W × H 를 잡거나 지우지 않는다),
  시뮬레이션 상태 바이트의 공통 모양(`StateArchiveUtil` — 머리(표 · 버전) · 이름 ·
  남은 바이트로 상한을 둔 개수 · 난수 · 걸음 타이머 · 구간 — 표 · 판 · 길이). 셋 이상의 키트에 같은 것이 따로 있던 것을 모았다(아래 "새 장르 키트").
- **GameState**: 키트 여럿을 한 게임에 섞을 때 나눠 쓰는 판 상태 — `GameStateComponent`(지갑 · 플래그 · 시계 · 퀘스트 일지 · 평판, 시계 알림을 틱마다 읽기 전용 목록으로).
  키트 디렉터들과 **같은 오브젝트, 맨 앞**에 두고, 상태 바이트는 상태마다 구간(`StateArchiveUtil::writeSection`)이다. 키트는 `GameStateRefs`(`Framework/`)로 빌린다. 아래 "키트 여럿을 한 게임에" 절
  키트 시뮬레이션의 `writeState` · `readState`(`FarmField` · `CitySimulation` · `RtsWorld` · `ThemeParkSimulation` · `VoxelWorld` …)는 임시에 읽어 끝까지 맞을 때만
  바꾸고, 정의는 카탈로그 id 로, 유닛 참조는 세대 든 id 로 적으며, 다시 만들 수 있는 것(길 · 격자 발자국 · 흐름장)은 적지 않는다.
  키트 상태의 `readState` 는 `Archive&` 하나만 받는다 — 빌린 것(카탈로그 · 파티)은 읽기 전에 `initialize` · `bindCatalog` · `bindParty` 로 묶는다
  (언리얼 SaveGame 처럼 직렬화는 균일하고 참조는 id 로 다시 잇는다). 키트 안에 든 기반 값 타입(`MoveTimeline`)만 주인이 묶은 카탈로그를 인자로 받는다.
- **Input**: 커맨드 입력(`InputCommandParser` — 철권 표기 · `InputCommandBuffer` — 새로 넣기 · 누른 채 · 동시 버튼 · 틱 한도 · 좌우 뒤집기 · 상태 바이트, 결정적),
  타이밍 판정(`TimingJudge` — 리듬 · 타이밍 공격 · 스킬 체크 · 저스트 프레임), 1인칭 시점(`FirstPersonLook`)
- **Data**: 데이터를 읽고 담는 틀 — `GameSettings`, 경로마다 한 번 읽어 나눠 쓰는 표의 캐시(`GameDataCache<T>` — 게임 서비스가 묶이면
  에셋 캐시 등록부에 올라 에디터 핫 리로드가 새 표로 바꾸고 `getReloadCount` 를 올린다, 옛 표는 모듈이 내릴 때까지 산다), 데이터 XML 읽기(`GameDataXml` — 문서 · 루트 · id 확인 · 숫자 목록 · 토큰 목록,
  다른 카탈로그를 함께 받는 루트 읽기용 로더 템플릿 `loadFile` · `loadText`), 카탈로그 베이스(`XmlCatalog<T>` — 물려받으면 `loadFromResource` ·
  `loadFromXmlText` 가 생기고 카탈로그는 `kXmlRootName` 과 비공개 `loadRoot` 만 둔다, 읽은 수 0 · false 는 실패), id 카탈로그(`GameCatalog<T>` —
  읽은 순서 + 해시 조회), 이름 → 수치(`StatBlock` — 여러 자원 비용 `canAfford` · `trySpend`), 시간 → 값 꺾은선(`GameCurve` — 스폰 · 페이싱 곡선)
- **AI**: 블랙보드(`Blackboard`), 행동 트리(`BehaviorTree` 정의 · `BehaviorTreeRunner` 실행 — 반응형 셀렉터 · 관찰 중단 · 데코레이터), 감각(`AiPerception` — 시야 각 ·
  거리 · 가림 · 소리 · 기억), 스폰 감독(`SpawnDirector` — 시간에 따라 쌓이는 예산 · 곡선 · 종류 상한 · 태그), NPC 하루 일정(`AI/Schedule` — `*.schedules.xml` 루틴 ·
  조건 · 우선순위 · 축제 · 약속, 일찍 나서기 · 끼어들기 스택 · 화면 밖 LOD · 잠 · 저장 · 네트워크 요약 · "왜 여기 있나" 추적, 2D · 3D 공통 — `AI/Schedule/README.md`),
  페이싱 감독(`AI/Director` — `*.director.xml` 긴장도 모델 · 쌓기/절정/쉼 단계와 곡선 · 단계별 스폰 예산 · 조우/보상 가중 풀(단계 진입 · 주기 · 예산) · 쿨다운 ·
  문맥 조건 · 보상 밀도 · 결정성 · 추적 — `AI/Director/README.md`)
- **Combat**: 무기 정의 · 상태(`WeaponCatalog` · `WeaponState` — 연사 · 탄창 · 재장전 · 퍼짐 · 산탄 · 거리 감쇠 · 머리 배율 · 탄속 · 탄 아이템), 탄 퍼짐(`WeaponMath`),
  체력 원천 · 신호(`HealthSourceComponent` — 어빌리티 시스템 · 키트 유닛 스탯 · 게임 적이 상속, 읽기 `getHealthReading` · 알림 `notifyHealthChanged` 한 곳 / `HealthListenerComponent` · `HealthChangedEvent` — 같은 오브젝트의 받는 쪽(HP 바)), 피해 공식(`DamageMath`), 탄도(`Ballistics` — 낙차 · 발사각 · 앞 겨누기), 턴 순서(`TurnOrder` — 라운드제 · 타임라인제), 록온(`LockOnSelector`),
  체력 상태(`Vitality` — 실드 · 기절 → 출혈 → 부활 · 최대 기절 횟수 · 무적 · 경직 게이지 · 최대 체력 바꾸기), 자원 게이지(`ResourceGauge` — 스태미나 탈진 · 과열 · 회복 배율 · 즉시 깎기),
  프레임 데이터(`MoveCatalog` · `MoveTimeline` — 발생 · 지속 · 경직 · 캔슬 · 히트스톱 · 가드 높이 · 상태 복원), 속성 상성(`ElementChart` — 복합 속성 곱 · 면역 · 상태이상 확률)
  슈터 · 배틀로얄 · 서부극 · 기체 대전 · JRPG · 포켓몬 · 젤다가 함께 쓴다(예전 `GF_Shooter` 키트의 무기는 여기로 옮겼다)
- **Inventory**: 아이템 값 목록(`ItemStackList` — 원소 `ItemStack` = id · 개수: 레시피 재료 · 전리품 · 보상 · 출하 대기, 칸이 없다 — 들고 있는 가방은 `Inventory`), 아이템 카탈로그(`ItemCatalog` — 분류 · 겹침 · 무게 · 희귀도 · 장비 칸 · 내구도 · 태그 · 능력치), 칸 인벤토리(`Inventory`), 격자 가방(`GridInventory` — w × h · 돌리기 · 빈자리 찾기 · 겹침, 모양은 연결 함수), 장비(`Equipment` — 칸 배치는
  데이터, 장착 조건 `<Requires>` 와 깨질 때의 정책 · 아이템 인스턴스 상태 — 꾸미기 값 · 피해 · 떨어진 부품), 전리품 표(`LootCatalog` — 가중치 · 없음 · 늘 주기 · 표 안의 표 · 행운), 제작(`RecipeCatalog` · `Crafter` — 작업대 · 레벨 · 도구 · 배우기 · 대기열 · 재료를 거두는 쪽 바꾸기),
  지갑 · 가게(`Wallet` · `ShopCatalog` · `ShopState` — 여러 통화 · 재고 · 재입고 · 매입 시세 하락과 회복 · 조건은 `IShopConditionEvaluator`)
- **Appearance**: 캐릭터 외형 데이터와 해석 — 슬롯 표 · 장비 세트 · 아이템 외형(부품 · 상태 · 피해 단계) · 꾸미기 스키마(캐릭터 · 아이템 공용) ·
  외형 규칙 · 프리셋(`CharacterAppearance`), 순수 해석기(`AppearanceResolver` — 결과 해시가 캐시 키), 외형 상태(`CharacterAppearanceState`),
  공유 코드 · 플레이어 프리셋 세이브 · 네트워크 동기화(`AppearanceSelection`), 그리고 그것을 오브젝트로 조립하는 외형 컴포넌트(`CharacterAppearanceComponent` —
  몸 메시 · 소켓 부착 부품 · 염색, 소켓 이름 공간 `AppearanceSocketRig`). 2D 스프라이트와 3D 메시가 같은 길이다. 자세한 것은 `Appearance/README.md`
- **Match**: 판 규칙(`MatchState` — 팀 · 역할 · 점수 · 도움 · 부활 대기 · 코스트 게이지 · 탈락 순위 · 시간 제한 · 목표로 끝내기), 라운드 묶음(`RoundSeries` — 순위 점수(비면 1 위 1 점 = 선승) · 목표 점수 · 동점 규칙(무승부 · 서든 데스) · 정수 걸음 라운드 시간 · 라운드 사이 대기 · 상태 바이트, 알림 대신 결과를 돌려준다), 팀 태도(`TeamAttitudeUtil` — 같은 팀 아군 · 다른 팀 적 · 팀 없음 중립, 언리얼 `ETeamAttitude`)
- **Movement**: 2D 플랫포머 몸(`PlatformerMotor2D` · `PlatformTileMap` — 점프 높이 · 짧은 점프 · 코요테 · 미리 누르기 · 벽 점프 · 대시 · 다단 점프 · 한쪽 발판 · 사다리),
  아케이드 차량(`ArcadeVehicleMotor` — 속도에 따른 조향 · 드리프트 미니터보 단계 · 니트로 · 오프로드 · 점프, 지면은 `IVehicleGround`),
  보는 쪽 기준 이동 방향(`LocomotionMath` — 서기 · 앞 · 뒤 · 옆걸음 · 공중, 애니메이터 이동 상태의 입력)
- **Navigation**: 격자(`NavGrid`), A*(`GridPathfinder`), 흐름장(`FlowField`), 걷는 행위자(`NavAgent` · `Steering`), SRPG 이동 범위(`GridReachability`)
- **Online**: 온라인 서비스(계정 · 경제 · 거래 · 채팅 키트)가 같이 쓰는 기반 계약 — 키트끼리는 include 하지 못하므로 여기 둔다. 층 1(Core 만 본다).
  `Online/Store`: 서비스 저장 계약 `IServiceStore`((표, 키) → 바이트 + 판, 조건부 쓰기를 묶은 트랜잭션 · 키 범위 읽기, 저장 왕복을 `IServiceStoreWork` 로 맡기고
  `pollCompletions` 로 거둔다 — 게임 · 네트워크 스레드는 DB 를 기다리지 않는다), 멱등 기록(`ServiceIdempotency`), 키 도우미(`ServiceKeyUtil` — 키는 ASCII,
  숫자는 고정 16 자리 16 진수), 메모리 구현(`MemoryServiceDatabase` 데이터 · 실패 주입 + `MemoryServiceStore` 앞 — 시험 · 개발 서버). 판은 저장소 전체에서 오르는 수라 ABA 가 없다.
  SQL 구현은 키트 `GF_SqlStore` · `GF_Server_SqlStore`.
  `Online/Guard`: 요청 보호 — `TokenBucket`(정수 · 밀리초, 버스트 · 조각 시간 이월) · `TokenBucketMap`(키마다, 상한에서 가득 찬 것을 지움) · `RequestLimits`.
  `Online/Cache`: 휘발성 저장 계약 `IEphemeralStore`(만료 키-값 · 원자 증감 · 비교 후 쓰기 · 정렬 집합 · 발행/구독, 요청을 맡기고 답을 거둔다 — 잃어도 되는 것만,
  정본은 `IServiceStore`) + 메모리 구현(`MemoryEphemeralDatabase` 시계 고정 · 게으른 만료 + `MemoryEphemeralStore` 앞). RESP 구현은 키트 `GF_Server_CacheStore`.
  `Online/Audit`: 지울 수 없는 감사 줄(`ServiceAuditLog` — 효과와 같은 트랜잭션에 "없어야 함" 으로, 상한을 넘으면 효과까지 Invalid). `Online/Bus`: 서버 간 알림
  (`IServerBus` — 주제 발행/구독 · 최대 한 번, `EphemeralServerBus` 캐시 위 · `LocalServerBus` 프로세스 안). `Online/Schedule`: 예약 작업(`ServiceScheduler` — 일일 · 주간 · 기간,
  회차를 저장소 조건부 쓰기로 차지해 서버 여럿 중 하나만, 지나친 회차는 최근 하나, 임대가 지나면 이어받기). `Online/Config`: 원격 설정 · 기능 플래그(`RemoteConfig` —
  저장소 정본 · 판 조건 바꾸기 + 감사 · 버스 알림, 계정 해시 출시 비율, 클라이언트 묶음 · 해시). 관측(지표 · 구조화 로그 · 요청 추적 id)은 서비스 틀의 요청 문맥이 자리다.
  `Online/Identity`: 신원 원형 — `AccountId` · `AccountIdentity` · `IAccountDirectory`(이 프로세스에 붙어 있는 계정, 발급은 계정 키트) ·
  `IAccountPresence`(서버 여럿의 접속 상태 — 계정 id · 표시 이름으로 붙은 서버 찾기, 결과는 맡긴 델리게이트로 한 번 · 다른 서버 계정에게 알림) ·
  `IAccountNameIndex`(오프라인 계정을 저장소에서 이름 · id 로 — 부르는 키트의 저장소 일 안에서). 셋 다 계정 키트가 구현하고 다른 키트는 창구만 본다.
  `Online/Service`: 서비스 틀 — 호스트(`OnlineServiceHost` — 스트림 끝점 · 요청 서버, 연결의 첫 요청 Hello 로 기반 · 키트 판 협상, 인증(계정 바인딩 — 익명 메서드만 로그인 없이) ·
  주소 · 계정 토큰 버킷 · 몸 상한을 먼저 보고 영역을 맡은 `IOnlineService` 로, 계정 ↔ 연결 표로 알림 `sendPush` · `sendPushToAll`, 계정이 떠나면 서비스들에 `onAccountLeft`,
  캐시 답 · 채널 메시지는 `EphemeralStoreRouter`(`Online/Cache` — 주인이 먼저 내려가면 `cancel`)로, 버스 메시지는 `subscribeServerBus` 한 서비스로 나눠 준다 — 호스트가 앞 · 버스의 유일한 소비자),
  와이어 표(`OnlineProtocol.h` — 키트마다 메서드 영역 256 칸: 메서드 영역 + 0x00..0x7F · 알림 영역 + 0x80..0xFF, 공통 오류 코드), 클라이언트(`OnlineServiceClient` —
  Hello · 모은 요청 · 알림 나누기 · 다시 연결(물러남), 자기 끝점 모드와 공유 끝점 모드(부하 시험 봇 — 끝점 하나에 연결 여럿)), 키트 바인딩 · 클라이언트 도우미
  (`ServicePendingTable` — 로직에 맡긴 요청의 꼬리표 ↔ 토큰, `ServiceClientCallTable` — 요청 id ↔ 사용자 델리게이트, `send` 안에서 바로 온 실패도 잃지 않는다).
  시험: `OnlineServiceHostTest` · `OnlineServiceTest` · `EphemeralStoreRouterTest` · `ServicePendingTableTest` · `ServiceClientCallTableTest`. 키트 끝단 시험 하니스는
  `Test/EngineTest/GameFramework/Online/OnlineHostTestUtil.h`(루프백 서버 · 공유 끝점 클라이언트 묶음 · 시험 로그인 · 가짜 접속 상태).
  `Online/Observability`: 서비스 표준 지표 묶음(`ServiceMetrics` — `service_requests_total{service,method,result}` · `service_request_seconds` ·
  `service_store_pending`). 등록부는 엔진 `Engine/Observability/MetricRegistry`(전용 서버 실행 파일이 하나 들고 넘긴다 — null 이면 세지 않는다).
  `Online/Local`: 클라이언트 로컬 저장 계약 `ILocalStore`(슬롯 `save/slot0` → 바이트, 슬롯 하나 단위 원자 쓰기 · 묶음 나열, 맡기고 거둔다 — 서버 계약과 따로 좁게),
  모든 저장소가 같은 봉투(`LocalSlotEnvelope` — `SWLS` 머리 · 형식 판 · 코덱 · 봉인 None(CRC32) / Authenticated(AEAD 태그) / Encrypted(AEAD), 키 표시가 다르면 WrongKey),
  바닥 `ILocalSlotStorage`(파일 `FileLocalSlotStorage` — 임시 파일 → 이름 바꾸기 · 띄울 때 찌꺼기 지우기, 메모리 `MemoryLocalDatabase` — 쓰기 도중 꺼짐 주입) 위의 앞 둘
  (`ThreadedLocalStore` 전용 스레드 하나 · `MemoryLocalStore` 그 자리 실행), 장치 키(`LocalDeviceKeyProvider` — Windows DPAPI · 리눅스 0600, 실수 · 가벼운 변조 막기만),
  공장(`LocalStoreFactory` — file · memory, 키트가 올리는 이름: `GF_SqlStore` 의 sqlite). 경로는 Core `UserDataPath`(사용자 설정과 같은 한 곳).
  시험: `LocalStoreMemoryTest` · `LocalStoreFileTest` · `LocalStoreSqliteTest`(같은 계약 아홉 `LocalStoreContract.h`).
  `Online/Http`: 최소 HTTP/1.1(`HttpClient` — 맡기고 거두는 요청 · 연결마다 하나 · 호스트마다 TLS 컨텍스트 · 시한 · 몸 상한 · chunked, `HttpServer` — 루프백 리다이렉트 ·
  시험 서버, `HttpMessageParser` · `HttpUrl` · `HttpUtil` 폼/퍼센트). 바깥 HTTPS(외부 로그인 JWKS · 토큰 · 프로필, 영수증 검증, 푸시)용 — 호스트 이름 해석은 아직 없다
  (IPv4 · localhost). 시험: `HttpTest`.
  `Online/Ledger`: 원장 — 계정 · 맡김 · 발행 · 소각 보유자 사이의 복식 이동(다리 전부 또는 없음), 잔액은 판 조건 레코드, 분개 키가 멱등 키, 보유자별 내역,
  보존 검사(`LedgerAudit` — 보유 = 발행 − 소각). 환불 회수만 계정 잔액을 음수(빚)로 만들 수 있고(`_bAllowDebt`), 빚이 있는 동안 그 재화는 쓰지 못한다.
  거래 · 우편 · 상점 · GM 지급이 `Ledger::stageTransfer` 로 자기 트랜잭션에 붙인다. 시험: `LedgerTest`.
  `Online/Mail`: 우편 넣기(`ServiceMail::stageSend` — 발행 재원은 수령 때, 그 밖은 넣을 때 맡김으로, 같은 멱등 키 한 번, 만료 색인, 기본 보관 30 일 ·
  만료 때 운영 · 보상 우편은 첨부 소멸 · 플레이어 우편은 반환). `Online/Sanction`: 계정 제재 레코드(채팅 금지 · 정지 · 영구 정지 — 끝 시각 · 사유, 판 조건 쓰기).
  `Online/Mail/ServiceMailCampaign.h`: 전체 우편(캠페인 레코드 하나 + 계정마다 수령 표식 "없어야 함" — 계정마다 행을 만들지 않는다, GM 키트가 만들고 우편함 키트가 끼운다).
  `Online/Identity/AccountSessionControl.h`: 세션 끊기 창구 `IAccountSessionControl`(계정 키트가 구현). 시험: `ServiceMailTest` · `ServiceSanctionTest`.
  `Online/Directory`: 서버 등록 · 고르기 — 기록 형식 `ServerRecord`(캐시 `sd/srv/<id>` 시한 15 초 · 종류 색인 `sd/idx/<종류>` 정렬 집합), 등록 `ServerRegistration`
  (5 초 하트비트, 상태 · 부하가 바뀌면 바로, 정상 종료 때 지움), 읽기 캐시 `ServerRegistryReader`(색인 → 기록을 읽어 스냅숏 교체, 시한으로 사라진 서버의 색인 멤버 정리,
  고른 몫을 다음 읽기까지 얹음, 내릴 때 기다리던 캐시 요청 취소), 고르기 `ServerSelection`(열림 · 종류 · 판 · 살아 있음 · 자리 → 같은 지역 → 찬 비율 → id, 순수 함수) —
  클라이언트 배정(GF_ServerDirectory)과 매칭의 전용 서버 배정(GF_Matchmaking)이 같이 쓴다. 시험: `ServerSelectionTest` · `ServerRegistryTest`.
- **Progression**: 경험치 곡선 · 레벨(`ExperienceCurve` · `LevelProgress`), 스킬 트리(`SkillTreeCatalog` · `SkillTreeState`), 평판 · 호감도(`ReputationCatalog` ·
  `ReputationState`), 로그라이트 지도(`RunMap`), 로컬 통계(`StatCatalog` · `PlayerStats` — `<Stats><Stat id kind="Counter|Max|Min|Time" max/>` 정의, `increment` ·
  `submit`(기록이 좋아질 때만) · `addTime`, 바뀔 때만 듣는 쪽에 `StatChange`, 프로필 파일 `saveToFile` · `loadFromFile` — 업적의 바탕, Steam Stats 의 로컬 판)
- **Quest**: 퀘스트(`QuestCatalog` · `QuestLog` — 선행 · 레벨 · 단계 · 목표 · 선택 목표 · 분기 · 보상 알림 · 시간 제한 · 반복)
- **World**: 시계(`WorldClock` — 시 · 때 · 날 · 계절 · 해 · 햇빛 · 잠), 날씨(`WeatherCatalog` · `WeatherSystem` — 계절 가중치 · 섞기 · 예보),
  방 · 지역 그래프(`AreaGraph` — 잠금 조건 · 일방통행 · 발견 · 탐색률 · 막힌 경계 · 코드로 짓기 · 다른 XML 안에 적기),
  월드 플래그와 조건식(`GameFlags` — `a && !b || count>=3`), 광선 · 시야 질의(`WorldQuery` — 물리 백엔드 서비스 또는 `PhysicsWorld` 폴백),
  공유 땅(`LandRegistry` — 칸마다 쓰는 키트 · 막힘, 얻기는 사각 전부이거나 아무것도, 주인은 이름으로 세이브; 키트는 `LandBinding` 으로 제 칸 좌표로 얻고 놓는다)
- **Interaction**: 상호작용 — 데이터 정의(`InteractionCatalog` — 누름 · 누르고 있기 · 연타 · 단계 · 거리 · 시야각 · 시야 · 쿨다운 · 태그 조건 · 맞춤 마커 · 강조 · 권한),
  고르기(`InteractionSelector`) · 진행(`InteractionSession`, 진행형 `InteractionProgress` — 여럿 · 끊김 · 퇴행 · 스킬 체크), 컴포넌트(`InteractableComponent` ·
  `InteractorComponent` · `SmartObjectComponent` · `GrabberComponent`), 권한 훅(`IInteractionAuthority`). 2D · 3D 공용. `Interaction/README.md`
- **Gimmick**: 데이터로 배선하는 레벨 장치 — 센서 · 연산자(AND · OR · NOT · 카운터 · 래치 · 지연 · 시퀀스) · 액추에이터(문 · 무버 · 엘리베이터 · 회전 · 스포너 ·
  위험 지대 · 빛 · 소리 · 켜기) 노드 등록부(`GimmickNodeRegistry`), 검증 · 고정 스텝 · 상태 바이트 회로(`GimmickCircuit`), 씬 컴포넌트(`GimmickCircuitComponent` ·
  `GimmickSensorComponent`), 원소 상호작용 규칙표(`ElementRuleTable` · `ElementGrid` — 기본표 `common/data/elements/default.elements.xml`), 장르 기믹 세트(`Genre/` — 플랫포머 · 어드벤처 · 슈터 · 레이싱 · 공포 · 잠입 · 메트로배니아 · RPG, 프리팹 `common/prefabs/gimmicks`). 2D · 3D 공용. `Gimmick/README.md`
- **Spline**: 곡선(`SplinePath` — Catmull-Rom · 3차 베지어 · 꺾은선, 호 길이 매개변수, 가장 가까운 점, 고른 간격 샘플)과 씬 컴포넌트(`SplineComponent`),
  누적 거리 표 계산(`ArcLengthUtil` — 코스터 트랙도 쓴다). 기믹 무버 · 카메라 레일 · 길이 함께 쓴다. `Spline/README.md`
- **UI**: 장르 무관 UI 컴포넌트 — `DialogueRunnerComponent`,
  `HealthBarComponent`, `DamageNumberComponent`. HP 바 · 데미지 숫자는 월드 공간 스프라이트(`SpriteInstanceBatch`)로 그린다 — 저장되는
  컴포넌트를 만들지 않는다. HP 바는 `HealthListenerComponent`(Combat)를 상속해 체력 시스템의 알림(`HealthChangedEvent` — 다시 두기 · 바뀜 · 쓰러짐)을 받는다 —
  체력 시스템(어빌리티 · 키트 · 게임)은 바를 모른다 — 바는 시작할 때 같은 오브젝트의 `HealthSourceComponent` 에서 비율을 읽는다(Lyra `ULyraHealthComponent::OnHealthChanged` 를 위젯이 받는 자리). 보이기 정책도 바의 것이다
  (`_bShowWhenHurt` · `_bHideWhenDead`). 데미지 숫자의 입력은 `DamageNumberComponent::setDamageValue` · `spawnNumber`.
  `FadeOutComponent` 의 흐림은 같은 오브젝트 스프라이트들의 색 알파에 곱해진다.

별도 타겟:

- **Kits**: 키트끼리 링크하지 않음. 공유 타입은 기반(`Framework` · `World` · `Utility` · `UI` …)으로. 장르 묶음 폴더 아래 키트 하나씩입니다(`Kits/<묶음>/<키트>`, 타겟은 `GF_<키트>`).
  - **액션 · 대전** (`Kits/Action/`)
    - `ActionCombat`: 공격 히트박스(`MeleeHitboxComponent`), 투사체, 유닛 스탯, 액션 룸.
      피해는 한 길이다 — 투사체(`ProjectileComponent`)와 공격 판정은 같은 오브젝트의 `BoxCollider2DComponent` 겹침으로 맞음을 알고
      `UnitStatsComponent::takeDamage( 피해, 쏜 쪽 )` 을 부르며, HP 가 깎인 그 자리에서 컴포넌트 델리게이트(`registerDamageApplied`)가 불리고
      `DamageAppliedEvent`("game" 채널)가 나간다. 액션 룸의 적은 몬스터 정의(`MonsterCatalog` 게임 서비스, 없으면 내장 그런트 · 보스)다. 액션 룸은 시작 · 클리어 · 패배에 룸 이벤트를 낸다. 채널 이벤트는 `GameEventUtil::send` 하나로 낸다 —
      버스 스레드면 그 자리에서, 아니면 다음 `processEvents` 에.
    - `ActionAdventure`: 액션 어드벤처(젤다 장르) — 던전 열쇠 · 조건 문 · 지도/나침반 · 장치(`AdventureDungeon`), 하트 조각 · 마법 · 스태미나 탈진(`AdventureVitals`), 주목 옆걸음 · 회피(`AdventureTargeting`), 불 번짐 · 전기 · 얼음 셀 자동자(`AdventureElementGrid` — 기반 원소 규칙표 `ElementGrid` 위), 효과 합산 요리(`AdventureCooking`), 무기 내구도(`AdventureWeaponWear`), 탑 · 사당 · 증표(`AdventureWorldMap`).
    - `ActionPlatformer`: 스테이지형 액션 플랫포머(검브렐라 · 페퍼 그라인더 · 어스블레이드 장르) — 체크포인트 · 목숨 · 비밀 수집 · 등급(`ActionStageRun`), 활공 · 갈고리 진자 · 드릴 이동(`ActionPlatformerBody`), 근접 콤보 · 총 · 패리 반사(`ActionCombatRig`), 데이터 적 패턴(`ActionEnemyBrain`).
    - `Metroidvania`: 메트로배니아 · 2D 소울라이크(블라스퍼머스 2 · 더 라스트 페이스 · 엠버베인 장르) — 능력 잠금(`MetroAbilitySet`), 탐색률 · 지도 구매(`MetroMapState`), 휴식 · 시체 · 물약(`MetroSoulsState`), 스태미나 · 패리 · 강인도(`MetroDuelist`), 부적 슬롯(`MetroCharmLoadout`).
    - `Fighting`: 3D 격투(철권 장르) — 캐릭터 데이터(커맨드 · 자세 · 조건 · 스트링, `FighterCatalog`), 60프레임 결정적 시뮬레이션(가드 높이 · 프레임 이득 · 저글 감쇠 · 스크류 · 벽꽝 · 잡기 풀기 · 횡이동 · 레이지 · 히트) · 라운드(기반 `RoundSeries` — 선승 · 무승부 라운드는 둘 다 1 승 · 라운드 시간 · 대기) · 롤백 상태 저장/복원(`FightingMatch` — 라운드 묶음은 상태 바이트 맨 뒤).
    - `MechArena`: 3인칭 팀 기체 대전(SD건담 캡슐파이터 장르) — 기체 · 형태 · 무기 칸 · 분류 보정 · 스킬(`MechCatalog`), 부스트 오버히트 · 다운치 · 기상 무적 · 록온 유도 · 근접 콤보 · 변형 · 팀 전력 게이지 · 기체 교체(`MechArenaWorld`), 넷 스냅숏(`MechArenaSnapshot` — 화면용 양자화)과 세이브 · 핫 리로드용 전체 상태(`writeState( Archive& )`)를 따로 둔다.
    - `BattleRoyale`: 배틀로얄(배틀그라운드 장르) — 자기장 단계 · 다음 원 고르기(`BrZone`), 비행기 경로 · 낙하 · 착지 예측(`BrDrop`), 지점별 전리품 · 보급 상자(`BrLoot`), 방어구 · 가방 등급 · 탄약(`BrGear`), 기절 · 팀원 부활 · 순위 · 킬 피드(`BrMatch`).
  - **공포 · 조사** (`Kits/Horror/`)
    - `SurvivalHorror`: 생존 공포 · 조사(바이오하자드 · 홀스틴 · 애니그마 오브 피어 장르) — 격자 가방(기반 `GridInventory`), 아이템 상자 · 조합 · 세이브 제한 · 정신력/손전등 · 열쇠 문 · 다이얼/순서 퍼즐 · 단서 보드 추리(`HorrorSession`), 턴제 초자연 전투(`HorrorEncounter`).
    - `AsymmetricHorror`: 비대칭 공포(데드 바이 데이라이트 장르) — 규칙 · 살인마 · 점수 XML(`AsymmetricHorrorRules`), 건강 → 부상 → 빈사 → 갈고리 단계 · 몸부림 · 구출, 다인 수리 · 치료 · 스킬 체크 · 걷어차기 퇴행, 판자 · 창틀 · 사물함, 탈출구 · 해치 · 붕괴(`HorrorMatch`), 넷 스냅숏(`HorrorSnapshot` — 화면용 양자화)과 세이브 · 핫 리로드용 전체 상태(`writeState( Archive& )`)를 따로 둔다.
    - `CoopScavenger`: 협동 수집 공포(리썰 컴퍼니 장르) — 할당량 주기(`ScavengerQuota`), 위성 · 하루 시각 · 날씨 · 위협 예산 · 죽음과 시신 회수 · 전멸 손실(`ScavengerExpedition`), 절차 시설 방 그래프 · 고철(`ScavengerFacility`), 운반 칸 · 양손 · 무게(`ScavengerCarry`).
    - `GhostHunt`: 유령 사냥(루이지 맨션 장르) — 손전등 원뿔 · 스트로브 기절 · 흡입 줄다리기 · 강화 단계(`GhostEncounter`), 방 불 켜기 · 열쇠 문 · 가구 보물 · 부 탈출(`GhostMansion`), XML(`GhostCatalog`).
  - **RPG** (`Kits/Rpg/`)
    - `Overworld`: 타일 걸음 필드 — 칸 조회(`TileMap` — Engine `TileMapXmlData` 그대로 · 걷기 · 조우 칸 · 통과 · 워프), 걸음 이동(`PlayerController` · `PlayerLocomotion`), 존 태그 · 클리어 게이트(`ZoneTracker`). 세이브는 스냅숏 봉투(타일 · 대기 워프는 `PlayerController` 상태 바이트 'OPLC', 플래그는 공유 상태). 무엇을 만나는지는 장르 키트의 지역 표(`MonsterCollector` · `ClassicJrpg`)가 정한다.
    - `ClassicJrpg`: 클래식 JRPG(드래곤 퀘스트 3 HD-2D · 씨 오브 스타즈 · 완다링 소드 장르) — 직업 · 주문 · 장비 카탈로그(`JrpgCatalog`), 파티 · 전직 · 여관 · 교회(`JrpgParty`), 라운드제 전투 · 타이밍 공격/방어(`JrpgBattle`), 걸음 수 인카운터(`JrpgEncounter`).
    - `MonsterCollector`: 몬스터 수집(포켓몬 장르) — 종 · 기술 · 성격 · 날씨 카탈로그(`MonsterCollectorCatalog`), 개체값 · 노력치 · 능력치 공식 · 경험치 · 진화(`MonsterInstance`), 우선도 · 스피드 순 1:1 전투 · 피해 공식 · 상성 · 포획(`MonsterBattle`), 트레이너 AI(`MonsterTrainerAi`).
    - `OpenWorldWestern`: 오픈월드 서부극(레드 데드 리뎀션 장르) — 목격자 시야 · 신고 시간 · 처치/위협으로 막기 · 지역별 현상금 · 수배 감쇠 · 보안관 추적(`WesternLaw`), 명예 단계 · 할인 · 대사 플래그(`WesternHonor`), 말 유대 · 능력 해금 · 코어 · 질주 · 겁(`WesternHorse`), 추위/더위 · 옷 · 음식 · 데드아이(`WesternSurvival`), 가죽 등급 · 사체 부패 · 매입 값(`WesternHunting`).
    - `WitcherRpg`: 위쳐 RPG(위쳐 3 장르) — 괴물 도감 지식 · 해금된 약점 · 속성 배율(`WitcherBestiary`), 연금술 · 독성 · 변이 혼합물 · 명상 보충 · 오일(`WitcherAlchemy`), 표식 · 대체 시전 · 스태미나 · 아드레날린(`WitcherCombat`), 변이 슬롯 색 맞춤(`WitcherMutagens`), 계약 단서 순서 · 보상 흥정(`WitcherContract`).
  - **전략** (`Kits/Strategy/`)
    - `RealTimeStrategy`: 실시간 전략(스타크래프트 장르) — 유닛 XML(`RtsCatalog`), 명령 · 채취 · 건설 · 생산 · 테크 · 전투 · 안개 · 흐름장 무리 이동(`RtsWorld`),
      고르기 · 부대(`RtsSelection`), 행동 트리 AI(`RtsAiController`).
    - `CityBuilder`: 도시 건설(파라오 장르) — 건물 · 물자 · 집 단계 XML(`CityCatalog`), 도로망 · 노동 · 순회 일꾼 · 수레 · 시장 · 집 진화 · 이민 · 세금 · 범람(`CitySimulation`).
    - `TacticsSrpg`: SRPG(SD건담 G제네레이션 · 메탈슬러그 택틱스 장르) — 기체 · 파일럿 · 무기 · 지형 XML(`SrpgCatalog`), 전장(`SrpgBattlefield` — `GridReachability` 이동 범위 · ZOC · MAP 병기 · 페이즈/개별 순서), 전투 예측 · 반격 · 지원 · 동기(`SrpgCombat`), 점수 AI(`SrpgAiController`), 승패 · 개발 · `RunMap` 로그라이트 캠페인(`SrpgProgress`).
    - `SideScrollConquest`: 횡스크롤 정복(썬즈 오브 발할라 장르) — 1차원 전선 거점 · 건물/일꾼/생산 · 병력 훈련/인구 · 지휘관 부대 명령 · 진형 · 사기 · 성문/성벽 · 충차/사다리 · 점령 → 영토 · 반격 웨이브(`ConquestWorld`).
  - **시뮬레이션 · 생활** (`Kits/Simulation/`)
    - `Farming`: 농장 생활(하베스트 문 장르) — 작물 XML 카탈로그(`CropCatalog` — 계절은 공유 시계의 이름, 씨앗 · 수확물 아이템을 `ItemCatalog` 로),
      밭(`FarmField`: 갈기 · 물 · 심기 · 거두기, 물 받은 날만 자람, 다시 열림, 철 지나면 시듦, 비), 출하함(`FarmShippingBin` — 빌린 가방에서 담고 빌린 지갑으로 정산).
      달력 · 돈 · 가방은 공유 상태(`WorldClock` · `Wallet` · `Inventory`)를 빌린다.
    - `CreatureLife`: 생물 생활(포코피아 · 문스톤 아일랜드 장르) — 칸 패턴 서식지 레시피(회전 · 큰 것 먼저), 시간대 · 날씨 방문(결정적, 조건은 일정과 같은 `ScheduleCondition`), 생물별 호감도(`ReputationState`) · 부탁(`QuestLog`), 능력 칸 변환 · 하루 횟수, 집 배정, 마을 매력도 단계(`CreatureTown`).
    - `RestaurantSim`: 식당 경영(셰프 RPG 장르) — 메뉴(Crafting 레시피 · 숙련도 품질), 재료 신선도 묶음(`IngredientStock`), 시장 시세, 손님 도착 · 성향 · 인내, 요리사 · 스테이션 병렬 조리, 서빙 · 계산 · 팁, 별점 이동 평균, 일 결산(`RestaurantSimulation`).
    - `ThemePark`: 롤러코스터 타이쿤 — 조각으로 쌓는 코스터 트랙(`CoasterTrackBuilder`: 오르막 체인 · 낙하 · 언덕 · 뱅크 회전 · 클로소이드 루프 ·
      브레이크 · 부스터, XML 레이아웃), 고정 스텝 열차 물리(`CoasterTrain`), 시험 운행으로 흥분 · 강도 · 멀미 평가(`CoasterRideAnalyzer`),
      손님 · 줄 · 표 · 입장료 · 운영비 · 공원 평점 경영 시뮬레이션(`ThemeParkSimulation`).
    - `Voxel`: 복셀 샌드박스(마인크래프트 장르) — 블록 XML 카탈로그(`VoxelBlockCatalog`: 면마다 아틀라스 칸), 청크 월드(`VoxelWorld`: 경계 블록이 바뀌면
      이웃 청크도 다시 짓기), 씨앗 고정 지형(`VoxelTerrainGenerator`: 값 노이즈 · 물 · 모래 · 나무), 격자 광선(`VoxelRaycast`: 맞은 면 · 놓을 칸),
      드러난 면만 짓는 메싱(`VoxelMesher`: 꼭짓점 그늘 · 반투명 분리), 상자 몸(`VoxelBody`: 축별 충돌 · 점프 · 헤엄), 핫바(`VoxelHotbar`).
  - **캐주얼 · 파티** (`Kits/Casual/`)
    - `CardGame`: 카드 게임(포커 · 맞고/고스톱 · 솔리테어 · 우노 · 덱 빌딩) — 공통 덱 · 셔플 · 턴 중계 행동 바이트(`CardDeck`), 족보 판정(`PokerHand`), 블라인드 · 사이드 팟 테이블(`PokerTable`), 화투 48장 · 뻑 · 쪽 · 싹쓸이 · 고/스톱 · 박(`MatgoGame`), 클론다이크 undo · 자동 완료(`KlondikeGame`), 우노 벌칙 · 쌓기(`UnoGame`), XML 카드 덱 빌딩 전투(`DeckBattle`).
    - `Rhythm`: 건반 리듬(오투잼 장르) — 채보(`RhythmChart` — 변속 · 정지 · 변박 · 롱노트, 박 ↔ 초), 판(`RhythmPlaySession` — `TimingJudge` 판정 · 콤보 · 라이프 · 등급 · 오토플레이 · 리플레이), 스크롤 위치.
    - `PartyArena`: 파티 아레나(바이킹 온 트램펄린 장르) — 트램펄린 튕김 타이밍 콤보 · 공중 공격 · 내려찍기 · 링 아웃 점수 · 락스텝 입력(`TrampolineArena`), 씨앗 무작위 아이템(`PartyItemSpawner`), 라운드 목록 · 순위 점수 · 먼저 N 점 우승(`PartyRoundSeries` — 점수표는 기반 `RoundSeries`, 동점은 서든 데스).
    - `KartRacing`: 카트 레이싱(카트라이더 · 마리오카트 장르) — Catmull-Rom 트랙 · 체크포인트 · 오프로드(`KartTrack`), 순서 랩 · 실시간 순위 · 역주행(`KartRace`), 순위 가중 아이템(`KartItems`), AI 레이싱 라인 · 드리프트 · 러버밴딩(`KartAi`), 고스트(`KartGhost`).
  - **네트워크 방식** (`Kits/Network/`)
    - 네트워크 방식(장르별로 골라 링크 — 싱글 게임은 링크하지 않는다, 공통 계층은 `Core/Network`). 정책은 가상 인터페이스로 게임이 바꾼다.
      메시지 첫 바이트는 키트마다 영역이 나뉘어(`NetMessageRange`) 한 게임이 둘을 같이 써도 섞이지 않는다. 키트의 서버 · 클라이언트는 모두 `INetMessageHandler` 라
      `NetMessageRouter` 에 걸어 두면 `pump( host )` 가 영역대로 나눠 주고 게임 메시지(0x80..)만 돌려준다(`handleMessage( buffer )` 를 직접 불러도 된다 — 제 것만 먹고 false).
      보낼 메시지는 키트마다 `NetMessageWriter` 하나를 다시 쓰고, 서버의 스냅샷 · 관심 영역 계산도 매 틱 목록을 새로 잡지 않는다.
      **대역폭**: 복제 서버 · MMO 의 틱 예산은 설정(`_snapshotBudgetBytes` · `_updateBudgetBytes`)과 호스트 연결 상한의 몫 중 작은 것이다 — 상한(`NetHostSettings::_maxBytesPerSecond`)을 낮추면 따라 준다.
      **멀티스레드**: `NetHost` 는 아무 스레드에서나 부를 수 있고 `NetHostThread` 가 게임 프레임과 따로 돌린다(Core README). 서버 쪽
      `ReplicationServer` · `MmoReplicator` 는 `setTaskManager( &engine::getTaskManager() )` 를 주면 연결(관찰자)마다의 스냅샷 · 관심 영역 계산을
      작업 스레드에 나눈다 — 결과는 한 스레드와 바이트까지 같고(`NetParallelTest`), 관찰자 128 · 엔티티 8000 에서 틱당 4.4 → 1.5 ms(워커 3).
      그때 정책(`IReplicationPolicy` · `IInterestPolicy`)은 여러 스레드에서 동시에 불리므로 읽기만 한다.
      - `NetClientServer`: 권위 서버(슈터 · 배틀로얄 · 액션 · 기체 대전 · 비대칭) — 스냅샷 델타(확인된 기준 대비 · 예산 · 우선도, `IReplicationPolicy` 관련성),
        보간(`ReplicationClient` — Core `NetClock`: 받은 스냅숏 틱의 하한 + 흐른 시간 − 지연(최소값과 틱 간격 × 2 중 큰 것), 되돌아가지 않고 받은 틱을 넘지 않는다), 확인 기반 입력 보내기(Core `NetInputSendWindow` — 서버가 스냅숏에 실어 돌려준 "빈틈없이 받은 다음 틱" 부터),
        클라이언트 예측 되맞추기(`ClientPrediction`), 랙 보정 되감기(`LagCompensationHistory`).
        우선도는 클라이언트마다 `NetPrioritizer` 로 스냅샷마다 쌓고, 실었거나 클라이언트가 이미 최신인 엔티티만 0 으로 돌린다 — 예산이 늘 차도 낮은 우선도가 굶지 않는다
        (우선도 10 넷이 예산을 채우면 우선도 1 은 11 틱쯤에 한 번). 스냅샷 예산은 메시지 전체(종류 바이트 · 머리 · 사라진 목록 · 끝 표시)를 `NetSendBudget` 으로 정확히 세고 1024 B 로 잘린다 — 못 실은 사라짐 · 바뀜은 재구성에
        기준 값으로 남아 다음 델타가 다시 고른다. 엔티티 상태는 `NetSnapshot::kMaxEntityBytes`(255 B)까지 — 넘으면 싣지 않는다(`setEntity` 가 처음 한 번 경고).
        입력은 틱마다 `NetClientServerMessage::kMaxInputBytes`(255 B)까지(넘으면 `sendInput` 이 false), 메시지 하나에 `kMaxInputCount`(32)와 메시지 상한 안에서
        확인 안 된 것을 **오래된 것부터** 싣는다(연속 손실이 길어도 서버가 꺼내기 전이면 빈틈이 남지 않는다) — 두 상수를 클라이언트 · 서버가 같이 쓰고, 서버는
        넘는 길이 · 개수를 깨짐으로 보며 깨진 메시지의 입력은 하나도 넣지 않는다. 시험: `NetClientServerTest` · `NetSimReplicationTest`(하니스 위 — 대량 사라짐 · 예산 포화에서 굶지 않음).
      - `NetLockstep`: 결정적 — 락스텝(`LockstepSession` — 입력 지연 · 체크섬 비동기 감지, RTS), 롤백(`RollbackSession` · `IRollbackGame` — 예측 · 되감기 · 재시뮬레이션, 격투).
        롤백 입력은 GGPO 식이다 — 메시지마다 "플레이어마다 빈틈없이 받은 다음 프레임"(확인)을 싣고, 보내는 쪽은 모두가 확인한 다음 프레임부터 싣는다
        (연속 손실이 길어도 빈틈이 남지 않는다 — Core `NetInputSendWindow` · `NetInputReceiveBuffer`). 받는 창은 [지금 − 64, 지금 + 64). 앞선 쪽은 (내 이점 − 상대 이점) / 2 가 `_maxFrameAdvantage` 를 넘으면
        한 프레임 쉰다(시간 동기). 락스텝은 서버가 클라이언트 연결이 닫히면(`onConnectionClosed`) "플레이어 p 는 틱 T 부터 빈 입력"(`kLeave`)을 신뢰 순서로
        알린다 — T 는 서버가 받은 p 의 마지막 입력 다음 틱이라 모두가 같은 틱에 p 를 뺀다(`getLeaveTick`). 입력은 플레이어마다 다음 틱만 받고(먼 틱 · 겹친 틱은
        버린다) 내 입력은 지금 + `kMaxInputLead`(127)까지만 예약한다(`submitLocalInput` 이 false). 체크섬은 `kChecksumWindow`(256 틱) 넘게 지나면 지운다.
        시험: `NetLockstepTest`(하니스 위 30 틱 연속 손실 · 늦게 시작한 상대 · 넷 중 둘이 떠남 · 한쪽만 체크섬).
      - `NetTurnRelay`: 턴제 중계(카드 · 보드 · SRPG) — 방 · 자리 · 표, `ITurnPolicy`(차례 · 허락 · 방향), 행동 기록 방송, 재접속 시 놓친 행동.
        서버는 자리마다 보낸 행동 수(`TurnSeat::_sentActionCount`)만 들고 방 기록에서 이어 보낸다 — 신뢰 창이 차면 멈췄다가 `TurnRelayServer::update`(매 틱)가
        이어 가므로 놓친 행동이 창(255)보다 많아도 빠지지 않고, 다른 알림도 창이 차면 연결마다 줄을 선다(64 를 넘게 쌓이면 그 연결을 끊는다). 행동은 8 KB 까지(1 KB 를 넘으면 Core 가 조각으로 나르고 조각마다 창 한 칸). 자리 표는
        운영체제 난수 비밀에서 섞는다. 표가 맞아도 그 자리 연결이 살아 있으면 `SeatInUse`, 한 연결은 자리 하나(다른 방은 `AlreadySeated`).
        시험: `NetTurnRelayTest`(하니스 위 300 행동 재동기 · 침입자 · 두 번 들어오기 · 서버마다 다른 표).
      - `NetMmo`: MMO — 관심 영역(엔진 `SpatialHashGrid2D` 의 XZ 점 · 반경 질의, 들어옴 · 나감 히스테리시스), 우선도 누적(`NetPrioritizer`) 대역폭 예산(`NetSendBudget`), `IInterestPolicy`(늘 보이기 · 우선도).
        들어옴은 틱마다 신뢰 메시지 하나에 묶고(조각으로 나른다), 나감은 1 KB 묶음으로 쪼개 보낸 것만 보이는 목록에서 뺀다(신뢰 창이 차면 다음 틱에). 들어옴 · 갱신에 서버 틱(`update` 마다 하나)을 싣고
        클라이언트는 엔티티마다 마지막으로 적용한 틱보다 옛 갱신을 버린다(`getStaleUpdateCount`). 상태는 `NetMmoMessage::kMaxStateBytes`(512 B)까지 — 넘는 `setEntity` 는
        서버가 받지 않는다. 시험: `NetMmoTest`(하니스 위 — 순간 이동 대량 나감 · 순서 뒤바뀐 갱신 · 상한 넘는 상태).
      - `NetDestruction`: 파괴 네트워킹(`DestructionReplicationServer` · `Client`, 영역 `kDestruction` 0x50). 권한 쪽 피해 사건을 번호(= 서버 상태의
        사건 수)를 붙여 신뢰 · 순서 없음 채널로 보내고(앞 사건을 잃어도 뒤 사건이 기다리지 않는다) 받는 쪽은 번호 순으로만 적용한다(앞 번호는 버리고 뒤 번호는 기다린다 — 청한 스냅숏을 기다리는 동안은 모두 쌓았다 잇는다). 덩어리(표의 `keepCollisionVolume` 이상)는
        서버가 질량 중심 · 회전을 `<Network poseRate>` 로 비신뢰로 보내고(멈추면 비트 그대로 신뢰로 확정 + 비신뢰로 몇 번 더), 받는 쪽은 Core `NetClock`(서버 틱
        추정 − 보간 지연, 지연은 설정값과 자세 간격 × 2 중 큰 것 — 자세 하나를 잃어도 사이를 잇는다)을 덩어리마다 `InterpolationBuffer` 의 자세 사이에서 그려 키네마틱으로 몬다. 파편은 각자 시뮬레이션하는 꾸밈(Debris 레이어 — 캐릭터와 안 부딪힌다). 늦은 참가 · 해시 어긋남은 상태
        스냅숏(신뢰 메시지 하나 — 64 KB 까지, 창이 차면 다음 틱에 다시)으로 맞춘다. 파괴를 쓰지 않는 게임이 링크하지 않게, 권위 방식(복제 서버 · 리슨 · MMO)과 상관없이 `NetHost` 위에 얹게 키트를 따로 둔다.
        롤백(상태 저장 · 되돌리기)은 없다. 시험: `NetSimDestructionTest` · `NetSimDestructionMatrixTest`(나쁜 회선 둘 — 회선마다 한 케이스, nogpu 조각).
      - `NetSimulation`: 한 프로세스 가상 서버(`NetSimHarness` — 언리얼 PIE "Play As Client, Number of Players N" + Network Emulation, 유니티 Multiplayer
        Play Mode + Network Simulator 의 자리). 서버 월드 1 + 클라이언트 월드 N 이 **각자 씬 · 오브젝트 매니저 · 물리**를 갖고, 루프백 망 위에 끝점마다
        흉내(`NetEmulationTransport`)를 씌워 클라이언트마다 올림 · 내림 조건(지연 · 흔들림 · 손실 · 중복 · 깨짐 · 순서 · 대역폭)을 따로 준다. 시각은 틱 × 간격,
        모든 난수는 씨앗에서 — 같은 씨앗이면 같은 패킷이 같은 틱에 도착한다. 게임은 `INetSimGame::createSession( world )` 에서 월드 내용을 짓고 세션
        (`INetSimSession` — `onTickBegin` · `onTickEnd` · `onHostEvent`)을 돌려준다. 키트의 연결 수명은 라우터가 처리기에 넘기므로(`onConnectionOpened` ·
        `onConnectionClosed`) `onHostEvent` 는 게임 몫만 한다(틱 앞, 라우터가 사건을 알린 바로 뒤에 불린다). 늦은 참가 · 떠남(`addClient` · `removeClient`, 떠나면 끊김 알림이 간다).
        엔진 루프는 활성 씬 하나만 틱하므로 하니스는 월드마다 `GameObjectManager::tick` 을 직접 부른다(씬의 `tick` 이 아니다 — 오디오 리스너는 프로세스에
        하나). 렌더러 · 오디오는 쓰지 않는다(nogpu 시험 · 게임 자동화). 틱 순서는 받은 것 나눠 주기 → 오브젝트 틱 → 보내기 — 깨끗한 회선이면 틱 N 에
        보낸 것을 틱 N + 1 이 받는다. 시험: `NetSimHarnessTest`.
  - **온라인 서비스** (`Kits/Online/` — 서버 전용은 `Kits/Online/Server/<키트>`, 모듈 `GF_Server_<키트>`. 서버 키트는 같은 기능의 공유 키트만 include 한다)
    - `Account`(`GF_Account`, Client · Server): 계정 와이어 타입(`AccountTypes.h` — 결과 · 끝난 까닭 · 세션 토큰 · 게임 접속 표 · 클라이언트 정보 · 연동 요약 ·
      빌드 판 비교 `AccountUtil::compareBuild`), 외부 로그인 표를 얻는 쪽(`PlatformLoginClient.h` — `IPlatformLoginClient` · 시스템 브라우저 `IExternalBrowser` ·
      가짜, PC 는 `LoopbackPkceLoginClient` — 시스템 브라우저 + 127.0.0.1 리다이렉트 + PKCE S256 · state · nonce, 표 = `id_token|nonce`),
      와이어(`AccountProtocol.h` — 메서드 · 알림 `kPushRevoked` · 코덱 `AccountWire`), 클라이언트(`AccountClient` — 다시 연결되면 토큰으로 재접속을 먼저 보내고
      그동안의 세션 요청은 그 뒤에, 밀려남 알림이면 토큰을 버린다 · `makeConnectCredentials` 로 UDP 자격), 게스트 장치 비밀(`AccountDeviceSecret` — 로컬 저장 Encrypted).
    - `Server/Account`(`GF_Server_Account`, Server): 로그인 서비스 — 계정(소금 + 느린 해시 Argon2id, 매개변수는 레코드에 — 바뀌면 다음 로그인에 다시 해시),
      게스트(장치 비밀 다이제스트 → 계정) · 연동(이름 · 외부 계정을 "없어야 함" 으로 — 다른 계정 것이면 `AlreadyLinked`, 자동 합치기 없음) · 외부 로그인
      (`Platform/PlatformLoginProvider.h` — 맡기고 거두는 확인, 가짜 `FakePlatformLoginProvider`; 제공자는 데이터 `PlatformLoginProviderSettings` →
      공통 구현 `OidcLoginProvider`(JWT RS256 · ES256 · iss · aud · exp · nonce, `JwksKeyCache` — 회전 때 재조회 · 다운이면 캐시로) · `ProfileApiLoginProvider`
      (액세스 토큰 → 프로필 API 의 주체 경로), 주체 id 만 키 — 이메일은 쓰지 않는다), 한 계정에 외부 계정 여럿 · 마지막 로그인 수단은 해제 불가,
      빌드 판(원격 설정 최소 · 권장 · 상점 주소), 로그인 · 재접속 직전 제재 확인(`Online/Sanction`), 탈퇴(유예 30 일 · 취소 · 쓸기 — 원장 · 감사는 남김),
      세션 토큰(선택자 + 검증자, 저장소엔 다이제스트만 · 상수 시간 비교 · 재접속마다 회전), 재접속 유예 · 절대 수명, 중복 로그인(기본 새 로그인이 옛 세션을 밀어냄 ·
      `RejectNew`, 묘비로 "다른 곳에서 로그인"), 실패 누적 잠금 · 주소마다 시도 제한(`TokenBucketMap`), 없는 계정에도 해시(열거 막기), 게임(UDP) 접속 표
      (`LoginTicketAuthority` — 주 키로 서명한 상태 없는 표 64 B + 표 비밀). 요청은 저장소에 일로 맡기고(`LoginStoreLogic` 이 저장소 스레드에서) 꼬리표로 거둔다.
      `IAccountDirectory` 구현(이 프로세스에 붙어 있는 계정만). 암호는 `ILoginCrypto` 뒤 — 실제 구현 `NetSecurityLoginCrypto`(네트워크 보안 제공자의
      Argon2id · HKDF-SHA256, 서버는 기동 때 `isPasswordHashSupported` 로 확인). 시험: `LoginServiceTest`(메모리 저장소 · 결정적 가짜 암호 + 실제 암호 한 케이스).
    - `Chat`(`GF_Chat`, Client · Server): 채팅의 공통 타입 — 결과 코드(`ChatResult`, 키트 오류 = `OnlineMethodRange::kChat` + 값), 채널 id 규칙(`ChatChannelId` —
      `world.` · `guild.<16 진>` · `party.<16 진>` · `custom.` · 귓속말 기록 키 `whisper.<작은 id>.<큰 id>`, `[0-9a-z_.]` 48 바이트 — 버스 주제 · 저장소 키에 그대로), `ChatMessage`.
      시험: `ChatChannelIdTest`.
    - `Server/Chat`(`GF_Server_Chat`, Server): 금칙어 거르개(`ChatWordFilter` — 코드 포인트 아호-코라식, 대소 · 전각 정규화, 끼움 글자를 건너뛰고 맞춰 `b.a.d` 도 걸고
      가릴 때는 구간 안의 끼움 글자도 가림, 가리기 · 거절 두 방식, 잘못된 UTF-8 거절, 목록은 `Config/Server/chat_banned_words.txt` — 저장소에는 시험 낱말만),
      도배 막이(`ChatSpamGuard` — 계정마다 `TokenBucketMap` 몰아 쓰기 5 · 1 초에 하나 + 정규화 해시로 10 초 안 같은 글 셋째 거절). 채널 · 귓속말 · 기록 · 서비스 바인딩은
      아직 없다(백로그 1-7). 시험: `ChatWordFilterTest` · `ChatSpamGuardTest`.
    - `Matchmaking`(`GF_Matchmaking`, Client · Server): 매칭 타입 — 결과 코드(`MatchmakingResult`), 모드 규칙(`MatchModeDefinition` — 팀 수 · 인원 · 실력 창 기본 ·
      초당 넓힘 · 상한 · 지역 풀기 · 시한), 표(`MatchTicket` — 혼자 또는 파티), 만든 경기(`MatchFormed`).
    - `Server/Matchmaking`(`GF_Server_Matchmaking`, Server): 매처(`MatchMaker` — 모드 하나의 대기열, 결정적이고 전송 · 저장을 모른다). 오래 기다린 표가 닻, 두 표의 창 중
      큰 것 안의 후보를 실력 거리 순으로 넣어 자리를 정확히 채우고, 큰 표부터 실력 합이 낮은 팀에(파티는 쪼개지 않음 — 나눌 수 없으면 그 닻은 이번에 건너뜀),
      기다리면 지역을 풀고, 시한을 넘긴 표는 돌려준다. 경기 id = 권한 서버 씨앗 << 32 | 순번. 파티 · 로비 · 대기열 권한 서버 · 전용 서버 배정은 아직 없다(백로그 1-7).
      시험: `MatchMakerTest`.
      Argon2id · HKDF-SHA256, 서버는 기동 때 `isPasswordHashSupported` 로 확인). 스트림 바인딩 `AccountServer`(IOnlineService · IAccountSessionControl — 연결의 세션 토큰은
      메모리에, 로그인 · 재접속 성공이면 호스트에 계정을 붙이고 같은 계정의 옛 연결은 알림 뒤 닫는다, 연결이 닫히면 재접속 유예, 탈퇴 쓸기 · 세션 다시 읽기를 주기로,
      서버 여럿이면 다른 서버의 옛 세션을 버스 `account.revoke` 로 닫는다), 접속 상태 `OnlinePresence`(`IAccountPresence` 구현 — 캐시 `presence:` 키를 시한 · 주기로 다시 적고
      "내 것일 때만" 지움, 계정 id · 이름으로 붙은 서버 찾기, 버스 `push.<서버>` 로 다른 서버의 계정에게 알림), 이름 색인 `AccountNameIndex`(`IAccountNameIndex` 구현 —
      정식 계정의 소문자 로그인 이름 표 · 프로필 표를 읽는다, 상태 없음),
      UDP 접속 인증기 `AccountConnectAuthenticator`(게임 서버 — 저장소 없이 표 서명 · 서버 · 시한만). 시험: `LoginServiceTest` · `PlatformLoginTest` ·
      `AccountStreamTest`(루프백 스트림 평문 · TLS, UDP Encrypted 접속까지) · `OnlineMultiServerTest`(호스트 둘이 저장소 · 캐시 · 버스를 나눠 씀) · `AccountNameIndexTest`.
    - `Trade`(`GF_Trade`, Client · Server): 거래 와이어 타입(`TradeTypes.h` — 다리 · 상태 · 닫힌 까닭 · 결과 · 스냅숏 · 코덱 `TradeWire`), 와이어(`TradeProtocol.h` —
      메서드 · 알림 Invited · Update · Closed, 응답에 요청한 계정의 이동 뒤 잔액), 클라이언트(`TradeClient` — 모든 요청에 멱등 키, 확정은 비추는 스냅숏의 두 판),
      인벤토리 칸 → 다리(`TradeInventoryUtil` — 같은 아이템 칸 합침, 인스턴스 상태 칸 거절, 아이템 → 자산 id 는 게임이 잇는다).
    - `Server/Trade`(`GF_Server_Trade`, Server): 거래 서비스 — 양쪽 제시 → 잠금 → 양쪽 확정, 정산은 **맡김 없이 원장 이동 하나의 트랜잭션**(두 방향 다리를 분개 하나로 —
      키 = 거래 id, 거래 레코드 · 활성 링크 · 감사 줄과 한 커밋, 모자라면 아무것도 안 움직이고 Failed). 상태 기계는 순수 함수(`TradeStateMachine` — 제시를 바꾸면 양쪽 잠금 ·
      확정이 풀리고 확정은 본 판 둘을 싣는다), 거래 레코드는 저장소에(`TradeStoreLogic` — 계정마다 열린 거래 하나 "없어야 함", 시한 · 남은 링크는 게으르게 정리, 주인 서버
      색인으로 재시작 복구), 거래 가능 정책 `ITradePolicy`. 스트림 바인딩 `TradeServer`(상대는 표시 이름 — 이 프로세스의 디렉터리, 없으면 접속 상태 창구
      `IAccountPresence`(기반 `Online/Identity`), 바뀐 거래를 두 당사자에게 알림 — 다른 서버면 접속 상태 창구로, 기능 플래그 `feature.trade_enabled`, 떠나면 PartyLeft).
      시험: `TradeServiceTest` · `TradeStreamTest`.
    - `Economy`(`GF_Economy`, Client · Server): 경제 와이어(`EconomyProtocol.h` — 메서드 · `EconomyResult` · 응답 몸 = 결과 + 잔액 · 내역), 화폐 카탈로그
      (`CurrencyCatalog` — 상한 · 유상 표시 · 가상 화폐의 재원 차감 순서(데이터, 관례는 무상 먼저) · `ILedgerPolicy`), 상품 카탈로그(`OfferCatalog` — 가격 xor 스토어 상품 ·
      지급 · 판매 기간 · 계정당 한도 · 한 번 수), 거울(`EconomyMirror` — 원장 잔액 → `Wallet`(add · charge, 빚까지) · `Inventory`(addItem · removeItem), 스냅숏은 없는 것 0).
      온라인 게임에서는 원장(서버)이 소유의 정본이고 `Wallet` · `Inventory` 는 읽기 사본이다 — 오프라인 게임의 `Shop` · `Wallet` 은 그대로 쓴다.
      클라이언트(`EconomyClient` — 요청 넷 · 잔액 캐시, 구매 · 지급 재시도는 같은 멱등 키, 로컬 지갑은 `applyLedgerBalances( wallet )` 로만 맞춘다).
      시험: `EconomyCatalogTest` · `EconomyMirrorTest` · `EconomyClientTest`(루프백 스트림 계정 + 경제).
    - `Server/Economy`(`GF_Server_Economy`, Server): 구매 · 영수증 저장 논리(`EconomyStoreLogic` — 구매는 계정 → 소각 · 발행 → 계정 분개 하나 + 계정당 구매 수 레코드를
      한 트랜잭션에, 분개 키 = 클라이언트 멱등 키라 재시도는 잔액 · 한도 판정보다 먼저 지난 결과, 재원에 빚이 있으면 그 가상 화폐로 못 산다. 영수증 지급은 거래 id 가
      분개 키라 다른 계정의 같은 영수증은 `AlreadyRedeemed`), 영수증 검증 계약(`Receipt/ReceiptValidator.h` — 맡기고 거두는 `IReceiptValidator` · 등록부, 가짜
      `Receipt/Provider/Fake/` — 개발 전용). 서비스 `EconomyService`(IOnlineService 영역 kEconomy + 전송과 무관한
      입구 `submitCall`, 저장소 일 하나 = 요청 하나, 영수증은 등록부 → 틱에서 거둬 지급 일, 기능 플래그 `feature.shop_enabled`, Shipping 은 개발 전용 제공자 거절,
      `ServiceMetrics`("economy")). 시험: `EconomyPurchaseTest` · `ReceiptRedeemTest` · `EconomyServiceTest`.
    - `Mailbox`(`GF_Mailbox`, Client · Server): 우편함 와이어(`MailboxProtocol.h` — 메서드 · `MailboxResult` · 화면 우편 `MailView` · 응답 몸 = 결과 + 칸, 캠페인 키 `campaign/<id>`),
      클라이언트(`MailboxClient` — 첫 쪽 목록 캐시 · 안 읽은 수, 수령 · 모두 받기는 멱등 키, 수령 잔액은 게임이 경제 클라이언트로).
    - `Server/Mailbox`(`GF_Server_Mailbox`, Server): 우편함 저장 논리(`MailboxStoreLogic` — 수령 = 재원 → 계정 원장 이동 + 상태 받음 + 만료 색인 지우기 한 트랜잭션, 분개 키
      `mail.claim/<우편 토큰>` 이 두 기기 동시 수령 · 응답 유실에도 원장 한 번, 상한을 넘으면 우편이 남는다. 모두 받기는 16 통까지 우편마다 커밋, 지우기는 받았거나 첨부 없는 것만,
      만료 쓸기는 시각 순 색인으로 버림(맡김 → 소각) · 돌려줌(맡김 → 보낸 계정, 상한 무시) · 발행 재원은 지우기만, 캠페인은 목록 첫 쪽 맨 앞에), 서비스 `MailboxService`
      (IOnlineService 영역 kMailbox + `submitCall`, 주기 쓸기 · 캠페인 다시 읽기 일, 지표 `mailbox_expired_total{action}`). 시험: `MailboxTest`.
    - `Admin`(`GF_Admin`, Client · Server — 운영 도구 · 에디터만 의존, 플레이어 게임은 의존하지 않는다): GM 와이어(`AdminProtocol.h` — 메서드 · 권한 등급 넷
      `Viewer` < `Support` < `Operator` < `Super` · 명령별 필요 등급 `getRequiredRole` · `AdminResult` · 요청/응답 한 형식), 클라이언트(`AdminClient` — 바꾸는 명령은 멱등 키).
    - `Server/Admin`(`GF_Server_Admin`, Server): GM 저장 논리(`AdminStoreLogic` — 등급(`admin_role`)을 명령마다 저장소에서 읽고, 바꾸는 명령은 멱등 기록(범위 `gm.<id>`)을 먼저 보고
      효과(원장 분개 · 제재 레코드 · 우편 · 일괄 우편 · 캠페인 · 등급)와 감사 줄과 멱등 기록을 한 트랜잭션에 — 거절 · 커밋 실패면 감사 줄도 없다. 메모(티켓) 필수, 회수는 음수 금지 ·
      환불 회수(`_bRefund`)만 빚 허용, 자기 등급 변경 금지, 일괄 우편은 계정마다 배치 키라 이어 하기에 중복이 없다, 첫 관리자 `seedRole`), 서비스 `AdminService`(IOnlineService
      영역 kAdmin + `submitCall`, 조회는 계정 창구로 표시 이름 · 접속 여부, 정지 · 영구 정지가 새로 걸리면 `IAccountSessionControl` 로 세션 끊기). GM 에디터 패널은 백로그
      (에디터 확장 지점 뒤). 시험: `AdminServiceTest`.
  - **저장** (`Kits/Storage/` — 서버 전용은 `Kits/Storage/Server/<키트>`, 모듈 `GF_Server_<키트>`)
    - `SqlStore`(`GF_SqlStore`, Client · Server): SQL 드라이버 계약(`Sql/SqlDriver.h` — `ISqlDriver` · `ISqlConnection` · `SqlValue` · `SqlRowSet` · 방언 훅 `SqlDialect`),
      연결 풀(`SqlConnectionPool` — 전용 워커마다 연결 하나 · 일 큐 · 완료 큐 · 끊기면 지수 물러남으로 다시 열기), 드라이버 등록부(`SqlDriverRegistry` — 이 빌드 타깃에 든 것만,
      없는 이름은 분명한 오류), 마이그레이션 적용기(`SqlMigrationRunner` — `NNNN_이름.sql` · `NNNN_이름.<드라이버>.sql`, 체크섬 · 한 트랜잭션 · 토큰 `{{blob}}` `{{keytext}}`),
      SQLite 드라이버(`Driver/Sqlite/` — sqlite3 를 아는 유일한 폴더, WAL · 준비문 캐시), 로컬 저장의 SQLite 바닥(`SqlLocalSlotStorage` — `sw_local_slot` upsert,
      마이그레이션 `Resource/common/sql/localstore/` · DB `<루트>/localstore.db`, 기반 공장에 `registerLocalStoreBackend` 로 "sqlite").
      **SQL 이식성**: 공통 SQL 은 SQLite 3.35+ · PostgreSQL 이 같은 문법만(`ON CONFLICT … DO NOTHING/UPDATE` · `RETURNING` · `LIMIT ?`), 자리표시자는 늘 `?`(드라이버가 바꾼다),
      갈라지는 곳은 방언 훅뿐이다. 시험: `SqlDriverSqliteTest`(드라이버 계약 `SqlDriverContract.h` · 풀 · 등록부).
    - `Server/SqlStore`(`GF_Server_SqlStore`, Server): `SqlServiceStore`(기반 `IServiceStore` 의 SQL 구현 — `sw_record` 표, 조건부 쓰기는 영향 받은 행 수로,
      마이그레이션은 `Resource/common/sql/servicestore/`), `ServiceStoreFactory`(서버 설정의 저장소 항목 → 저장소, `memory` 는 기반 메모리 구현).
      PostgreSQL 드라이버(`Driver/Postgres/` — libpq 를 아는 유일한 폴더, `?` → `$n` 준비문 캐시 · 이진 결과 · SQLSTATE 로 제약 · 직렬화 실패 · 끊김을 가름, 비밀번호는 접속 글이 아니라 따로).
      시험: `ServiceStoreSqliteTest`(메모리와 같은 계약 일곱 `ServiceStoreContract.h` — 맡긴 쪽 로그 문맥 포함 · 다시 띄우기 · 공장), `ServiceStorePostgresTest` · `SqlDriverPostgresTest`(서버가 있을 때만 — `SW_TEST_POSTGRES_URL`).
    - `Server/CacheStore`(`GF_Server_CacheStore`, Server): 휘발성 저장 계약(기반 `IEphemeralStore`)의 RESP2 드라이버(`Driver/Resp/` — hiredis 없이 직접: 인코더 · 증분 파서 ·
      연결, 명령 연결 하나(파이프라인 — 답은 보낸 순서) + 구독 연결 하나, 선택 TLS), `CacheStoreFactory`(서버 설정의 캐시 항목 → 앞, `memory` 는 기반 메모리 구현,
      끝점 `host:port?prefix=&timeoutMs=&tls=&ca=`). **명령은 Valkey(리눅스) · Garnet(윈도우)이 모두 지원하는 것만**: `AUTH` · `GET` · `SET [PX] [NX|XX]` · `DEL` · `PEXPIRE` ·
      `INCRBY` · `WATCH` · `UNWATCH` · `MULTI` · `EXEC` · `ZADD` · `ZINCRBY` · `ZREM` · `ZSCORE` · `ZREVRANK` · `ZREVRANGE … WITHSCORES` · `PUBLISH` · `SUBSCRIBE` · `UNSUBSCRIBE`
      (Lua · `SELECT` · RESP3 · Redis 6.2+ 옵션은 쓰지 않는다). 비교 후 쓰기는 `WATCH → GET → MULTI/EXEC`(그 GET 답까지 뒤 요청을 내보내지 않는다), 고정 창 카운터는
      `MULTI · SET k 0 PX ttl NX · INCRBY · EXEC`(새 키에만 만료). 끊김 · 시한이면 기다리던 요청이 정확히 한 번 `Unavailable` 이고 다음 요청이 다시 연결한다(물러남 100 ms → 5 s).
      시험: `RespCodecTest`, `RespEphemeralStoreFakeServerTest`(가짜 서버에 계약 여덟 + 끊김 · 시한 · AUTH · TLS · 다시 구독), `EphemeralStoreRespTest`(실제 서버 — `SW_TEST_RESP_URL`).

## 카메라: 프리셋 데이터 + 모드 + 블렌드 + 뷰 타깃

카메라 시점은 코드가 아니라 `<CameraPresets>` XML 이다(Cinemachine 가상 카메라 + Custom Blends, 언리얼 카메라 모드의 자리). 예시는
`Resource/engine/cameras/default.cameras.xml` · `Resource/game/shooter3d/data/shooter.cameras.xml`(1인칭 · 3인칭 · 궤도 · CCTV).

- **프리셋 하나 = 섹션 원소 몇 개.** `<View>`(모드 · 피치 · 요 · 거리 · 오프셋 · `aim`/`lookAt`) · `<Lens>` · `<Damping>` · `<BlendIn>` · `<Input>`(마우스 감도 ·
  휠 배율 · 이동 속도 · Q/E 회전 칸 · 오른쪽 버튼 끌기) · `<Confiner>`(피치 · 줌 범위, 상자) · `<Framing>`(화면 위치 · 데드존 · 소프트존 · look-ahead · 그룹 맞추기) ·
  `<Collision>`(스프링 암) · `<Noise>`(펄린 손떨림) · `<Sweep>`(CCTV 요 훑기). 섹션마다 구조체 하나(`CameraViewDef` …). XML 의 각은 도, 정의는 라디안이다.
  모르는 속성 · 원소 · 열거자는 경고한다(`ResourceDataSchemaTest` 가 `*.cameras.xml` 을 읽는다).
- **모드**(`CameraMode.h` — 컴포넌트를 모르는 순수 계산): `Fixed`(CCTV — 자리 고정, 각 · 점 · 대상을 보고 훑는다) · `OrthoTopDown` · `Orbit` · `Follow` · `FirstPerson` ·
  `ThirdPerson`(어깨 너머, 대상의 시점을 따른다). 입력은 `applyCameraInput` 이 모드 상태(`CameraModeState` — 돌린 각 · 줌 · 팬 · 암 길이 · 조준 · look-ahead)에 넣고,
  `evaluateCameraMode` 가 대상 · 상태로 포즈를 낸다. 순서: 줌 · 그룹 맞추기 → 각(+ 입력 + 훑기, 피치는 제약) → 피벗 → 자리 → 스프링 암 → 프레이밍 → 상자.
- **스프링 암**: 피벗에서 카메라까지 구를 쓸어(`ICameraCollisionProbe::sweepSphere`) 막히면 **바로** 당기고 풀리면 `recoverTime` 으로 돌아간다. 질의는 인터페이스라
  물리 백엔드가 바뀌어도 카메라는 그대로다 — 지금 구현은 상자 목록(`CameraBoxCollisionProbe`)과 매니저의 `PhysicsWorld` 바디(`PhysicsWorldCameraProbe`, 대상 자신은 뺀다).
- **흔들림**(`CameraShake.h`): 손떨림은 프리셋의 `<Noise>`(채널마다 다른 시드 줄기의 1D 펄린, 같은 시드 · 시간 = 같은 값), 충격은 `CameraImpulseListener::addImpulse`
  (크기 × 남은 비율 × e^(−t/감쇠), 길이 끝에 정확히 0, 반지름 안에서 거리에 따라 선형 감쇠). 흔들림은 포즈 위에 얹는 오프셋이라 블렌드 · 감쇠가 섞지 않는다.
  2D 따라가기 카메라(`Follow2DCameraComponent::shake`)도 같은 충격 식을 쓴다.
- **블렌드 고르기**: `<Blend from to>` 표(정확히 → `from="*"` → `to="*"`) → 들어가는 프리셋의 `<BlendIn>` → `<DefaultBlend>`. 곡선은 엔진의 `BlendCurve`
  (Cut · Linear · EaseIn/Out/InOut · SmoothStep · Cubic · Exponential · Spring · Custom 키, `Engine/Animation/BlendCurve.h`). `evaluateBlendWeight` · `blendPoses` 는 컴포넌트를
  모르는 함수라 시퀀서 · 소켓 부착의 되돌아가기(`SocketBindingComponent`)도 같은 곡선을 쓴다. 직교 ↔ 원근은 섞지 않고 가중치 0.5 에서 바꾼다.
- **블렌드는 지금 화면에서 출발한다**(`CameraPoseBlender` — 디렉터 · 매니저가 같이 쓴다): 나가는 쪽은 블렌드 동안 계속 살아 있고, 블렌드 도중 다시 바꾸면 그 순간의 섞인
  포즈를 고정해 출발점으로 둔다. 포즈를 내던 중의 **컷**(곡선 `Cut` · 길이 0)은 카메라에 컷 표시(`CameraComponent::markCut`)를 남겨 렌더러가 TAA 기록을 버린다.
- **`CameraDirectorComponent`**: `CameraComponent` 와 같은 오브젝트에 붙이고 프리셋 경로 · 시작 프리셋 · 대상(또는 묶음 `_listGroupTarget`) · 돌리기 입력 맵 액션(`_cycleAction`) · 팬 · 회전 액션(`_panAction` · `_rotateAction` — 기본 `Camera.Pan` · `Camera.Rotate`)을
  준다. `PostPhysics` 에서 입력만 읽고 **포즈는 틱 뒤에 쓴다**(`executeOrDeferPostTick`) — 틱 중의 트랜스폼 쓰기는 틱 뒤에 적용되므로 틱 안에서 대상을 읽으면 한 프레임
  늦다. `-gv_cameraPreset=<id>` 가 시작 프리셋을 고른다(캡처 카메라 제외 — 스크린샷용).
- **`CameraManagerComponent`**(언리얼 `SetViewTargetWithBlend` · Cinemachine Brain): 로컬 플레이어마다 하나, 플레이어가 실제로 그리는 카메라에 붙는다. 뷰 타깃 = 다른
  오브젝트의 카메라(보통 보조 `CameraRole::Custom`)이고 그 포즈 · 렌즈를 블렌드로 따라간다. `PostUpdate` 에서 틱 뒤로 미뤄 디렉터 다음에 읽는다. 용도별 목록은
  `findCamerasByRole`(플레이어 시점 `Game` · 보조 `Custom` · 캡처 `Capture`), 플레이어별 매니저는 `findForPlayer`.
- **기존 리그도 모드다.** `OrthoCameraRigComponent` 는 자기 값으로 `OrthoTopDown` 프리셋을 지어 디렉터로 풀어(덮어쓴 탑승 시점은 `Fixed`), 탑승 시점으로 들어가고 나올 때
  `_overrideBlend` 로 블렌드하고 Q/E 는 `_rotateTime` 으로 돈다. `FirstPersonCameraComponent` 는 눈 자리 · 시점을 `FirstPerson` 모드로 푼다.
- **출력은 엔진 카메라가 고른다**(`CameraComponent::setRenderOutput` — 화면 전체 · 화면 사각형(분할 화면 · PiP) · 렌더 텍스처(`rendertarget/<이름>`, 머티리얼이
  텍스처로 읽는다), 갱신 주기 · 해상도 배율 · 그림자 · 후처리 · 보임 기준 오브젝트). 디렉터 · 매니저는 포즈 · 렌즈만 쓰므로 CCTV 도 디렉터 + `Fixed` 프리셋 +
  렌더 텍스처 출력이다. 렌더러 쪽은 `Source/Engine/Graphics/Renderer/README.md` "다중 뷰".

## 새 장르 키트를 만들 때

키트는 **그 장르의 규칙**만 담습니다. 아래는 이미 기반에 있으니 키트에서 다시 만들지 않습니다(다시 만든 것이 셋 넘게 쌓였던 것을 모은 목록입니다).

| 필요한 것 | 쓸 것 | 쓰는 키트 |
|-----------|-------|-----------|
| XML 정의 목록(id · 읽은 순서 · 같은 id 는 바꾸기) | `GameCatalog<T>` + `GameDataXml::loadRoot` · `parseRoot` · `findRequiredId` | 작물 · 무기 · 블록 · 코스터 레이아웃 |
| "1 0.5 0.2" · "Spring,Fall" 같은 칸 | `GameDataXml::parseFloat4` · `parseFloats` · `forEachToken` | 블록 색 · 계절 · 공원 배치 |
| 되풀이되는 난수(시험 · 리플레이) | `GameRandom`(상태) · `GameHash`(좌표 → 수, 상태 없음) | 탄 퍼짐 · 손님 · 날씨 · 지형 · 조우 |
| 프레임 수와 상관없는 시뮬레이션 | `FixedStepTimer` | 코스터 열차 · 공원 경영 |
| 히트스캔 · 클릭 고르기 · 1인칭 · 원뿔 시야 | `RayMath` · `FirstPersonLook` | 슈터 · 복셀 · 유령 사냥 |
| 가중치 고르기 · 섞기 | `GameRandom::pickWeightedIndex` · `shuffle` | 파티 아이템 · 식당 손님 · 카드 |
| 여러 자원 비용 | `StatBlock::canAfford` · `trySpend` | 횡스크롤 정복 |
| 아이템 + 개수 값 목록 | `ItemStackList` | 출하함 · 전리품 · 레시피 · 보상 |
| 비스듬히 내려다보는 직교 카메라 · 장식 흩뿌리기 | `OrthoCameraRigComponent` · `PropScatterComponent` | ThemeParkTycoon · HarvestValley · NileCity · StarSkirmish |
| 1인칭 카메라 · 손에 든 모델 · 마우스 잠금 | `FirstPersonCameraComponent` | Shooter3D · VoxelCraft |
| 피해 숫자 | `DamageNumberComponent::spawnNumber` | 액션 · 어빌리티 |
| 총 · 탄창 · 재장전 · 탄도 · 피해 공식 | `Combat/` | 슈터 · (배틀로얄 · 서부극 · 기체 대전) |
| 아이템 · 인벤토리 · 장비 · 전리품 · 제작 · 격자 가방 | `Inventory/` | 배틀로얄 · 위쳐 · 식당 · 생존 공포 · 협동 수집 |
| 레벨 · 스킬 트리 · 평판 · 로그라이트 지도 | `Progression/` | (RPG · 생활 · 택틱스) |
| 퀘스트 · 시간 · 날씨 | `Quest/` · `World/` | (오픈월드 · 생활) |
| 팀 · 점수 · 부활 · 순위 | `Match/` | (대전 · 배틀로얄 · 비대칭) |
| 길찾기 · 군집 · 이동 범위 | `Navigation/` | 도시 건설 · RTS · (SRPG) |
| 행동 트리 · 감각 | `AI/` | RTS · (모든 적 AI) |
| 타이밍 판정 · 턴 순서 · 커맨드 입력 · 록온 · 2D 플랫포머 몸 | `Input/` · `TurnOrder` · `LockOnSelector` · `Movement/` | (리듬 · JRPG · 격투 · 액션 · 플랫포머) |

괄호 안의 장르는 이 공통 부분을 쓰도록 설계했지만 아직 키트가 없는 것입니다(`docs/06_Backlog.md` 의 장르 키트 대기열). 키트 여럿을 섞은 본보기는 `MeadowVillage`(Farming + CreatureLife)다.

알림을 꺼내는 `drainEvents( outListEvent )` 는 기반 · 키트 모두 **받는 쪽 목록 뒤에 붙이고 자기 목록을 비웁니다**(바꿔치기하지 않는다). 매 프레임 같은 목록을 다시 쓰는 쪽은 먼저 `clear()` 합니다.

키트 하나는 장르 묶음 아래 `Kits/<묶음>/<이름>/CMakeLists.txt` 에 `sw_addGameFrameworkKit(GF_<이름>)` 한 줄, `Kits/CMakeLists.txt` 의 `add_subdirectory(<묶음>/<이름>)`,
같은 폴더의 매니페스트 `GF_<이름>.module.json`(이름 · 버전 · `_kind: Kit` · 의존 · 플랫폼 · 구성 · 대상 `_listTarget` — `Source/Games/README.md`)입니다. 시험 실행 파일은 켜진 키트를 레지스트리로 링크합니다. 엔진 없이 돌릴 수 있는 규칙(계산 · 데이터)은
컴포넌트가 아닌 보통 클래스로 두어 시험이 씬 없이 부르게 합니다 — 지금의 키트 넷이 그렇게 되어 있습니다.

### 클라이언트 · 서버로 나뉘는 기능 — 한 기능 = 최대 세 모듈

매니페스트 `_listTarget`(필수)이 모듈이 들어가는 빌드 타깃이다(`Client` · `Server` — 언리얼 모듈 Type 의 ClientOnly · ServerOnly 자리).
CMake 는 빌드 타깃(`SW_TARGET_TYPE` — Game 은 둘 다)과 겹치지 않는 모듈을 짓지 않고, 런타임은 호스트(App = 빌드 마스크, Server 실행 파일 = Server)로 다시 거른다.

- 공유 `GF_<X>`(`["Client", "Server"]`, `Kits/<묶음>/<X>/`) — 메시지 id · 직렬화 · 프로토콜 상수 · 클라이언트 쪽 요청 함수 · 게임플레이. 지금 있는 키트는 모두 공유다.
- 서버 전용 `GF_Server_<X>`(`["Server"]`, `Kits/<묶음>/Server/<X>/`) — 인증 · 세션 표 · 저장소 · 관리 명령. `GF_<X>` 에 의존한다.
- (필요할 때만) 클라이언트 전용 `GF_Client_<X>`(`["Client"]`, `Kits/<묶음>/Client/<X>/`) — UI · 위젯.

의존 · include 는 서버 전용 → 공유 ← 클라이언트 전용 방향만 된다 — `CheckModuleTargets` 가 이름 접두 · 의존 · include 를, `CheckGameFrameworkLayers` 가
키트 사이 include 를 본다(서버 · 클라이언트 키트는 같은 기능의 공유 키트만 include 한다). DB · 캐시 드라이버와 그 서드파티는 `["Server"]` 모듈 안에만 둔다.
## 키트 여럿을 한 게임에 — 소유권 · 순서 · 이름 공간

키트는 게임 전체를 쥐지 않는다. 섞인 게임에서 키트 둘이 같은 돈 · 시간 · 퀘스트를 보려면 다음을 지킨다(조립 시험: `KitCompositionTest`, 시험 게임 `MeadowVillage`).

- **공유 상태는 `GameStateComponent` 하나**(지갑 · 플래그 · 시계 · 일지 · 평판 · 가방 · 날씨 · 땅). 키트 디렉터들과 **같은 오브젝트에 맨 앞**으로 붙인다 — 한 오브젝트의 틱은 붙은 순서로
  한 워커가 돈다(`TickRegistry`). 공유 상태를 만지는 디렉터를 다른 오브젝트에 두면 같은 그룹에서 동시에 돌아 데이터 경쟁이다 —
  씬을 나눠야 하면 뒤쪽 디렉터의 PROPERTY `_tickAfter` 로 앞 오브젝트의 디렉터를 가리킨다(규칙이 서브틱으로 옮겨 그 디렉터 뒤에 돈다).
- **키트 시뮬레이션은 기반 상태를 빌린다**(`const GameStateRefs&` — 지갑 · 가방 · 플래그 · 시계 · 날씨 · 일지 · 평판 · 땅의 포인터 묶음). 제 것으로 들지 않는다 —
  키트 하나만 쓰는 게임은 디렉터가 들고 빌려 준다.
- **땅에 무언가 놓는 키트는 땅을 빌린다**(`bindLand( LandRegistry*, 원점 )` — 밭 · 마을 · 도시 · RTS · SRPG 전장 · 공원 · 복셀). 놓기 전에 얻고 치우면 놓는다 —
  남의 칸에는 놓지 못하고, 남이 막아 둔 칸(건물 · 나무)은 다른 키트의 길찾기가 피한다(RTS 는 땅 리비전이 바뀌면 땅 격자를 다시 칠한다). 키트 자기 격자(밭 칸 · 도시 칸)는 그대로 키트의 것이다.
  액션 플랫포머의 굴착 격자는 옆에서 본 2D 레벨의 충돌 칸이라 월드 XZ 땅이 아니다.
- **입력은 입력 맵 액션으로만**(게임 · 키트 모두 원시 키를 묻지 않는다 — 게임 팩의 `data/<게임>.input.xml`, 이름은 게임 접두). **카메라를 미는 것은 한 디렉터**(또는 기반 카메라 리그 하나) —
  다른 디렉터는 카메라를 만지지 않는다. 같은 키를 두 디렉터가 읽는 일은 액션 이름이 갈라 드러난다.
- **빌린 객체의 알림은 꺼내지 않는다**(`drainEvents` 는 게임 화면의 것). 키트는 상태를 본다(`QuestLog::getStatus`). 시계 알림은 `getClockEvents` 를 여럿이 읽는다.
- **판을 여는 것은 그 오브젝트의 첫 디렉터**(`GameStateComponent::initialize`), 시작값 · 공유 상태를 건드리는 시작 배치는 `isFreshGame()` 일 때만.
- **상태 바이트는 구간**(`StateArchiveUtil::writeSection` — 표 · 판 · 길이). 키트 상태마다 `kStateTag`(4 글자, 저장소에서 하나 — `CheckKitNamespaces`) · `kStateVersion` 을 키트 클래스가 든다.
- **코드 없이 규칙으로 닫은 것** — 자동 플레이 스위치는 게임 전체의 것(디렉터 하나만은 `_bAutoPlay`), 한 판에 경기 흐름(`MatchState`)은 하나(판 사이는 `RoundSeries`),
  키트끼리 서로의 고정 걸음 중간 값을 읽지 않는다(디렉터 틱 경계에서만 공유 상태), 섞인 넷 게임은 롤백 상태 버퍼를 상태 구간으로, 모듈 적재 순서는 의존 순 · 이름순(결정적).
- **이름 공간** — 키트는 키 · 입력 액션 이름을 박지 않고 설정 칸으로 받는다, 키트가 읽는 게임 설정 칸은 `<키트>.` 접두(둘 다 `CheckKitNamespaces`),
  넷 메시지는 키트 영역(`Kits/Network/NetKitMessageRange.h`) — 겹치는 처리기는 라우터가 받지 않는다(`NetMessageRouter::addHandler` 가 false).

## 무엇이 키트에 들어가고 무엇이 기반에 남는가

**의존 관계로는 판별되지 않는다.** 키트 컴포넌트는 하나같이 `Engine` 만 include 하므로, 컴파일러
입장에서는 어디에 둬도 똑같다. 그래서 기준을 적어 둔다 — 적어 두지 않으면 "처음 필요해진 키트"
에 남고, 다른 장르는 그걸 쓰려고 키트를 링크하거나(금지) 복사한다.

기준: **다른 장르의 게임이 이 타입을 그대로 쓰겠는가?**

- 쓴다 → 기반(`Base/`)의 알맞은 폴더 — 수명 · 배선은 `Framework`, 씬 컴포넌트는 그 기능의 폴더(월드 · 장식은 `World`, 카메라는 `Camera`), 계산 도구는 `Utility`, 화면에 뜨는 것은 `UI`.
  형식(컴포넌트냐)으로 묶은 폴더는 두지 않는다 — 의존 방향을 숨긴다(옛 `Components/` 가 카메라 시스템 위에 서 있었다).
  HP 바와 데미지 숫자는 턴제도 쓴다. 중력은 플랫포머도, 탄막도 쓴다.
- 안 쓴다 → 그 키트. 공격 히트박스·투사체·액션 룸처럼 **장르의 규칙을 담은 것**이 여기 해당한다.

## 기반 폴더의 층 — `CheckGameFrameworkLayers`

기반 폴더(`Base/<폴더>`)는 층(DAG)이다. 폴더는 **자기보다 낮은 층**만 include 하고, 같은 층끼리도 서로 모른다. 기반은 키트를, 키트는 다른 키트를 include 하지 않는다
(묶음 공용 헤더 `Kits/<묶음>/x.h` 는 그 묶음 키트만). GameFramework 는 `Games/` · `Editor/` 를 모른다. 표는 `Scripts/lint/gate/CheckGameFrameworkLayers.py` 의
`_kBaseTier` 이고 새 폴더는 층을 정해 넣는다(없으면 실패).

| 층 | 폴더 |
|----|------|
| 0 | `Utility` |
| 1 | `Data` · `Match` · `Navigation` · `Online` · `Spline` |
| 2 | `Framework` |
| 3 | `Combat` · `Input` · `Inventory` · `Movement` · `Progression` · `World` |
| 4 | `AI` · `Appearance` · `Camera` · `Interaction` · `Quest` · `UI` |
| 5 | `Ability` · `Gimmick` · `GameState` |

위층이 알리는 길은 신호다 — 체력 시스템 → HP 바는 `Combat/HealthListenerComponent`, 상호작용 → 기믹 센서는 센서가 완료 수를 끌어 읽는다. 기반을 DLL 여럿으로
나누지는 않는다(층은 폴더로만 지킨다).

## 리플렉션 — 폴더를 늘릴 때

리플렉션 대상 헤더는 소스와 **같은 규칙**으로 모은다(재귀 GLOB). `GameFramework` 는 `Kits/` 를 뺀 모든 헤더를
`GLOB_RECURSE` 로 넘기고(그래서 `Base/` 아래 새 폴더도 저절로 들어간다), 키트(`sw_addGameFrameworkKit`)는 `sw_addReflectionStep` 에 헤더 목록을 넘기지 않고 자동 탐색에 맡긴다.
주의: 폴더를 이름으로 적어 모으면 새 폴더의 `REFLECT()` 타입이 **조용히 등록되지 않는다** — 컴파일은 통과하고 역직렬화만 실패한다.
헤더에 처음 `REFLECT` 를 넣었으면 다시 configure 해야 한다(목록은 configure 때 훑는다).

## 사용법
`Source/Games/내게임/CMakeLists.txt`에서 빌드 시 필요한 키트만 골라서 링크(`target_link_libraries`)하면 해당 기능들을 가져다 쓸 수 있습니다.

## 키트를 만들 때 — 장르의 뼈대이지 게임 하나의 스키마가 아니다

키트는 **장르 전체**가 쓰는 것이라, 게임 하나의 규칙이 타입에 박히면 그 장르의 다른 게임은
이 키트를 못 쓴다. 규칙은 셋이다.

**개수를 코드가 정하지 않는다.** `MonsterDef` 의 사격 패턴(`_listShot`)은 `vector` 이고, XML 은 `<Shot>` 원소를 있는 만큼 읽는다
(한 번에 몇 발인지는 데이터가 정한다).

**종류를 코드가 정하지 않는다.** `MonsterDef` 의 보상은 `_mapDrop` 이고 `<Drop exp="10" souls="3"/>` 처럼
**속성 이름이 곧 보상 이름**이다.

**같은 문제는 같은 방식으로 푼다.** id → 행 조회는 `MonsterCatalog` · `MonsterCollectorCatalog`(`XmlCatalog`) 모두 맵이다. 한 프레임워크 안에서 같은
일을 두 방식으로 하면 읽는 사람이 어느 쪽이 정석인지 알 수 없다.

키트에 새 타입을 넣기 전에 물어볼 것: *이 장르의 다른 게임이 이 필드를 그대로 쓸 수 있나?*
"슬롯 2개", "통화 2종", "스탯 이름 고정" 이 나오면 거의 항상 아니다.
