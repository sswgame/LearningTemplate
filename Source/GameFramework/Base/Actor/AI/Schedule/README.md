# AI/Schedule — NPC 하루 일정

## 이것은 무엇이고 왜 있나

마을 사람, 상인, 손님, 동물은 시각에 따라 다른 곳에서 다른 일을 합니다. 아침에는 가게를 열고, 비가 오면 집에 있고, 축제 날에는 광장에 모입니다.
이 폴더는 그 하루 일정을 데이터로 적고 돌립니다. 스타듀 밸리의 일정과 축제, 스카이림의 Radiant AI 패키지, 동물의 숲 주민의 하루, 페르소나의 요일 일정, RDR2 의 마을 사람과 같은 기능입니다.

"지금 무엇을 할까"를 고르는 결정 계층이라서 행동 트리, 블랙보드, 스폰 감독과 같은 `AI/` 폴더에 있습니다. 시계와 날씨, 플래그, 지역 그래프(`World/`)와 길찾기(`Navigation/`)는 읽기만 합니다.
농장, 생활, 오픈월드, 경영 키트가 함께 쓰므로 키트가 아니라 기반에 있습니다.

## 머릿속 그림

**블록.** 하루의 한 구간입니다. 시작과 끝 시각, 활동 종류, 장소를 가집니다. 루틴과 행사와 약속이 모두 블록으로 이루어집니다.

**루틴과 행사.** 루틴은 NPC 하나의 평소 일정이고, 행사(축제)는 여러 NPC 를 덮는 높은 우선순위의 일정입니다. 같은 시간에 후보가 여럿이면 우선순위가 높은 것이 이깁니다.

**계획.** 시스템은 NPC 마다 그날의 계획을 세우고, 어느 시각의 상태는 계획에서 바로 계산합니다. 계획은 (그날, 계획을 세운 시각, 그때의 위치)의 함수입니다.

**활동.** `GoTo`, `WorkAt`, `Attend`, `StayHome`, `Sleep`, `UseObject`, `Meet`, `Wander` 입니다. 데이터가 이름으로 고르고, 활동 레지스트리(`ScheduleActivity`)에 종류를 더할 수 있습니다.

## 따라 해 보기 — 상인 한 명의 하루

아직 이 시스템을 쓰는 게임은 없습니다. 테스트(`Test/EngineTest/GameFramework/AI/TestSchedule.cpp`)가 아래 데이터와 같은 모양으로 돌립니다.

### 1단계 — 일정 데이터

```xml
<Schedules>
  <Calendar days="Mon,Tue,Wed,Thu,Fri,Sat,Sun" seasons="Spring,Summer,Fall,Winter" weathers="sunny,rain"/>
  <Place id="store" area="town" position="24 0 6" radius="1"/>
  <Spot id="bench_west" kind="Bench" area="town" position="18 0 14"/>
  <Interrupt id="Talk" priority="10" timeout="20"/>
  <Appointment id="friday_drinks" place="saloon" start="19:00" end="21:00" wait="30" priority="40" days="Fri"/>
  <Archetype id="villager"><Routine id="sleep"><Block start="0:00" end="7:00" activity="Sleep"/></Routine></Archetype>
  <Npc id="pierre" archetype="villager" home="pierre_house" speed="12" tags="Job.Shopkeeper">
    <Routine id="weekday" days="Mon,Tue,Thu,Fri,Sat">
      <Block start="9:00" end="17:00" activity="WorkAt" place="store" animation="Sweep"/>
      <Block start="17:00" end="19:00" activity="UseObject" objectKind="Bench" place="town_square"/>
      <Block activity="Meet" appointment="friday_drinks"/>
    </Routine>
    <Routine id="rain_indoors" priority="10" weathers="rain"><Block start="17:00" end="22:00" activity="StayHome"/></Routine>
  </Npc>
  <Event id="egg_festival" priority="100" seasons="Spring" daysOfSeason="13" archetypes="villager">
    <Block start="9:00" end="14:00" activity="Attend" place="town_square"/>
  </Event>
</Schedules>
```

`pierre` 는 평일 9시부터 17시까지 가게에서 일하고, 그 뒤 벤치에 앉습니다. 비가 오는 날 17시 이후에는 우선순위 10 인 `rain_indoors` 가 이겨 집에 있습니다.
금요일 저녁에는 약속에 가고, 봄 13일에는 축제가 9시부터 14시까지를 덮습니다.

### 2단계 — 시스템 돌리기

<!-- snippet: ScheduleSystem.h 의 @code 블록 — 5b U7 에서 대조 -->
```cpp
ScheduleSystem schedules;
schedules.initialize( &catalog, settings, clock.getDay() * kScheduleMinutesPerDay + clockMinute );
schedules.setPathing( &areaPathing, &navPathing );           // 굵은 계획 경로, 화면 안 고운 경로
schedules.setFlags( &flags );
// 매 프레임
schedules.setWeather( weather.getCurrent() );
schedules.setNpcLod( npcIndex, bVisible ? ScheduleLod::Near : ScheduleLod::Far );
schedules.update( clock );
const ScheduleNpcView view = schedules.getNpcView( npcIndex ); // 위치, 활동, 애니메이션
```

게임의 몸 컴포넌트는 `ScheduleNpcView` 의 `_location` 과 `_target` 을 따라 움직입니다.

### 3단계 — 왜 거기 있는지 묻기

`explainNpc` 는 지금 블록과 그 출처(루틴, 행사, 약속, 우선순위, 나선 시각, 이동 분), 지금 시각을 덮는 후보마다 이겼는지와 막은 조건(`[weathers failed]`)을 씁니다.
`dumpTimeline` 은 오늘 계획을 한 줄씩 씁니다(`09:00-17:00 WorkAt @ 'town' (...) leave 08:30 (+30 min) [routine 'weekday' p0]`).
`-gv_scheduleTrace=<npc id 또는 *>` 를 주면 값이 바뀌는 `update` 에서 둘을 로그에 남깁니다(테스트 빌드 전용 전역 변수).

## 작동 원리

### 데이터 규칙

시각은 `H:MM`(0:00 부터 24:00)입니다. 하루를 넘는 블록은 없으므로, 밤잠은 22:00–24:00 과 0:00–7:00 두 블록으로 적습니다. 블록이 없는 시간은 NPC 의 `idle`(기본 `StayHome`)입니다.

**고르기.** 하루를 블록 경계로 자른 조각마다, 조건이 맞는 후보(루틴, 행사, 약속 블록) 중 우선순위가 가장 높은 것이 이깁니다. 같으면 먼저 적은 것입니다.
축제는 우선순위가 높은 행사라서 그 시간만 덮습니다. 조건은 루틴에도 블록에도 적을 수 있고, `phases`(하루의 때)는 블록 시작 시각으로 판정합니다.

**조건**(`ScheduleCondition`)은 요일, 계절의 날, 계절, 날씨, 하루의 때, `GameFlags` 식, 태그(관계, 퀘스트), 확률입니다. 판정할 때 절마다 결과를 남기므로 추적에서 볼 수 있습니다.
페이싱 감독의 조우 조건도 같은 클래스를 씁니다.

**약속.** `Meet` 블록이 같은 약속을 가리키는 NPC 들이 참가자입니다. 읽을 때 모으고, 둘 미만이면 경고합니다. 시간과 장소는 약속의 것이고, 우선순위는 루틴과 약속 중 높은 것입니다.
시작 시각에 `wait` 분을 더한 때까지 모두 와 있지 않으면 그날 약속은 깨지고, 참가자는 그 아래 블록으로 갑니다.

### 계획을 다시 세우는 때

계획은 하루가 시작될 때, 조건(날씨, 플래그 리비전, 태그)이 바뀔 때, 끼어들기가 끝날 때, 약속이 깨질 때만 다시 세웁니다. 그 밖의 시각은 계획에서 계산하므로 다음이 성립합니다.

- **일찍 나서기.** 블록마다 앞 위치에서의 이동 시간(분, 올림)만큼 앞서 나섭니다. 이 시간은 앞 블록의 끝을 빌리는 것입니다.
  돌아다니기(`Wander`)는 둘째 위치부터 그 시각에 나섭니다.
- **끼어들기.** 대화 같은 끼어들기는 우선순위 순서의 스택이고, 같으면 나중 것이 위입니다. `timeout` 이 지나면 저절로 빠집니다.
  끼어든 동안에도 일정 시계는 흐르고, 모두 빠지면 끼어든 위치(`reportInterruptedLocation` 으로 게임이 알린 곳)에서 **지금 시각의 블록**으로 다시 세웁니다. 끊긴 블록으로 돌아가지 않습니다.
- **화면 밖 LOD.** `Far` NPC 는 프레임마다 아무 일도 하지 않습니다. 나섬, 닿음, 활동 시작 같은 사건만 `_farStepMinutes` 간격으로 내고, 위치는 지역 단위의 계획 경로입니다.
  `Near` 로 바꾸면 그 시각의 고운 경로 위로 옮기고 `Snapped` 사건을 냅니다.
- **잠.** `skipTo` 는 끼어들기를 지우고 시간을 건너뜁니다. 사이 사건은 버리고, NPC 마다 `Snapped` 를 냅니다(활동 중이면 `ActivityStarted` 도).

**결정성.** 시각은 정수 분입니다. 확률과 돌아다니기 위치는 (씨앗, NPC, 날, 시각)의 해시로 정합니다. 같은 분의 사건은 날 시작, 끼어들기 만료, 약속 판정, NPC 순서로 냅니다.
`computeStateHash` 로 확인합니다. 행사와 약속의 확률에는 NPC 를 섞지 않으므로, 모두에게 열리거나 아무에게도 열리지 않습니다.

**저장.** `fillSaveState` 와 `restoreSaveState` 가 `ScheduleSaveState` 를 채우고 되돌립니다. 게임은 이것을 자기 상태 데이터에 PROPERTY 로 넣습니다.
시각과 씨앗과 날씨, NPC 마다 계획의 출발 시각과 위치, 끼어들기, 태그, 깨진 약속, 자리 예약이 들어갑니다. 계획 자체는 저장하지 않고 다시 세웁니다.
그래서 데이터를 고친 뒤 불러와도 새 일정으로 이어집니다.

**네트워크.** `fillNetSummary` 와 `encodeNetSummary` 가 머리 6바이트와 NPC 마다 26바이트의 요약을 만듭니다. 위치, 지역과 활동 해시, 블록, 상태, 끼어들기 깊이가 들어갑니다.

### 위치와 길

"어디"는 `ISchedulePathing` 인터페이스가 답합니다. 위치(`float3` 와 지역), 이동 시간 추정, 경로를 줍니다. 구현은 직선, `NavGridSchedulePathing`(XZ, XY), `AreaGraphSchedulePathing`(지역 단위) 셋입니다.
`setPathing` 의 첫째가 굵은 계획 경로이고, 둘째가 화면 안의 고운 경로입니다.

`UseObject` 활동은 벤치나 작업대를 시간 창으로 예약합니다(`ScheduleLocator`). 예약은 멱등이라 같은 요청을 다시 해도 결과가 같습니다. 기본 구현은 데이터의 `<Spot>` 입니다.

## 확장하는 법

아직 연결하지 않은 곳이 셋 있습니다. 쓰는 게임이 생기면 이 순서로 붙입니다.

1. **스마트 오브젝트.** 스마트 오브젝트 시스템이 `IScheduleActivityLocator` 를 구현해 `setActivityLocator` 로 끼웁니다. `reserve` 는 (NPC, 종류, 날, 시간 창)에 대해 멱등이어야 합니다.
   지금 점유만 하는 시스템이면 어댑터가 시간 창 예약 테이블을 가지고, 블록이 시작될 때 실제로 점유합니다.
2. **애니메이션.** `IScheduleActivityAnimator::onActivityStarted( cue )` 를 구현해, `_animation`(블록의 `animation`, 없으면 활동 종류의 기본)으로 애니메이터 상태나 클립을 고릅니다.
   `Near` NPC 에게만 불리고, 화면 밖 NPC 는 사건(`drainEvents`)만 남습니다.
3. **길찾기.** 내비메시가 생기면 `ISchedulePathing` 구현을 하나 더합니다.

## 함정과 주의

- **일찍 나서기는 앞 블록의 끝을 빌리지만, 앞 블록의 우선순위가 더 높으면(약속, 축제) 빌리지 않습니다.** 빌리면 점심 약속 중에 일하러 나서서 약속 출석 판정이 깨집니다.
  이때 NPC 는 앞 블록 끝까지 있다가 늦게 도착합니다.
- **판정 위치(계획의 출발점, 끼어든 위치, 약속 출석)는 늘 계획 경로로 잽니다.** LOD 와 상관없는 경로로 재야 두 결과가 같은 상태 해시가 됩니다.
  하나는 화면 밖에서 몇 시간을 한 번에 넘긴 결과이고, 다른 하나는 화면 안에서 매 분 돌린 결과입니다.
- **모르는 이름과 틀린 값은 경고하고 그 항목을 뺍니다.** 모르는 원소나 속성, 모르는 활동, 장소, 약속, 아키타입, 요일, 계절, 날씨가 여기에 해당합니다.
  틀린 시각과 조건식, 시작이 끝보다 늦은 블록, 조건 없는 블록끼리의 겹침도 그렇습니다.
  활동에 필요한 `place` 나 `objectKind` 가 빠진 블록과 아키타입 순환도 뺍니다.
  `ResourceDataSchemaTest` 가 `Resource/` 아래 모든 `*.schedules.xml` 을 읽습니다.

## 더 볼 곳

- [AI/Director](../Director/README.md) — 같은 조건 클래스를 쓰는 페이싱 감독
- [GameFramework](../../../../README.md) — 월드 시계, 플래그, 지역 그래프가 있는 기반 폴더

| 파일 | 내용 |
|------|------|
| `ScheduleCatalog.h` | `*.schedules.xml` 읽기와 검사 |
| `ScheduleCondition.h` | 조건과 판정 기록 |
| `ScheduleSystem.h` | 런타임, LOD, 저장, 추적 |
| `SchedulePathing.h` | 위치와 경로 인터페이스와 구현 |
