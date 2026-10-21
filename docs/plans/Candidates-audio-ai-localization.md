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

### NpcScheduleFollowUp
제목: NPC 하루 일정의 남은 것
옛번호: D5-3
왜 필요한가: HarvestValley 마을 사람이 첫 사용처다(`GameFramework/Base/Actor/AI/Schedule`, `README.md`; `FarmVillageDirectorComponent` 가 마을 내비 격자 위 `NavAgent` 로 일정상 자리를 쫓는다. 시나리오 `harvestvalley/automation/schedule`).
남은 것:
- (끝남, 2026-10-22 · 빌드 미확인) `ScheduleSaveState` 를 디렉터의 상태 바이트(2 판)에 싣는다(`FarmVillageDirectorComponent::writeState`). 옛 1 판은 판 번호로 고르는 이관 함수(`migrateStateVersion1`, 머리 판 범위 읽기 `StateArchiveUtil::readHeaderInRange`)가 읽는다. 세이브 · 불러오기 시나리오로 끼어들기 · 호감도가 이어지는지는 그것을 거는 코드가 생긴 뒤 확인한다.
- 에디터 패널(`ScheduleDebugPanel`)을 실물로 열어 확인(에디터 시나리오 쪽 일).
함정: 내비 장애물은 이름 목록의 오브젝트 자리 + 반지름이다(메시 경계는 비동기 로드라 결정적이지 않아 쓰지 않았다). 시계를 감지 않고 오래 걸으면 나무 옆에서 미끄러지며 일정상 자리를 3 m 넘게 놓칠 수 있다(놓치면 그 자리에 놓인다).
