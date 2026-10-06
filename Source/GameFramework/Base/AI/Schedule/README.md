# AI/Schedule — NPC 하루 일정

마을 사람 · 상인 · 손님 · 동물이 시각에 따라 어디서 무엇을 하는지 정하는 장르 공통 기반입니다(스타듀 밸리 일정 · 축제, 스카이림 Radiant AI 패키지,
동물의 숲 주민의 하루, 페르소나 요일 일정, RDR2 마을 사람). **AI 폴더에 있는 이유**: "지금 무엇을 할까" 를 고르는 결정 계층이라 행동 트리 · 블랙보드 ·
스폰 감독과 같은 층이고, 시계 · 날씨 · 플래그 · 방 그래프(`World/`)와 길 찾기(`Navigation/`)를 읽기만 합니다. 키트가 아니라 기반인 것은 농장 · 생활 ·
오픈월드 · 경영 키트가 함께 쓰기 때문입니다(키트끼리는 링크하지 않는다).

| 파일 | 하는 일 |
|------|---------|
| `ScheduleCondition` | 조건 — 요일 · 계절의 날 · 계절 · 날씨 · 하루의 때 · `GameFlags` 식 · 태그(관계 · 퀘스트) · 확률. 절마다 결과를 남긴다 |
| `ScheduleActivity` | 활동 등록부 — 데이터가 이름으로 고르는 활동 종류(`GoTo` · `WorkAt` · `Attend` · `StayHome` · `Sleep` · `UseObject` · `Meet` · `Wander`) |
| `ScheduleCatalog` | `*.schedules.xml` — 달력 어휘 · 장소 · 활동 자리 · 끼어들기 · 약속 · 묶음(아키타입) · NPC 루틴 · 축제/행사, 읽을 때 모든 이름을 검사 |
| `SchedulePathing` | "어디" 인터페이스 — 자리(`float3` + 지역), 이동 시간 추정 · 경로. 직선 · `NavGrid`(XZ · XY) · `AreaGraph`(지역 단위) 구현 |
| `ScheduleLocator` | 활동 자리 제공자 — `UseObject` 가 벤치 · 작업대를 시간 창으로 예약(멱등). 기본은 데이터 `<Spot>` |
| `ScheduleSystem` | 런타임 — 칸 고르기 · 일찍 나서기 · 끼어들기 스택 · 약속 · 화면 밖 LOD · 잠 · 저장 · 네트워크 요약 · 추적 |
| `ScheduleSaveState` | 게임 `SaveGame` 에 `PROPERTY()` 로 넣는 저장 상태 |

## 데이터

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

- 시각은 `H:MM`(0:00..24:00), 하루를 넘는 칸은 없다(밤잠은 22:00–24:00 과 0:00–7:00 두 칸). 칸 없는 시간은 NPC 의 `idle`(기본 `StayHome`).
- **고르기**: 하루를 칸 경계로 자른 조각마다 조건이 맞는 후보(루틴 칸 · 행사 칸 · 약속 칸) 중 우선순위가 가장 높은 것, 같으면 먼저 적은 것이 이긴다.
  축제는 우선순위가 높은 행사라 그 시간만 덮는다. 조건은 루틴 · 칸 모두에 적을 수 있고 `phases` 는 칸 시작 시각의 때로 판정한다.
- **약속**: `Meet` 칸이 같은 약속을 가리키는 NPC 들이 참가자다(읽을 때 모은다 — 둘 미만이면 경고). 시간 · 장소는 약속의 것이고 우선순위는
  루틴과 약속 중 높은 것. 시작 + `wait` 에 모두 와 있지 않으면 그날 약속은 깨지고 참가자는 그 아래 칸으로 간다.
- 검사: 모르는 원소 · 속성 · 활동 · 장소 · 약속 · 묶음 · 요일 · 계절 · 날씨, 틀린 시각 · 조건식, 시작 ≥ 끝, 조건 없는 칸끼리 겹침, 활동에 필요한
  `place` · `objectKind` 빠짐, 묶음 순환 → 경고하고 그 항목을 뺀다. `ResourceDataSchemaTest` 가 `Resource/` 아래 모든 `*.schedules.xml` 을 읽는다.

## 런타임

**계획은 (그날, 계획을 세운 시각, 그때의 자리)의 함수다.** 하루 시작 · 조건(날씨 · 플래그 리비전 · 태그) 바뀜 · 끼어들기 끝 · 약속 깨짐에서만 다시
세우고, 어느 시각의 상태는 계획에서 바로 계산한다 — 그래서

- **일찍 나서기**: 칸마다 앞 자리에서의 이동 분(계획 경로 추정, 올림)만큼 앞서 나선다. 앞 칸의 끝을 빌리므로, 앞 칸이 더 높은 우선순위(약속 · 축제)면
  끝까지 있다가 늦게 닿는다. 돌아다니기의 둘째 자리부터는 그 시각에 나선다.
- **끼어들기**: 우선순위 순 스택(같으면 나중 것이 위), `timeout` 이 지나면 저절로 빠진다. 끼어든 동안 일정 시계는 흐르고, 모두 빠지면 끼어든 자리
  (`reportInterruptedLocation` 으로 게임이 알린 자리)에서 **지금 시각의 칸**으로 다시 세운다 — 끊긴 칸이 아니다.
- **화면 밖 LOD**: `Far` NPC 는 프레임마다 아무 일도 하지 않는다. 상태 사건(나섬 · 닿음 · 활동 시작)만 `_farStepMinutes` 간격으로 미뤄 내고,
  자리는 계획 경로(지역 단위)다. `Near` 로 바꾸면 그 시각의 고운 경로 위 자리로 서고 `Snapped` 를 낸다. 판정(계획의 출발점 · 끼어든 자리 · 약속 출석)은
  늘 계획 경로로 재므로 LOD 를 바꿔도 계획이 갈리지 않는다 — 몇 시간을 한 번에 넘긴 화면 밖과 매 분 돌린 화면 안이 같은 상태 해시다.
- **잠**: `skipTo` 는 끼어들기를 지우고 건너뛴다. 사이 사건은 버리고 NPC 마다 `Snapped`(+ 활동 중이면 `ActivityStarted`).
- **결정성**: 시각은 정수 분, 확률 · 돌아다니기 자리는 (씨앗, NPC, 날, 시각) 해시, 같은 분의 사건은 날 시작 → 끼어들기 만료 → 약속 판정 → NPC 순.
  `computeStateHash` 로 확인한다. 행사 · 약속의 확률 열쇠에는 NPC 를 섞지 않는다(모두에게 열리거나 아무에게도).
- **저장**: `fillSaveState` / `restoreSaveState` — 시각 · 씨앗 · 날씨, NPC 마다 계획의 출발 시각 · 자리 · 끼어들기 · 끼어든 자리 · 태그, 깨진 약속,
  기본 자리 제공자의 예약. 계획 자체는 저장하지 않고 다시 세운다(데이터를 고친 뒤 불러와도 새 일정으로 이어진다).
- **네트워크**: `fillNetSummary` + `encodeNetSummary`(머리 6 바이트 + NPC 마다 26 바이트) — 자리 · 지역/활동 해시 · 칸 · 상태 · 끼어들기 깊이.

## 추적

- `explainNpc` — 지금 칸과 그 출처(루틴/행사/약속 · 칸 번호 · 우선순위 · 나선 시각 · 이동 분), 지금 시각을 덮는 후보마다 `+`/`-` 와 막은 절
  (`[weathers failed]`), 깨진 약속, 끼어들기 스택.
- `dumpTimeline` — 오늘 계획 한 줄씩(`09:00-17:00 WorkAt @ 'town' (...) leave 08:30 (+30 min) [routine 'weekday' p0]`). 에디터 패널은 이 글을 그대로 보인다.
- `-gv_scheduleTrace=<npc id | *>` — 값이 바뀌는 `update` 에서 둘을 로그에 한 번 남긴다(시험용 전역 변수 — 배포본에는 없다).

## 밖과 잇는 곳 (병합 뒤 할 일)

- **스마트 오브젝트**: `IScheduleActivityLocator` 를 스마트 오브젝트 시스템이 구현해 `setActivityLocator` 로 끼운다. `reserve` 는 (NPC · 종류 · 날 ·
  시간 창)에 멱등이어야 하고, 지금 점유만 되는 시스템이면 어댑터가 시간 창 예약표를 들고 칸이 시작될 때 실제 점유한다. 없으면 데이터 `<Spot>`.
- **애니메이션**: `IScheduleActivityAnimator::onActivityStarted( cue )` 의 `_animation`(칸의 `animation`, 없으면 활동 종류의 기본)을 애니메이터 그래프
  상태 · 클립으로 고르는 구현을 끼운다. Near NPC 에게만 불리고, 화면 밖은 사건(`drainEvents`)만 남는다.
- **길 찾기**: 계획 경로는 `AreaGraphSchedulePathing`(지역) 또는 `NavGridSchedulePathing`, 화면 안 고운 경로는 내비 격자. 실제 몸은 게임의 `NavAgent` 가
  `ScheduleNpcView::_location` · `_target` 을 따라 움직인다. 내비메시가 생기면 `ISchedulePathing` 구현 하나를 더한다.

## 함정 · 계약

- **일정의 "일찍 나서기" 는 앞 칸의 끝을 빌린다** — 앞 칸의 우선순위가 더 높으면(약속 · 축제) 빌리지 않는다. 빌리면 점심 약속 중에 일하러 나서 약속 출석
  판정이 깨진다. 화면 밖 일정이 화면 안과 같으려면 판정 자리(계획 출발점 · 끼어든 자리)를 LOD 와 상관없는 계획 경로로 잰다(`AI/Schedule/README.md`).
