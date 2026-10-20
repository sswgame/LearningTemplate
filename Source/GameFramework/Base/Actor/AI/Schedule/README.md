# Schedule — NPC 하루 일정

## 이것은 무엇이고 왜 있나

마을 사람이 "9 시에 가게를 열고, 17 시에 광장 벤치에 앉고, 금요일 19 시에 술집에서 만난다" 를 데이터(`*.schedules.xml`)로 적고 돌리는 런타임입니다.
스타듀 밸리의 NPC 일정, 레드 데드 리뎀션 2 의 하루 일과, 언리얼 Mass 의 스마트 오브젝트 예약에 해당합니다.
게임 코드는 시계를 흘리고(`update( clock )`) NPC 의 지금 모습(`getNpcView`)을 읽기만 합니다. 어느 칸인지, 언제 나서는지, 축제가 덮는지, 약속이 깨졌는지는 이 폴더가 정합니다.

핵심 약속은 **계획이 (그날, 계획을 세운 시각, 그때의 자리)의 함수**라는 것입니다. 하루가 시작될 때, 조건(날씨 · 플래그 · 태그 · 호감도)이 바뀔 때,
끼어들기가 끝날 때, 약속이 깨질 때만 계획을 다시 세웁니다. 어느 시각의 상태는 그 계획에서 바로 계산하므로 화면 밖 NPC 는 매 프레임 아무 일도 하지 않고,
몇 시간을 한 번에 건너뛰어도 매 분 돌린 것과 같은 자리에 섭니다. 시각은 정수 분, 무작위는 씨앗 해시라 결정적입니다.

## 머릿속 그림

```
schedules.xml ──► ScheduleCatalog(장소 · 스팟 · 루틴 · 행사 · 약속 · 끼어들기 · NPC)
                         │
WorldClock ──update──► ScheduleSystem ──► getNpcView(npc) : 자리 · 활동 · 단계(Traveling/Performing/Interrupted)
                         │  ├ 계획 경로(ISchedulePathing, 굵은 것) : 출발 시각 · 판정 자리 · 지역
                         │  ├ 고운 경로(화면 안 Near 만) : 보이는 자리
                         │  ├ 자리 제공자(IScheduleActivityLocator) : 벤치 · 낚시 자리 예약
                         │  └ drainEvents : Departed · Arrived · ActivityStarted · Snapped …
                         └─► fillSaveState / restoreSaveState · fillNetSummary · explainNpc · dumpTimeline
```

## 따라 해 보기

HarvestValley 의 `FarmVillageDirectorComponent` 가 첫 사용처입니다. 데이터는 `Resource/game/harvestvalley/data/villagers.schedules.xml` 입니다.

1. 카탈로그를 읽습니다. `ScheduleCatalog::loadFromResource( path )` 가 실패하거나 NPC 가 없으면 디렉터는 시작하지 않습니다.
2. 경로를 정합니다. `setPathing( 계획, 고운 )` — HarvestValley 는 계획을 곧은 선(기본), 화면 안 걸음을 마을 내비 격자(`NavGridSchedulePathing`)로 둡니다.
3. `initialize( &catalog, settings, 0 일 0:00 부터 센 분 )` 으로 NPC 를 집에 세우고 그 시각의 계획을 세웁니다. 설정의 `_clock` 은 게임 `WorldClock` 과 같은 값이어야 계절 · 하루의 때 조건이 맞습니다.
4. 매 틱 `setWeather` · `setNpcLOD` 를 맞추고 `update( clock )` 한 뒤 `getNpcView( npc )._location` 을 몸에 줍니다. HarvestValley 의 몸은 `NavAgent` 로 그 자리를 쫓습니다.
5. 시나리오로 확인합니다. `Schedule.Activity target="pierre"` 처럼 NPC id 로 묻는 탐침이 `GameplayAutomationSteps.cpp` 에 있습니다(`game/harvestvalley/automation/schedule.scenario.xml`).

## 작동 원리

**칸 고르기.** 루틴은 조건(`days` · `seasons` · `weathers` · `flags` · `tags` · 호감도)이 맞는 날 쓰고, 같은 시각에 칸이 여럿이면 `priority` 가 높은 것이 이깁니다.
행사(`<Event>`)는 그날 그 시간만 덮고, 약속(`<Appointment>`)은 `Meet` 칸이 가리키는 NPC 들이 모이는 시간 · 장소입니다. 어느 칸도 없는 시간은 NPC 의 `idle` 활동(기본 `StayHome`)입니다.

**일찍 나서기.** 칸의 자리까지 걸리는 시간을 계획 경로로 재서 그만큼 앞서 나섭니다(`ScheduleSegment::_departMinute`). 화면 안 NPC 의 걷는 자리는 같은 출발 · 도착 시각 사이를
고운 경로 위에서 비율로 나눕니다. 그래서 화면 밖 NPC 와 같은 시각에 도착하고, LOD 를 바꿔도 계획이 갈리지 않습니다.

**끼어들기.** `pushInterruption( npc, "Talk" )` 으로 쌓고 `popInterruption` 으로 뺍니다. 일정 시계는 그동안에도 흐르고, 모두 빠지면 지금 시각의 칸으로 돌아갑니다.
만료(`timeout`)가 있는 끼어들기는 시간이 지나면 저절로 빠집니다.

**저장.** `fillSaveState` 는 시각 · 끼어들기 스택 · 끼어든 자리 · 호감도 · 약속 결과를 담습니다(`ScheduleSaveState.h`). 끼어들기와 호감도를 쓰지 않는 게임은
일정이 시계의 함수이므로 저장할 것이 없습니다 — HarvestValley 디렉터는 상태 바이트에 머리만 싣고, 끼어들기(말 걸기)나 호감도를 쓰게 되면 그때 싣습니다.

## 확장하는 법

- 새 활동 이름은 데이터에만 적습니다(`activity="Fish"`). 이름의 뜻(애니메이션 · 스팟 종류)은 `animation` · `objectKind` 속성과 게임의 훅이 정합니다.
- 길 찾기를 바꾸려면 `ISchedulePathing` 을 구현합니다. 같은 입력에는 늘 같은 값을 돌려줘야 합니다(출발 시각이 그 추정으로 정해진다). 이미 있는 것은 곧은 선, 내비 격자, 방 그래프입니다.
- 벤치 · 낚시 자리처럼 한 명만 쓰는 자리는 `<Spot>` 으로 적으면 기본 자리 제공자가 예약합니다. 게임 오브젝트에서 자리를 찾으려면 `IScheduleActivityLocator` 를 끼웁니다.
- 활동 애니메이션은 `IScheduleActivityAnimator` 훅으로 받습니다. 화면 안 NPC 만 불립니다(`ScheduleSkeletalAnimator` 가 스켈레탈 애니메이터 구현).

## 함정과 주의

- **목록 함수는 같은 스레드에서 읽는다고 가정합니다.** `collectLiveSystems` 는 생성자 · 소멸자가 올리고 내리는 전역 목록을 잠그지 않고 돌려줍니다. 에디터 디버그 창과 자동화 탐침은 게임 스레드에서만 부릅니다.
- **시각은 정수 분입니다.** 시계의 분 아래는 `_minuteFraction` 으로 쌓였다가 넘어갑니다. 시나리오가 시계를 감으면(`AdvanceGameTime`) 다음 `update` 에서 그 사이의 사건이 시각 순으로 처리됩니다.
- **잠은 `skipTo` 입니다.** 끼어들기를 모두 지우고 사이의 사건을 버린 뒤 NPC 마다 `Snapped` 를 냅니다. 몸을 그 자리에 놓는 것은 게임의 몫입니다(HarvestValley 는 몸이 `_bodySnapDistance` 넘게 떨어지면 놓는다).
- 에디터 디버그 창(`ScheduleDebugPanel`)은 실물로 열어 보지 않았습니다. 에디터 쪽 일이라 후보 절(`Candidates-editor.md`)에 남아 있습니다.

## 더 볼 곳

| 파일 | 내용 |
|------|------|
| `ScheduleCatalog.{h,cpp}` | XML 읽기, 장소 · 스팟 · 루틴 · 행사 · 약속 · NPC 정의 |
| `ScheduleCondition.{h,cpp}` | 루틴 · 칸의 조건 식 |
| `ScheduleSystem.h`, `ScheduleSystemPlan.cpp` | 계획 세우기(칸 고르기, 일찍 나서기, 약속) |
| `ScheduleSystemRuntime.cpp` | 시간 흐르기, 사건, 끼어들기 |
| `ScheduleSystemSave.cpp`, `ScheduleSaveState.h` | 저장 · 복원 · 네트워크 요약 |
| `ScheduleHash.h` | 칸 정체 키 · 이름 키 · 키 섞기(계획 · 실행 · 저장 · 디버그가 같이 쓴다) |
| `ScheduleSystemDebug.cpp` | `explainNpc` · `dumpTimeline` · `gv_scheduleTrace` |
| `SchedulePathing.{h,cpp}` | 곧은 선 · 내비 격자 · 방 그래프 경로 |
| `ScheduleLocator.{h,cpp}` | 스팟 예약 |
| `Games/HarvestValley/FarmVillageDirectorComponent.cpp` | 첫 사용처(시계 연결, 내비 격자, 몸 스폰) |
