# Scenario — 시나리오 형식과 단계 등록

## 이것은 무엇이고 왜 있나

시나리오 XML 의 형식(`AutomationScenario`), 단계를 올리는 등록표(`AutomationStepRegistry`), 시나리오 시계 · 입력 · 창 · 백엔드 · 환경 단계, 단계 문맥(`AutomationStepContext`), 타임라인, 녹화(`ScenarioRecorder`)가 있습니다.
테스트 판정(로그 게이트 · 보고 · 종료 코드 · Session · Chain · 이미지 지표)은 이 폴더가 모릅니다 — 판정은 [Automation](../Automation/README.md) 이 이 폴더를 내려다보며 합니다. 기능(UI · GameFramework · Editor · App)이 단계를 올릴 때 끌어오는 헤더는 이 폴더의 것뿐입니다.
탐침 레지스트리는 더 아래 `Engine/Probe` 에 있습니다. 시나리오 형식 · 단계 · 탐침 · 보고 읽는 법은 [Automation README](../Automation/README.md) 에 모여 있습니다(한 영역의 기능이라 설명을 갈라 두지 않는다).

## 폴더의 선

- 이 폴더는 `Engine/Automation` 의 어떤 헤더도 include 하지 않습니다(층 게이트 `CheckEngineLayers` 가 티어로 막는다. Scenario 11 < Automation 12).
- `AutomationScenario::getTestOptions()`(Scratch · Launch · Load · `isolated` · `editor` · `requireForeground`)는 형식이 테스트 쪽에 내주는 한 묶음입니다. 그 구조체와 사본 폴더(`AutomationScratch`)도 형식이 들고 있어 이 폴더에 있습니다.
- 파일 이름의 `Automation` 접두는 옛 이름입니다. 개명은 호출부가 없는 일이라 따로 하지 않았습니다.

## 더 볼 곳

단계 문맥 `AutomationStepContext.h`, 입력 맵 액션 단계 `AutomationActionSteps.cpp`, 녹화 `ScenarioRecorder.cpp`, 분리 계획 [Automation 분리](../../../docs/plans/AutomationSplit.md).
