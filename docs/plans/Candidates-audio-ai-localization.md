# 오디오 · 현지화 · AI 후보

> 상태: 후보 · 선행: 없음 · 영역: audio-ai-localization · 옛번호: D5-1, D5-2, D5-3 · 진행: 해당없음

이 문서는 착수 전 후보의 목록이다. 착수하면 절을 계획 문서로 옮기고 여기서 지운다. 오디오 엔진, 현지화, NPC AI, 게임 로직 자동화가 범위다.

## 기능

### LocalizationCultureData
제목: 현지화 문화권 데이터와 쿠킹 필터
옛번호: D5-2
왜 필요한가: 구현은 끝났다(2026-10-22 — RTL 문화권 `he` · `fa`, 엔진 `ja` 번역과 출시 문화권, 쿠킹 문화권 필터 `CookAssets.collectUnstagedTranslationTablesInternal`, Shooter3D HUD 의 든 아이템 이름 `_itemName`). 빌드 · 눈 확인만 남았다.
남은 것:
- 사람 눈 확인: 일본어 화면의 잘림, `he` · `fa` 의 RTL 배치와 `۰-۹` 숫자, Shooter3D HUD 의 아이템 이름 줄 자리(무기 이름 위).
- `App --check-text` 로 출시 문화권 `ja` 에 빠지거나 낡은 번역이 없는지.
못 하는 이유(2026-10-24): 눈 확인은 사람이 화면을 봐야 한다. `--check-text` 는 빌드된 App 이 있어야 돌려서 통합에서 실행 대기다(매니페스트 id `localization-check-text`). 풀리는 조건: 사람이 일본어 · RTL 화면을 보고, 통합 담당이 `--check-text` 를 돌린다.

### NpcScheduleFollowUp
제목: NPC 하루 일정의 남은 것
옛번호: D5-3
왜 필요한가: HarvestValley 마을 사람이 첫 사용처다(`GameFramework/Base/Actor/AI/Schedule`, `README.md`; `FarmVillageDirectorComponent` 가 마을 내비 격자 위 `NavAgent` 로 일정상 자리를 쫓는다. 시나리오 `harvestvalley/automation/schedule`).
남은 것:
- `ScheduleSaveState` 의 세이브 · 불러오기 시나리오: 디렉터의 상태 바이트(2 판)에는 싣는다(2026-10-22, `FarmVillageDirectorComponent::writeState`). 끼어들기 · 호감도가 이어지는지는 세이브 · 불러오기를 거는 코드가 생긴 뒤 시나리오로 확인한다.
- 에디터 패널(`ScheduleDebugPanel`)을 실물로 열어 확인(에디터 시나리오 쪽 일).
못 하는 이유(2026-10-24): 에디터 패널은 사람이 창을 열어 봐야 하고, 세이브 시나리오는 세이브를 거는 코드가 먼저 있어야 한다. 풀리는 조건: 게임에 세이브 호출이 생길 때, 사람이 에디터에서 패널을 열 때.
함정: 내비 장애물은 이름 목록의 오브젝트 자리 + 반지름이다(메시 경계는 비동기 로드라 결정적이지 않아 쓰지 않았다). 시계를 감지 않고 오래 걸으면 나무 옆에서 미끄러지며 일정상 자리를 3 m 넘게 놓칠 수 있다(놓치면 그 자리에 놓인다).
