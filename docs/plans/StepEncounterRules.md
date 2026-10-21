# 걸음 조우 판정 하나로

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: G-7 · 진행: 1/2

Overworld `shouldEncounterOnStep` 의 결정적 주기와 ClassicJRPG `JRPGEncounterWalker` 의 확률 + 유예가 따로 걸음을 세고 판정했습니다. 2026-10-22 사용자 결정으로 착수 조건(오버월드 위에 JRPG · 몬스터 수집 게임)을 갈음했습니다.

## 1. 정한 것

- 기반 `GameFramework/Base/World/Encounter/StepEncounter.h` 에 규칙 `StepEncounterRule`(방식 `Period` · `Chance`, rate, 유예 걸음)과 셈 `StepEncounterCounter`(총 걸음 · 조우 뒤 걸음, `advance` · `roll` · `markEncounter`)를 둡니다. 두 키트가 이 하나를 씁니다.
- Overworld 는 `Period`(난수 없음, 유예 0)이고 조우 칸 걸음만 셉니다. ClassicJRPG 는 `Chance` 이고 자기 난수를 넘깁니다. 몬스터 수집 게임은 지역 표와 자기 난수로 같은 셈을 돌립니다.
- 세이브 바이트는 바꾸지 않았습니다. Overworld 는 예전 `uint32` 칸에 조우 칸 걸음 수를, JRPG 는 난수 · 조우 뒤 걸음 · 총 걸음을 그대로 씁니다. 범위 검사는 `StepEncounterCounter::restore` 한 곳입니다.

## 2. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| E1 | 기반 규칙 · 셈, 두 키트 옮기기, 테스트 이름 바꾸기(`StepEncounterCounter::isPeriodicEncounter`) | 끝(빌드 없이 작성) |
| E2 | 빌드 · `TestClassicJRPG` · `TestGameFramework` 실행(시드 결정성 · 세이브 왕복 케이스가 같은 걸음에 조우하는지) | 남음 |
