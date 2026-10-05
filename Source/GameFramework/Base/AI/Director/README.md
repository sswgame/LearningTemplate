# AI/Director — 페이싱 감독(AI 디렉터)

플레이어의 긴장도를 읽어 **언제 · 얼마나 · 무엇을** 낼지 정하는 장르 공통 감독입니다. 레프트 4 데드 AI 디렉터(긴장도 → 쌓기 · 절정 유지 · 쉼),
RDR2 무작위 조우(가중 풀 · 쿨다운 · 시각 · 장소 · 플레이어 상태 조건), 로그라이크 방 감독(보상 밀도 예산)의 공통 부분입니다. 스폰 자체는
`AI/SpawnDirector`(예산 · 곡선 · 상한 · 태그)가 하고, 조건은 일정의 `ScheduleCondition`(요일 · 계절 · 날씨 · 하루의 때 · 플래그 식 · 태그 · 확률),
난수는 `GameRandom`, 하루의 때는 `WorldClock` 을 씁니다 — 감독은 그 위의 페이싱 층입니다. 조우 id 의 뜻(무엇을 몇 기 · 어떤 보상)은 게임이 압니다.

| 파일 | 하는 일 |
|------|---------|
| `AiDirectorProfile` | `*.director.xml` — 긴장도 신호 · 단계와 나가는 길 · 단계 곡선 · 풀(단계 진입 · 주기 · 예산)과 항목. 모르는 이름은 로드 오류 |
| `AiDirectorIntensity` | `IAiDirectorIntensityModel`(게임이 끼울 수 있는 긴장도 모델) + 데이터로 도는 기본 모델 |
| `AiDirector` | 런타임 — 단계 넘기기 · 스폰 예산 배율 · 풀 고르기 · 사건 · 추적 · 설명 · 상태 해시 |

## 데이터

```xml
<AiDirector startPhase="BuildUp">
  <Calendar weathers="sunny,rain"/>                                    <!-- 조건 이름 검사(선택, 일정과 같은 원소) -->
  <Intensity max="1" decayPerSecond="0.06" decayDelay="4">
    <Signal id="damageTaken" kind="impulse" scale="0.012" combat="true"/>   <!-- addSignal(양) 한 번에 양 × scale -->
    <Signal id="enemiesNear" kind="rate" scale="0.01" max="0.08" combat="true"/> <!-- setSignal(값) 동안 초마다 값 × scale -->
    <Signal id="lowAmmo" kind="level" scale="0.25"/>                       <!-- 긴장도의 바닥 -->
  </Intensity>
  <Phase id="BuildUp" spawnScale="1" spawnTags="Drone">
    <Curve time="0" scale="0.6"/><Curve time="30" scale="1.5"/>            <!-- 단계 안 시간 → 스폰 배율 -->
    <Exit to="Peak" intensityAbove="0.7" minTime="8"/>                     <!-- 적은 절이 모두 참이면 나간다 -->
    <Exit to="Peak" minTime="45"/>                                         <!-- 시간 길(늘 언젠가는 절정) -->
  </Phase>
  <Phase id="Peak" spawnScale="1.5"><Exit to="Relax" minTime="8"/></Phase>
  <Phase id="Relax" spawnScale="0" rewardScale="2">
    <Exit to="BuildUp" intensityBelow="0.3" calmFor="4" minTime="6"/>
  </Phase>
  <Pool id="surge" trigger="phaseEnter" phase="Peak">
    <Encounter id="swarm" weight="3" count="4"/>
    <Encounter id="elite" weight="1" count="2" scale="2.5" minCycle="1" cooldown="60"/>
  </Pool>
  <Pool id="ambient" trigger="interval" interval="20" chance="0.5" cooldown="30" pacing="BuildUp,Relax">
    <Encounter id="ambush" areas="Forest,Road" phases="Night" tags="Player.Mounted" notTags="Player.Wanted" weathers="rain"/>
  </Pool>
  <Pool id="field" kind="reward" trigger="budget" perMinute="1" maxBudget="2" need="lowAmmo" needScale="2">
    <Encounter id="repair" cost="1" maxIntensity="0.6"/>
  </Pool>
</AiDirector>
```

- **신호** `kind`: `impulse`(한 번 · 상한 `max`) · `rate`(초마다, 상한 `max`/s) · `level`(바닥). `combat="true"` 신호가 들어오면 "싸움 뒤 지난 시간" 이 0 이 되고,
  그 뒤 `decayDelay` 초가 지나야 `decayPerSecond` 로 식습니다(레프트 4 데드: 싸우는 동안은 식지 않는다). 긴장도 = max(쌓인 스트레스, Σ 바닥), 상한 `max`.
- **단계** 이름 · 수는 데이터가 정합니다(세 단계일 필요가 없다). `spawnScale` × `<Curve>`(단계 안 시간) 가 스폰 감독 예산 배율이고, **0 이면 쌓지도 내지도 않습니다**
  (쉬는 동안 모아 둔 예산으로 내지 않는다). `spawnTags` 가 그 단계에서 낼 스폰 항목의 태그입니다. 시작 단계로 돌아올 때마다 순환 수(`getCycle`)가 하나 오릅니다.
- **나가는 길** `<Exit>` 절: `minTime`(단계에 머문 시간) · `intensityAbove` · `intensityBelow` · `calmFor`(마지막 싸움 뒤). 적은 순서로 보고 먼저 맞는 길로 나갑니다.
- **풀** `trigger`: `phaseEnter`(단계에 들어설 때 `picks` 번) · `interval`(`interval` 초마다 `chance`) · `budget`(예산이 `perMinute` × 단계 배율 × (1 + `needScale` × 필요 신호)로
  쌓이고 미리 골라 둔 항목의 `cost` 에 닿으면 낸다 — 싼 것만 계속 나오지 않게). 단계 배율은 `kind="reward"` 면 `rewardScale`, 아니면 `spawnScale`.
  `cooldown` 은 풀에서 두 번 고르는 사이, `pacing` 은 그 단계에서만 도는 것(밖에서는 시계 · 예산이 멈춘다).
- **항목** 조건: `weight` · `cooldown` · `maxCount`(한 판) · `minCycle` · `minTime` · `minIntensity`/`maxIntensity` · `areas`(지금 지역 태그 중 하나) · `pacing` +
  일정 조건 속성 전부(`days` · `daysOfSeason` · `seasons` · `weathers` · `phases`(하루의 때) · `flags` · `tags` · `notTags` · `chance`). 게임에 넘기는 값: `count` · `scale`.
- 검사: 모르는 원소 · 속성 · 신호 종류 · 풀 종류/트리거 · 단계(시작 · 나가는 길 · 풀 · 항목의 `pacing`) · 필요 신호, 겹친 id, 단계 없음 → **로드 오류**(경고 + false).
  `ResourceDataSchemaTest` 가 `Resource/` 아래 모든 `*.director.xml` · `*.spawns.xml` 을 읽습니다.

## 런타임

```cpp
AiDirector director;
director.initialize( &profile, &spawnTable, seed );               // 테이블은 없어도 된다(조우만 고르는 감독)
director.getBuiltinIntensityModel().addSignal( "damageTaken", 12 ); // 게임이 신호를 넣는다(또는 setIntensityModel 로 자기 모델)
director.setContext( context );                                    // 지역 태그 · 시계(fillFromClock) · 플래그 · 플레이어 태그 · 날씨
director.update( dt );
director.drainEvents( listEvent );                                 // PhaseChanged · Spawned · Despawned · Encounter · Reward
director.notifyDespawned( spawnId );                               // 스폰 개체가 사라지면
```

- **한 프레임의 순서**: 긴장도 모델 → 시간 → 단계 넘기기(한 프레임 최대 4) → 스폰 → 풀(Interval · Budget). PhaseEnter 풀은 단계에 들어선 그 자리에서 고릅니다.
- **결정성**: 씨앗이 같고 같은 dt · 신호 · 문맥이면 같은 사건을 같은 때에 냅니다. 스폰 감독 씨앗은 감독 씨앗에서 나오고, 조건의 확률 절은 (씨앗 · 풀 · 항목 · 고른 횟수)의
  해시로 굴립니다. `computeStateHash` 로 확인합니다.
- **추적**: `dumpTrace`(최근 256 사건 — `[12.30s] I=0.82 PhaseChanged 'Peak' from 'BuildUp' exit 0 cycle 1`), `explain`(단계 · 긴장도 · 나가는 길마다 막은 절 ·
  풀마다 항목의 `+`/`-` 와 까닭 `[cooldown] [area] [condition]` …), `computeBlockMask`(같은 판정의 비트). `-gv_aiDirectorTrace=1` 이면 사건마다 로그 한 줄(시험용).

## 쓰는 곳

- `Shooter3D` — 스켈레톤 웨이브를 감독이 낸다(`Resource/game/shooter3d/data/arena.director.xml` · `arena.spawns.xml`). 신호는 맞은 피해 · 쓰러뜨린 적 ·
  가까운 적 수 · 탄 부족, 사건은 스켈레톤 스폰 · 무리/정예(절정 진입) · 탄(쉼 진입) · 수리(예산). 자동 플레이(`-gv_shooterAutoPlay=1`)는 씨앗이 고정이라 같은 프레임
  시간이면 같은 페이싱이다(실제 프레임 시간은 실행마다 다르다).

## 상용 엔진과 견주면

- 레프트 4 데드 AI 디렉터: 긴장도(피해 · 근처 처치) → 절정 유지 → 쉼, 쉬는 동안 일반 스폰 없음, 무리(호드) 사건, 보급 밀도 — 같은 틀이다. 디렉터가 길(내비)을 읽어 플레이어
  앞뒤에 스폰 자리를 고르는 "활동 영역" 과 플레이어 넷의 긴장도를 합치는 일은 없다(자리는 게임이 정한다, 긴장도 모델 하나).
- RDR2 무작위 조우: 가중 풀 · 쿨다운 · 시각/날씨/장소/플레이어 상태 조건 · 한 번만 나오는 조우(`maxCount="1"`) — 데이터로 된다. 조우 스크립트 · 배치는 게임 몫.
- 로그라이크 방 감독: 예산 풀 · 보상 밀도 · 순환에 따른 해금(`minCycle`). 방 생성 그래프는 없다.
- 없는 것: 저장/불러오기(판 단위 상태라 지금은 `restart`), 에디터 패널(`explain` · `dumpTrace` 글이 그대로 쓰일 자리), 여러 플레이어의 긴장도 합치기.
