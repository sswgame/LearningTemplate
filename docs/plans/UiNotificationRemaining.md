# 알림 · 힌트 · 목표 마커의 남은 것

> 상태: 측정대기 · 선행: 없음 · 영역: ui · 옛번호: D6-5 · 진행: 2/2

**남은 확인(통합에서 확인):** 매니페스트 id `ui-notification-in-game`(`Test/QA/VerifyBatch.json`). 확인이 끝나면 통합 담당이 이 문서를 닫는다.

알림 · 힌트 · 목표 마커 중 둘이 남아 있었다. 착수 조건(퀘스트 키트의 월드 목표 오브젝트)은 사용자 지시(2026-10-22 "조건부도 구현")로 갈음했고, 목표 오브젝트가 없어 만들었다.
단위가 끝나면 그 절을 지운다.

## 1. 단위

| 단위 | 내용 | 선행 |
|---|---|---|
| N2 | 게임 하나에 붙여 보기 — meadowvillage 에 목표 자리 오브젝트 `OrchardSite`(`QuestObjectiveMarkerComponent`, 부탁 `meadow_request` 의 `Habitat`/`orchard`)를 두고, 탐침 `UI.MarkerVisible`(`Engine/UI/World/WidgetProbes.cpp`)과 시나리오 `game/meadowvillage/automation/questmarker.scenario.xml` 이 받으면 마커가 뜨고(이력 0 → 1) 다 하면 사라지는지(→ 0) 단언한다. 캡처 대신 가시성 이력으로 본다 | 없음 |

### N2 게임 하나에 붙여 보기

빌드 · 테스트는 끝났다(2026-10-23, `wt/uibuild`): `full-build`(Debug, `all AllTests`) 경고 0 · 오류 0, `EngineTest --test_filter=QuestTest.*` 3/3 통과(새 `ObjectiveTargetIsOpenOnlyWhileItsObjectiveRemains` 포함).

- 만들어 둠: 장면 `meadow.scene.xml` 의 `OrchardSite`(9, 0, 1.5), 탐침, 시나리오. 실행은 통합이 한다: `AppScenarioTest` 의 `meadowvillage.questmarker`(`day.scenario` 와 같은 흐름이라 부탁이 `Active` 인 구간이 있다).
  마커 가시성은 UI 갱신이 놓은 뒤의 마커 내용 위젯 가시성이고, `ExpectVisited` 가 프레임마다 읽어 이력으로 본다. 부탁이 `Active` 인 구간이 한두 프레임뿐이면 이력이 놓칠 수 있다 — 그때는 구간 앞뒤에 `AdvanceGameTime` 을 더 잘게 나눈다.

## 2. 끝난 단위의 확인 목록

- N1 목표 마커를 퀘스트 목표 오브젝트에: `QuestObjectiveMarkerComponent`(게임플레이 층 `Gameplay/SessionState`, `ObjectiveMarkerComponent` 파생)가 월드 목표다. 퀘스트 id · 종류 · 대상(일지가 `notify( kind, target )` 로 세는 두 이름)을 데이터로 갖고, 셋 중 하나라도 비면 시작할 때 오류다.
  보일지는 `WidgetComponent::shouldShowMarker`(UI 갱신에서, 병렬 틱 밖)가 `QuestLog::isObjectiveOpen` 으로 묻는다 — 진행 중 · 선택 대기 아님 · 지금 단계에 그 목표가 있고 다 세지 않음.
  일지는 씬의 판 상태(`SessionStateComponent`)가 하나일 때만 쓴다. 없거나 둘 이상이면 오류를 한 번 남기고 숨긴다. UI 층(`UI/Marker`)은 게임플레이 층을 볼 수 없어(CheckGameFrameworkLayers) 퀘스트를 아는 쪽을 파생으로 위에 두었다. 기반 `ObjectiveMarkerComponent` 는 예전처럼 늘 보인다(웨이포인트).
- 입력 힌트 위젯: 따로 두지 않기로 정했다. 리치 텍스트 행동 태그 `[action=이름]`(`UIActionGlyphSource`)이 장치 종류를 따라 글리프를 바꾸고 다시 배치되므로(`onInputGlyphsChanged`), 힌트는 그 태그가 든 글 위젯 하나다.
  위젯을 두면 글과 글리프의 배치 규칙이 두 벌이 된다. 버튼 모양 아이콘(텍스처 글리프)이 필요해지면 태그가 그리는 것을 바꾼다.
