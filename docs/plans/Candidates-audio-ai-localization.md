# 오디오 · 현지화 · AI 후보

> 상태: 후보 · 선행: 없음 · 영역: audio-ai-localization · 옛번호: D5-1, D5-2, D5-3 · 진행: 해당없음

이 문서는 착수 전 후보의 목록이다. 착수하면 절을 계획 문서로 옮기고 여기서 지운다. 오디오 엔진, 현지화, NPC AI, 게임 로직 자동화가 범위다.

## 기능

### LocalizationCultureData
제목: 현지화 문화권 데이터와 쿠킹 필터
옛번호: D5-2
왜 필요한가: 구현은 끝났다(2026-10-22 — RTL 문화권 `he` · `fa`, 엔진 `ja` 번역과 출시 문화권, 쿠킹 문화권 필터 `CookAssets.collectUnstagedTranslationTablesInternal`, Shooter3D HUD 의 든 아이템 이름 `_itemName`). 사람 눈 확인만 남았다.
남은 것:
- 사람 눈 확인: 일본어 화면의 잘림, `he` · `fa` 의 RTL 배치와 `۰-۹` 숫자, Shooter3D HUD 의 아이템 이름 줄 자리(무기 이름 위).
- `--check-text`(출시 문화권 `ja` 의 빠지거나 낡은 번역)는 대기가 아니다(2026-10-27 다시 봄): 같은 판정을 `LocalizationGatherTest.RepositoryProjectsAreUpToDate`(EditorTest, CI 와 통합의 nogpu)가 매번 한다.
못 하는 이유(2026-10-27): 글 잘림을 재는 탐침이 없어 사람이 화면을 봐야 한다(매니페스트 id `localization-check-text`). 풀리는 조건: 사람이 일본어 · RTL 화면을 볼 때, 또는 위젯 글 넘침을 세는 탐침이 생길 때.
