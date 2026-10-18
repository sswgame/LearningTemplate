# 오디오 · 현지화 · AI 후보

> 상태: 후보 · 선행: 없음 · 영역: audio-ai-localization · 옛번호: D5-1, D5-2, D5-3 · 진행: 해당없음

이 문서는 착수 전 후보의 목록이다. 착수하면 절을 계획 문서로 옮기고 여기서 지운다. 오디오 엔진, 현지화, NPC AI, 게임 로직 자동화가 범위다.

## 기능

### AudioEngineExtensions
제목: 오디오 엔진 확장
옛번호: D5-1
왜 필요한가: 오디오 엔진(`Source/Engine/Audio`)은 출력이 스테레오 · 리눅스 Null 이고 공간 음향 · 절차 소리 그래프가 없다.
남은 것:
- 에디터 비용 측정: 이벤트 창(`AudioEventBrowserPanel`)의 게시 상태가 블록마다 인스턴스 × 보이스를 훑는 비용.
- 출력: HRTF(바이노럴), 5.1/7.1(지금 출력은 스테레오), 리눅스 출력 백엔드(지금 Null).
- 회절.
- 컨볼루션 리버브, 그래뉼러 · 절차 소리 그래프(MetaSounds 급).

### LocalizationCultureData
제목: 현지화 문화권 데이터와 쿠킹 필터
옛번호: D5-2
왜 필요한가: RTL 문화권 데이터와 일본어 번역이 비어 있고, 쿠킹에 문화권 필터가 없다(`Engine/Localization/LocalizationDocuments.h`).
남은 것:
- 아랍어 이외 RTL 문화권 데이터와 `ja` 번역(`po/ja.po` 의 글이 비어 있다). 끝나면 엔진 프로젝트 `stagedCultures` 에 넣는다.
- 쿠킹 문화권 필터: `stagedCultures` 밖 문화권의 번역 테이블도 팩에 들어간다(언리얼은 스테이지 목록 밖 문화권을 빼고 패키징한다). 목록 밖 테이블이 생기면 쿠킹에서 거른다.
- 아이템 이름(Shooter3D)은 표에 모이지만 화면에 쓰는 코드가 아직 없다(무기 이름은 HUD 가 `getStringByText` 로 쓴다).

### NpcScheduleFollowUp
제목: NPC 하루 일정의 남은 것
옛번호: D5-3
왜 필요한가: HarvestValley 마을 사람이 첫 사용처다(`GameFramework/Base/Actor/AI/Schedule`, `README.md`; `FarmVillageDirectorComponent` 가 마을 내비 격자 위 `NavAgent` 로 일정상 자리를 쫓는다. 시나리오 `harvestvalley/automation/schedule`).
남은 것:
- `ScheduleSaveState` 를 디렉터의 상태 바이트에 싣기: 끼어들기(말 걸기)와 호감도를 거는 곳이 생기면 한다. 지금은 쓰는 코드가 없어 복원할 상태가 모두 시계에서 나오므로 실을 바이트가 없다(먼저 실으면 쓰지 않는 형식을 굳힌다).
- 에디터 패널(`ScheduleDebugPanel`)을 실물로 열어 확인(에디터 시나리오 쪽 일).
함정: 내비 장애물은 이름 목록의 오브젝트 자리 + 반지름이다(메시 경계는 비동기 로드라 결정적이지 않아 쓰지 않았다). 시계를 감지 않고 오래 걸으면 나무 옆에서 미끄러지며 일정상 자리를 3 m 넘게 놓칠 수 있다(놓치면 그 자리에 놓인다).
