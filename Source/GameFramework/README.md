# GameFramework (장르 공통 뼈대)

여러 게임에서 반복적으로 사용되는 장르별 공통 로직이나 키트(Kits)가 모여있는 곳입니다.

App은 이 라이브러리를 링크하지 않습니다. 게임플레이 입력은 `Engine/Input/InputMap.h`의 인스턴스를 사용합니다.

## 하위 폴더 구성

`GameFramework` 타겟(= 모든 키트가 깔고 앉는 기반):

- **Ability**: 언리얼 Gameplay Ability System 과 같은 어빌리티 시스템 — `AbilitySystemComponent`(어트리뷰트 · 이펙트 · 어빌리티 · 태그 개수),
  `AttributeSet` · `CombatAttributeSet`, `GameplayEffectDef` · `GameplayEffectSpec`(즉시 · 지속 · 무한 · 주기 · 스택 · 실행 계산), `GameplayAbility`
  (태그 조건 · 비용 · 쿨다운 · 트리거 · 입력) · `AbilityTask`, XML 카탈로그(`AbilityCatalog`). 장르를 가리지 않아 키트가 아니라 기반에 있습니다(턴제는
  틱을 끄고 턴마다 `advanceTime( 1 )`). 같은 오브젝트의 `HealthBarComponent` · `DamageNumberComponent` 와 이어집니다. 자세한 것은 `Ability/README.md`,
  쓰는 예는 `Source/Games/AbilityArena`
- **Framework**: 게임 모듈의 수명과 배선 — `IGame`, `GameInstanceBase`, 서비스 로케이터(`GameService`), 세이브 베이스(`SaveGame`),
  "game" 채널 이벤트(`GameEvents.h` · 내는 길 `GameEventUtil`), 모드 전이(`GameModeStateMachine` — 일시정지 진입 · 해제에 `GamePausedEvent` · `GameResumedEvent`),
  화면 전환(`ScreenTransitionManager`). `GameInstanceBase` 가 세이브 · 로드 완료와 씬 로드 요청 · 완료를 그 자리에서 낸다. `onInitialize` 뒤에 사용자 설정을 다시 넣는다(`UserSettingsManager::reapplyAll` — 언어 · 입력 맵이 그때 선다). 공유 타입은 루트의 `GameFrameworkMinimal.h`
- **Components**: 장르 무관 씬 컴포넌트 — `FadeOutComponent`, `GravityComponent`, `DontDestroyOnLoadComponent`, 비스듬히 내려다보는 직교 카메라
  (`OrthoCameraRigComponent` — WASD · 방향키 이동(WASD 끄기 · 초점 범위 묶기), 휠 확대(`setOrthoHeight` 도 같은 범위), Q/E 90° 회전(단계 0 이면 끈다), 다른 컴포넌트가 앞 틱 그룹에서 넣는 원근 시점 덮어쓰기, 화면 점 → 땅 점 `findGroundPoint`(마우스 고르기)), 장식 흩뿌리기
  (`PropScatterComponent` — 씨앗 고정 배치를 영역 가장자리 · 안쪽에, 제외 원, 플레이 시작에 세우고 끝에 걷는다). 계산은 `OrthoCameraRigMath` · `PropScatterMath` 로
  떼어 씬 없이 시험한다. 1인칭 카메라(`FirstPersonCameraComponent` — 마우스 시점 · 피치 한계 · 마우스 잠금(Esc) · 눈 자리 · 손에 든 뷰 모델 자리, 계산은
  `FirstPersonCameraMath`). 시점 자체는 `Input/FirstPersonLook` 이고, 몸을 움직이는 게임 컴포넌트가 같은 오브젝트의 뒤 그룹에서 시점을 읽고 눈 자리를 넣는다
- **Camera**: 데이터 카메라 — 프리셋(`CameraPresetDef` · `CameraPresetCatalog`), 블렌드 곡선 · 포즈 섞기(`evaluateBlendWeight` · `blendPoses`), 블렌드 · 감쇠
  상태 기계(`CameraDirector`), 그것을 카메라에 쓰는 `CameraDirectorComponent`. 아래 "카메라" 절
- **Stage**: 절차로 무대를 세우는 도우미(`PrimitiveStage` — 활성 씬 잡기 · 세운 오브젝트 추적 · 색 · 텍스처 머티리얼 인스턴스 캐시 · 해 · 카메라). 시험 게임이 쓴다
- **Utility**: 장르 무관 계산 도구 — 씨앗 고정 난수 · 좌표 해시(`GameRandom` · `GameHash` — 가중치 고르기 `pickWeightedIndex` · 섞기 `shuffle`), 값 노이즈(`ValueNoise`),
  광선 판정(`RayMath` — 구 · 상자 · 바닥 평면 · 원뿔), 앞 · 위 → 오일러(`OrientationUtil` — 롤이 있는 차량 · 카메라), 2D 네 방향(`FacingDir`),
  고정 스텝 누적기(`FixedStepTimer`), 게임 시간 타이머(`TimerQueue`). 셋 이상의 키트에 같은 것이 따로 있던 것을 모았다(아래 "새 장르 키트")
- **Input**: 커맨드 입력(`InputCommandParser` — 철권 표기 · `InputCommandBuffer` — 새로 넣기 · 누른 채 · 동시 버튼 · 틱 한도 · 좌우 뒤집기 · 상태 바이트, 결정적),
  타이밍 판정(`TimingJudge` — 리듬 · 타이밍 공격 · 스킬 체크 · 저스트 프레임), 1인칭 시점(`FirstPersonLook`)
- **Data**: 데이터를 읽고 담는 틀 — `GameSettings`, `GameStrings`, 데이터 XML 읽기(`GameDataXml` — 문서 · 루트 · id 확인 · 숫자 목록 · 토큰 목록), id 카탈로그(`GameCatalog<T>` —
  읽은 순서 + 해시 조회), 이름 → 수치(`StatBlock` — 여러 자원 비용 `canAfford` · `trySpend`)
- **AI**: 블랙보드(`Blackboard`), 행동 트리(`BehaviorTree` 정의 · `BehaviorTreeRunner` 실행 — 반응형 셀렉터 · 관찰 중단 · 데코레이터), 감각(`AiPerception` — 시야 각 ·
  거리 · 가림 · 소리 · 기억), 스폰 감독(`SpawnDirector` — 시간에 따라 쌓이는 예산 · 곡선 · 종류 상한 · 태그)
- **Combat**: 무기 정의 · 상태(`WeaponCatalog` · `WeaponState` — 연사 · 탄창 · 재장전 · 퍼짐 · 산탄 · 거리 감쇠 · 머리 배율 · 탄속 · 탄 아이템), 탄 퍼짐(`WeaponMath`),
  피해 공식(`DamageMath`), 탄도(`Ballistics` — 낙차 · 발사각 · 앞 겨누기), 턴 순서(`TurnOrder` — 라운드제 · 타임라인제), 록온(`LockOnSelector`),
  체력 상태(`Vitality` — 실드 · 기절 → 출혈 → 부활 · 최대 기절 횟수 · 무적 · 경직 게이지 · 최대 체력 바꾸기), 자원 게이지(`ResourceGauge` — 스태미나 탈진 · 과열 · 회복 배율 · 즉시 깎기),
  프레임 데이터(`MoveCatalog` · `MoveTimeline` — 발생 · 지속 · 경직 · 캔슬 · 히트스톱 · 가드 높이 · 상태 복원), 속성 상성(`ElementChart` — 복합 속성 곱 · 면역 · 상태이상 확률)
  슈터 · 배틀로얄 · 서부극 · 기체 대전 · JRPG · 포켓몬 · 젤다가 함께 쓴다(예전 `GF_Shooter` 키트의 무기는 여기로 옮겼다)
- **Inventory**: 아이템 봉투(`ItemBag` — id → 개수), 아이템 카탈로그(`ItemCatalog` — 분류 · 겹침 · 무게 · 희귀도 · 장비 칸 · 내구도 · 태그 · 능력치), 칸 인벤토리(`Inventory`), 격자 가방(`GridInventory` — w × h · 돌리기 · 빈자리 찾기 · 겹침, 모양은 연결 함수), 장비(`Equipment` — 칸 배치는
  데이터), 전리품 표(`LootCatalog` — 가중치 · 없음 · 늘 주기 · 표 안의 표 · 행운), 제작(`RecipeCatalog` · `Crafter` — 작업대 · 레벨 · 도구 · 배우기 · 대기열 · 재료를 거두는 쪽 바꾸기),
  지갑 · 가게(`Wallet` · `ShopCatalog` · `ShopState` — 여러 통화 · 재고 · 재입고 · 매입 시세 하락과 회복 · 조건은 `IShopConditionEvaluator`)
- **Match**: 판 규칙(`MatchState` — 팀 · 역할 · 점수 · 도움 · 부활 대기 · 코스트 게이지 · 탈락 순위 · 시간 제한 · 목표로 끝내기)
- **Movement**: 2D 플랫포머 몸(`PlatformerMotor2D` · `PlatformTileMap` — 점프 높이 · 짧은 점프 · 코요테 · 미리 누르기 · 벽 점프 · 대시 · 다단 점프 · 한쪽 발판 · 사다리),
  아케이드 차량(`ArcadeVehicleMotor` — 속도에 따른 조향 · 드리프트 미니터보 단계 · 니트로 · 오프로드 · 점프, 지면은 `IVehicleGround`)
- **Navigation**: 격자(`NavGrid`), A*(`GridPathfinder`), 흐름장(`FlowField`), 걷는 행위자(`NavAgent` · `Steering`), SRPG 이동 범위(`GridReachability`)
- **Progression**: 경험치 곡선 · 레벨(`ExperienceCurve` · `LevelProgress`), 스킬 트리(`SkillTreeCatalog` · `SkillTreeState`), 평판 · 호감도(`ReputationCatalog` ·
  `ReputationState`), 로그라이트 지도(`RunMap`)
- **Quest**: 퀘스트(`QuestCatalog` · `QuestLog` — 선행 · 레벨 · 단계 · 목표 · 선택 목표 · 분기 · 보상 알림 · 시간 제한 · 반복)
- **World**: 시계(`WorldClock` — 시 · 때 · 날 · 계절 · 해 · 햇빛 · 잠), 날씨(`WeatherCatalog` · `WeatherSystem` — 계절 가중치 · 섞기 · 예보),
  방 · 지역 그래프(`AreaGraph` — 잠금 조건 · 일방통행 · 발견 · 탐색률 · 막힌 경계 · 코드로 짓기 · 다른 XML 안에 적기), 진행형 상호작용(`InteractionProgress` — 여럿 · 끊김 · 퇴행 · 스킬 체크),
  월드 플래그와 조건식(`GameFlags` — `a && !b || count>=3`)
- **UI**: 장르 무관 UI 컴포넌트 — `RuntimeHud`, `DialogueRunnerComponent`,
  `HealthBarComponent`, `DamageNumberComponent`. HP 바 · 데미지 숫자는 월드 공간 스프라이트(`SpriteInstanceBatch`)로 그린다 — 저장되는
  컴포넌트를 만들지 않는다. 입력은 `HealthBarComponent::setTargetRatio` · `DamageNumberComponent::setDamageValue` 하나씩이다.
  `FadeOutComponent` 의 흐림은 같은 오브젝트 스프라이트들의 색 알파에 곱해진다.

별도 타겟:

- **Kits**: 키트끼리 링크하지 않음. 공유 타입은 기반(`Framework` · `Components` · `Utility` · `UI` …)으로. 장르 묶음 폴더 아래 키트 하나씩입니다(`Kits/<묶음>/<키트>`, 타겟은 `GF_<키트>`).
  - **액션 · 대전** (`Kits/Action/`)
    - `ActionCombat`: 공격 히트박스(`MeleeHitboxComponent`), 투사체, 유닛 스탯, 액션 룸.
      피해는 한 길이다 — 투사체(`ProjectileComponent`)와 공격 판정은 같은 오브젝트의 `BoxCollider2DComponent` 겹침으로 맞음을 알고
      `UnitStatsComponent::takeDamage( 피해, 쏜 쪽 )` 을 부르며, HP 가 깎인 그 자리에서 컴포넌트 델리게이트(`registerDamageApplied`)가 불리고
      `DamageAppliedEvent`("game" 채널)가 나간다. 액션 룸은 시작 · 클리어 · 패배에 룸 이벤트를 낸다. 채널 이벤트는 `GameEventUtil::send` 하나로 낸다 —
      버스 스레드면 그 자리에서, 아니면 다음 `processEvents` 에.
    - `ActionAdventure`: 액션 어드벤처(젤다 장르) — 던전 열쇠 · 조건 문 · 지도/나침반 · 장치(`AdventureDungeon`), 하트 조각 · 마법 · 스태미나 탈진(`AdventureVitals`), 주목 옆걸음 · 회피(`AdventureTargeting`), 불 번짐 · 전기 · 얼음 셀 자동자(`AdventureElementGrid`), 효과 합산 요리(`AdventureCooking`), 무기 내구도(`AdventureWeaponWear`), 탑 · 사당 · 증표(`AdventureWorldMap`).
    - `ActionPlatformer`: 스테이지형 액션 플랫포머(검브렐라 · 페퍼 그라인더 · 어스블레이드 장르) — 체크포인트 · 목숨 · 비밀 수집 · 등급(`ActionStageRun`), 활공 · 갈고리 진자 · 드릴 이동(`ActionPlatformerBody`), 근접 콤보 · 총 · 패리 반사(`ActionCombatRig`), 데이터 적 패턴(`ActionEnemyBrain`).
    - `Metroidvania`: 메트로배니아 · 2D 소울라이크(블라스퍼머스 2 · 더 라스트 페이스 · 엠버베인 장르) — 능력 잠금(`MetroAbilitySet`), 탐색률 · 지도 구매(`MetroMapState`), 휴식 · 시체 · 물약(`MetroSoulsState`), 스태미나 · 패리 · 강인도(`MetroDuelist`), 부적 슬롯(`MetroCharmLoadout`).
    - `Fighting`: 3D 격투(철권 장르) — 캐릭터 데이터(커맨드 · 자세 · 조건 · 스트링, `FighterCatalog`), 60프레임 결정적 시뮬레이션(가드 높이 · 프레임 이득 · 저글 감쇠 · 스크류 · 벽꽝 · 잡기 풀기 · 횡이동 · 레이지 · 히트) · 라운드 · 롤백 상태 저장/복원(`FightingMatch`).
    - `MechArena`: 3인칭 팀 기체 대전(SD건담 캡슐파이터 장르) — 기체 · 형태 · 무기 칸 · 분류 보정 · 스킬(`MechCatalog`), 부스트 오버히트 · 다운치 · 기상 무적 · 록온 유도 · 근접 콤보 · 변형 · 팀 전력 게이지 · 기체 교체(`MechArenaWorld`), 상태 바이트(`MechArenaSnapshot`).
    - `BattleRoyale`: 배틀로얄(배틀그라운드 장르) — 자기장 단계 · 다음 원 고르기(`BrZone`), 비행기 경로 · 낙하 · 착지 예측(`BrDrop`), 지점별 전리품 · 보급 상자(`BrLoot`), 방어구 · 가방 등급 · 탄약(`BrGear`), 기절 · 팀원 부활 · 순위 · 킬 피드(`BrMatch`).
  - **공포 · 조사** (`Kits/Horror/`)
    - `SurvivalHorror`: 생존 공포 · 조사(바이오하자드 · 홀스틴 · 애니그마 오브 피어 장르) — 격자 가방(기반 `GridInventory`), 아이템 상자 · 조합 · 세이브 제한 · 정신력/손전등 · 열쇠 문 · 다이얼/순서 퍼즐 · 단서 보드 추리(`HorrorSession`), 턴제 초자연 전투(`HorrorEncounter`).
    - `AsymmetricHorror`: 비대칭 공포(데드 바이 데이라이트 장르) — 규칙 · 살인마 · 점수 XML(`AsymmetricHorrorRules`), 건강 → 부상 → 빈사 → 갈고리 단계 · 몸부림 · 구출, 다인 수리 · 치료 · 스킬 체크 · 걷어차기 퇴행, 판자 · 창틀 · 사물함, 탈출구 · 해치 · 붕괴(`HorrorMatch`), 상태 바이트(`HorrorSnapshot`).
    - `CoopScavenger`: 협동 수집 공포(리썰 컴퍼니 장르) — 할당량 주기(`ScavengerQuota`), 위성 · 하루 시각 · 날씨 · 위협 예산 · 죽음과 시신 회수 · 전멸 손실(`ScavengerExpedition`), 절차 시설 방 그래프 · 고철(`ScavengerFacility`), 운반 칸 · 양손 · 무게(`ScavengerCarry`).
    - `GhostHunt`: 유령 사냥(루이지 맨션 장르) — 손전등 원뿔 · 스트로브 기절 · 흡입 줄다리기 · 강화 단계(`GhostEncounter`), 방 불 켜기 · 열쇠 문 · 가구 보물 · 부 탈출(`GhostMansion`), XML(`GhostCatalog`).
  - **RPG** (`Kits/Rpg/`)
    - `Overworld`: 오픈월드형 필드 탐색 시스템
    - `TurnBattle`: 턴제 전투 시스템
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
    - `Farming`: 농장 생활(하베스트 문 장르) — 달력(`FarmCalendar`: 6:00–26:00 하루 · 28 일 계절 · 해), 작물 XML 카탈로그(`CropCatalog`),
      밭(`FarmField`: 갈기 · 물 · 심기 · 거두기, 물 받은 날만 자람, 다시 열림, 철 지나면 시듦, 비), 인벤토리 · 출하 정산(`FarmInventory`).
    - `CreatureLife`: 생물 생활(포코피아 · 문스톤 아일랜드 장르) — 칸 패턴 서식지 레시피(회전 · 큰 것 먼저), 시간대 · 날씨 방문(결정적), 생물별 호감도(`ReputationState`) · 부탁(`QuestLog`), 능력 칸 변환 · 하루 횟수, 집 배정, 마을 매력도 단계(`CreatureTown`).
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
    - `PartyArena`: 파티 아레나(바이킹 온 트램펄린 장르) — 트램펄린 튕김 타이밍 콤보 · 공중 공격 · 내려찍기 · 링 아웃 점수 · 락스텝 입력(`TrampolineArena`), 씨앗 무작위 아이템(`PartyItemSpawner`), 라운드 목록 · 순위 점수 · 먼저 N 점 우승(`PartyRoundSeries`).
    - `KartRacing`: 카트 레이싱(카트라이더 · 마리오카트 장르) — Catmull-Rom 트랙 · 체크포인트 · 오프로드(`KartTrack`), 순서 랩 · 실시간 순위 · 역주행(`KartRace`), 순위 가중 아이템(`KartItems`), AI 레이싱 라인 · 드리프트 · 러버밴딩(`KartAi`), 고스트(`KartGhost`).
  - **네트워크 방식** (`Kits/Network/`)
    - 네트워크 방식(장르별로 골라 링크 — 싱글 게임은 링크하지 않는다, 공통 계층은 `Core/Network`). 정책은 가상 인터페이스로 게임이 바꾼다.
      메시지 첫 바이트는 키트마다 영역이 나뉘어(`NetMessageRange`) 한 게임이 둘을 같이 써도 섞이지 않는다. 키트의 서버 · 클라이언트는 모두 `INetMessageHandler` 라
      `NetMessageRouter` 에 걸어 두면 `pump( host )` 가 영역대로 나눠 주고 게임 메시지(0x80..)만 돌려준다(`handleMessage( buffer )` 를 직접 불러도 된다 — 제 것만 먹고 false).
      보낼 메시지는 키트마다 `NetMessageWriter` 하나를 다시 쓰고, 서버의 스냅샷 · 관심 영역 계산도 매 틱 목록을 새로 잡지 않는다.
      **멀티스레드**: `NetHost` 는 아무 스레드에서나 부를 수 있고 `NetHostThread` 가 게임 프레임과 따로 돌린다(Core README). 서버 쪽
      `ReplicationServer` · `MmoReplicator` 는 `setTaskManager( &engine::getTaskManager() )` 를 주면 연결(관찰자)마다의 스냅샷 · 관심 영역 계산을
      작업 스레드에 나눈다 — 결과는 한 스레드와 바이트까지 같고(`NetParallelTest`), 관찰자 128 · 엔티티 8000 에서 틱당 4.4 → 1.5 ms(워커 3).
      그때 정책(`IReplicationPolicy` · `IInterestPolicy`)은 여러 스레드에서 동시에 불리므로 읽기만 한다.
      - `NetClientServer`: 권위 서버(슈터 · 배틀로얄 · 액션 · 기체 대전 · 비대칭) — 스냅샷 델타(확인된 기준 대비 · 예산 · 우선도, `IReplicationPolicy` 관련성),
        보간(`ReplicationClient` — 지연만큼 과거 · 시계 맞추기), 입력 겹쳐 보내기, 클라이언트 예측 되맞추기(`ClientPrediction`), 랙 보정 되감기(`LagCompensationHistory`).
      - `NetLockstep`: 결정적 — 락스텝(`LockstepSession` — 입력 지연 · 체크섬 비동기 감지, RTS), 롤백(`RollbackSession` · `IRollbackGame` — 예측 · 되감기 · 재시뮬레이션, 격투).
      - `NetTurnRelay`: 턴제 중계(카드 · 보드 · SRPG) — 방 · 자리 · 표, `ITurnPolicy`(차례 · 허락 · 방향), 행동 기록 방송, 재접속 시 놓친 행동.
      - `NetMmo`: MMO — 관심 영역 격자(`InterestGrid`, 들어옴 · 나감 히스테리시스), 우선도 누적 대역폭 예산, `IInterestPolicy`(늘 보이기 · 우선도).

## 카메라: 프리셋 데이터 + 블렌드 + 디렉터

카메라 시점은 코드가 아니라 `<CameraPresets>` XML 이다(Cinemachine 가상 카메라 + Custom Blends, 언리얼 카메라 모드의 자리). 예시는
`Resource/engine/cameras/default.cameras.xml`.

- **프리셋 하나 = 섹션 원소 몇 개.** `<View>`(모드 `Fixed` · `OrthoTopDown` · `Orbit` · `Follow` · `FirstPerson`, 피치 · 요 · 거리 · 오프셋) · `<Lens>`(시야각 ·
  직교 높이 · 투영 · 근/원평면) · `<Damping>`(자리 · 회전 시간 상수) · `<BlendIn>`. 섹션마다 구조체 하나(`CameraViewDef` …)라 프레이밍 · 제약 · 충돌 · 흔들림 같은
  다음 섹션은 구조체 하나와 원소 하나를 더하면 된다. XML 의 각은 도, 정의는 라디안이다. 모르는 속성 · 원소 · 열거자는 경고한다.
- **블렌드 고르기**: `<Blend from to>` 표(정확히 → `from="*"` → `to="*"`) → 들어가는 프리셋의 `<BlendIn>` → `<DefaultBlend>`. 곡선은 `CameraBlendCurve`
  (Cut · Linear · EaseIn/Out/InOut · SmoothStep · Cubic · Exponential · Spring · Custom 키). `evaluateBlendWeight` · `blendPoses` 는 컴포넌트를 모르는 함수라
  시퀀서도 같은 곡선을 쓴다. 직교 ↔ 원근은 섞지 않고 가중치 0.5 에서 바꾼다.
- **블렌드는 지금 화면에서 출발한다**(`CameraDirector`): 나가는 프리셋은 블렌드 동안 대상을 계속 따라가고, 블렌드 도중 다시 켜면 그 순간의 섞인 포즈를 고정해 출발점으로
  둔다 — 어느 쪽이든 켠 순간 튀지 않는다. 감쇠는 지수 감쇠(1 − e^(−dt/τ))라 프레임 수와 상관없다.
- **컴포넌트**: `CameraDirectorComponent` 를 `CameraComponent` 와 같은 오브젝트에 붙이고 프리셋 경로 · 시작 프리셋 · 대상(`GameObjectHandle`)을 준다.
  `TickGroup::PostUpdate` 에서 자기 카메라만 월드 값으로 쓰고 대상은 읽기만 한다. 게임은 `activatePreset( id )` 로 바꾼다.
- 기존 `OrthoCameraRigComponent` · 1인칭 카메라는 아직 따로 돈다 — 모드로 옮기는 것은 `docs/06_Backlog.md` 1-6 의 카메라 항목.

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
| 아이템 개수 | `ItemBag` | 농장 인벤토리 · 출하함 |
| 절차로 세우는 시험 무대 | `PrimitiveStage` | 시험 게임 다섯(씬 · 프리팹으로 옮기기 전 — `Source/Games/README.md`) |
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

괄호 안의 장르는 이 공통 부분을 쓰도록 설계했지만 아직 키트가 없는 것입니다(`docs/06_Backlog.md` 의 장르 키트 대기열).

알림을 꺼내는 `drainEvents( outListEvent )` 는 기반 · 키트 모두 **받는 쪽 목록 뒤에 붙이고 자기 목록을 비웁니다**(바꿔치기하지 않는다). 매 프레임 같은 목록을 다시 쓰는 쪽은 먼저 `clear()` 합니다.

키트 하나는 장르 묶음 아래 `Kits/<묶음>/<이름>/CMakeLists.txt` 에 `sw_addGameFrameworkKit(GF_<이름>)` 한 줄, `Kits/CMakeLists.txt` 의 `add_subdirectory(<묶음>/<이름>)`,
`Config/App/AppConfig.json` 의 `_listGameKitModule`, 시험은 `Test/EngineTest/CMakeLists.txt` 의 `LIBS` 입니다. 엔진 없이 돌릴 수 있는 규칙(계산 · 데이터)은
컴포넌트가 아닌 보통 클래스로 두어 시험이 씬 없이 부르게 합니다 — 지금의 키트 넷이 그렇게 되어 있습니다.

## 무엇이 키트에 들어가고 무엇이 기반에 남는가

**의존 관계로는 판별되지 않는다.** 키트 컴포넌트는 하나같이 `Engine` 만 include 하므로, 컴파일러
입장에서는 어디에 둬도 똑같다. 그래서 기준을 적어 둔다 — 적어 두지 않으면 "처음 필요해진 키트"
에 남고, 다른 장르는 그걸 쓰려고 키트를 링크하거나(금지) 복사한다.

기준: **다른 장르의 게임이 이 타입을 그대로 쓰겠는가?**

- 쓴다 → 기반의 알맞은 폴더 — 수명 · 배선은 `Framework`, 씬 컴포넌트는 `Components`, 계산 도구는 `Utility`, 화면에 뜨는 것은 `UI`.
  HP 바와 데미지 숫자는 턴제도 쓴다. 중력은 플랫포머도, 탄막도 쓴다.
- 안 쓴다 → 그 키트. 공격 히트박스·투사체·액션 룸처럼 **장르의 규칙을 담은 것**이 여기 해당한다.

## 리플렉션 — 폴더를 늘릴 때

리플렉션 대상 헤더는 소스와 **같은 규칙**으로 모은다(재귀 GLOB). `GameFramework` 는 `Kits/` 를 뺀 모든 헤더를
`GLOB_RECURSE` 로 넘기고, 키트(`sw_addGameFrameworkKit`)는 `sw_addReflectionStep` 에 헤더 목록을 넘기지 않고 자동 탐색에 맡긴다.
주의: 폴더를 이름으로 적어 모으면 새 폴더의 `REFLECT()` 타입이 **조용히 등록되지 않는다** — 컴파일은 통과하고 역직렬화만 실패한다.
헤더에 처음 `REFLECT` 를 넣었으면 다시 configure 해야 한다(목록은 configure 때 훑는다).

## 사용법
`Source/Games/내게임/CMakeLists.txt`에서 빌드 시 필요한 키트만 골라서 링크(`target_link_libraries`)하면 해당 기능들을 가져다 쓸 수 있습니다.

## 키트를 만들 때 — 장르의 뼈대이지 게임 하나의 스키마가 아니다

키트는 **장르 전체**가 쓰는 것이라, 게임 하나의 규칙이 타입에 박히면 그 장르의 다른 게임은
이 키트를 못 쓴다. 규칙은 셋이다.

**개수를 코드가 정하지 않는다.** `SpeciesDef` 의 기술 · `PartyMember` 의 PP 는 `vector` 이고, XML 은 `move0`, `move1`, ... 을
끊길 때까지 읽는다(슬롯 수는 데이터가 정한다). 세이브에는 개수(`ppCount`)를 함께 적는다.

**종류를 코드가 정하지 않는다.** `MonsterDef` 의 보상은 `_mapDrop` 이고 `<Drop exp="10" souls="3"/>` 처럼
**속성 이름이 곧 보상 이름**이다. `RuntimeHud` 가 게이지를 이름 맵으로 다루는 것과 같은 방식이다.

**같은 문제는 같은 방식으로 푼다.** id → 행 조회는 `MonsterCatalog` · `SpeciesCatalog` 모두 맵이다. 한 프레임워크 안에서 같은
일을 두 방식으로 하면 읽는 사람이 어느 쪽이 정석인지 알 수 없다. 다만 인덱스가 직렬화되는 곳(`SpeciesDef::_listMoveIndex`)은
**벡터의 자리를 그대로 두고** 맵을 곁에 둔다.

키트에 새 타입을 넣기 전에 물어볼 것: *이 장르의 다른 게임이 이 필드를 그대로 쓸 수 있나?*
"슬롯 2개", "통화 2종", "스탯 이름 고정" 이 나오면 거의 항상 아니다.
