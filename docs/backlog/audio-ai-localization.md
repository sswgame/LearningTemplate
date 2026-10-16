# 백로그: 오디오, 현지화, AI

오디오 엔진, 현지화, NPC AI, 게임 로직 자동화의 남은 일입니다. 상태 태그의 뜻, 일을 고르는 순서, 새 항목을 쓰는 법은 [백로그 안내](README.md)에 있습니다.

- `D5-1` [기능] **오디오 엔진(`Engine/Audio/README.md`)의 남은 것.** (2) 에디터 — 이벤트 창(`AudioEventBrowserPanel`, `audioevents`: 이벤트 속성 · 재생 중 인스턴스 · 보이스 프로파일)은 들어갔다.
  남은 것: 이벤트 데이터를 창에서 고쳐 저장하기(엔진에 `AudioEventLibrary` 를 쓰는 길이 없어 지금은 "데이터 파일 열기" 뿐), `Source/Engine/Audio` · Editor README 에 새 창과 읽기 API(`findEventDesc` ·
  `getInstanceStatuses`)를 적기, 게시 상태가 블록마다 인스턴스 × 보이스를 훑는 비용 측정.
  (4) 출력: HRTF(바이노럴) · 5.1/7.1(지금 출력은 스테레오), 리눅스 출력 백엔드(지금 Null — 후보는 `G-11`). (5) 회절. (6) 컨볼루션 리버브, 그래뉼러 · 절차 소리
  그래프(MetaSounds 급).

- `D5-2` [기능] **로컬라이제이션 — 남은 것**(`Engine/Localization/LocalizationDocuments.h`). 아랍어 이외 RTL 문화권 데이터 · `ja` 번역(`po/ja.po` 의 글이 비어 있다 — 끝나면 엔진 프로젝트 `stagedCultures` 에 넣는다).
  쿠킹에는 문화권 필터가 없어 `stagedCultures` 밖 문화권의 번역 테이블도 팩에 들어간다(언리얼은 스테이지 목록 밖 문화권을 빼고 패키징한다) — 목록 밖 테이블이 생기면 쿠킹에서 거른다.
  아이템 이름(Shooter3D)은 표에 모이지만 화면에 쓰는 코드가 아직 없다(무기 이름은 HUD 가 `getStringByText` 로 쓴다).

- `D5-3` [기능] **NPC 하루 일정(`GameFramework/Base/Actor/AI/Schedule`)의 남은 것.** HarvestValley 마을 사람이 첫 사용처다(`FarmVillageDirectorComponent` — 판 상태 시계로 일정을 돌리고
  `Villager.<id>` 몸이 일정상 자리를 곧장 따라간다, 시나리오 `harvestvalley/automation/schedule`). 남은 것: (1) 몸이 `NavAgent` 로 길을 따라가지 않는다(지금은 일정상 자리로 순간 이동 — 일정의 경로가 곧은 선이라 보이는 차이는 작다).
  (2) 끼어들기(말 걸기)와 호감도를 쓰게 되면 `ScheduleSaveState` 를 디렉터의 상태 바이트에 싣는다(지금은 머리만 — 일정이 시계의 함수라 복원하면 같은 자리에 선다).
  (3) 에디터 패널(`ScheduleDebugPanel`)을 실물로 열어 보지 않았다. (4) `AI/Schedule/README.md` 는 없다. 목록 함수는 시스템을 같은 스레드에서 읽는다고 가정한다.

- `D5-4` [기능] **게임 로직 자동화(2026-10-15, `Engine/Automation/README.md`)의 남은 것.**
  (2) 새 단계를 쓰는 시나리오가 없는 게임: MeadowVillage · AbilityArena(적 AI `ArenaEnemyAIComponent` 는 상태 이름을 아직 안 낸다) · VoxelCraft · ThemePark.
  (3) 적 AI 처럼 같은 이름의 오브젝트가 여럿이면(`AIController`) `target` 으로 하나를 고를 수 없다 — 이름 뒤 번호 규칙이나 "그 이름의 모두/아무나" 질의가 필요하다.
  (4) GameFramework 의 `ensureLinked` 앵커(`ControlAutomationSteps`)는 Shipping 이 GameFramework 를 통째로 링크하므로(`sw_linkWholeArchive`) 필요 없다 — 지울 때 Engine 쪽 앵커도 같이 본다(Engine 은 테스트 실행 파일도 통째 링크).

---

[◀ 백로그 안내](README.md)
