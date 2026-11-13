---
name: scenario-debug
description: "이 저장소의 자동화 시나리오 실패를 추적하는 절차: 단독 · 체인 재현, 앞 단계 이분 탐색, 사진 · 임시 로그, 거짓 통과 찾기. 시나리오 · 체인 단계가 지거나 흔들릴 때, 체인에서만 지거나 단독으로만 질 때, <WaitUntil> gave up · did not settle 같은 판정 경고를 볼 때 쓴다."
---

# scenario-debug

**증거 전에 원인을 말하지 않는다.** 가설은 재현 → 값 또는 사진으로 확인한 뒤에 사용자에게 말한다.

## 1. 모양 가르기

같은 시나리오를 단독과 체인으로 돌려 가른다.

```
# Windows Debug (빠르다, 먼저 시도). 백엔드는 하나로. 로그는 dbg.<백엔드>.log
build/Ninja-Debug/Bin/App.exe -EnableEditor -scenario=<경로,경로> -scenario-keep-going -unattended -scenario-backends=dx12 -log-file=dbg.log
# Linux
py -3 Scripts/dev/linux_scenarios.py stages 이름 앞단계,이름
```

- 에디터 자체 테스트는 케이스 하나만: `-scenario=engine/automation/editor/selftests.scenario.xml -gv_editorSelfTestPattern=hierarchy.tagFilter` (시나리오 사본을 만들지 않는다).
- 체인을 처음부터 다시 돌리지 않는다: 진 단계와 의심 앞 단계만 `-scenario=` 에 넘긴다.
- `-scenario-keep-going` 을 빼지 않는다. 앞 단계가 건너뜀 · 실패하면 뒤 단계가 돌지 않아 통과로 착각한다.
- 게임 시나리오는 그 시나리오 `<Launch>` 값을 인자로 함께 넘긴다.
- 단독으로만 지면: 앞 단계의 상태에 가려진 진짜 결함이나 타이밍이다. 단독 실행이 정답에 가깝다.
- 체인에서만 지면: 앞 단계가 남긴 상태다 → 2 번.
- 한 OS 에서만 지면: 다른 OS 에서 같은 묶음을 돌린다. Windows 에서 재현되면 Windows 에서 고친다(10 배 빠르다).

## 2. 앞 단계 찾기

Windows: `py -3 Scripts/dev/scenario_bisect.py <체인> <단계> [--preset Ninja-Debug] [--backend dx12] [--show]`, Linux: `py -3 Scripts/dev/linux_scenarios.py bisect <단계> [체인]` — 같은 함수로 앞 단계들을 이분 탐색한다(단계 목록 · 인자는 ctest 명령에서 가져오므로 게임 체인도 된다, 그 백엔드의 체인 로그가 있어야 한다). 단독 쌍으로 지지 않으면 뒤 단계와 함께 만드는 상태다. 그때는 묶음을 직접 좁힌 뒤, 범인 시나리오의 구간을 빼 가며 돌린다.

자주 나온 상태: 눌린 채 끝난 입력(단계 초기화 `EditorAutomationStageReset`), 그리지 않는 위젯의 마지막 값(ImGuizmo), 숨긴 창(무인 실행의 `showWindow( false )` — 최소화가 아니다), 포커스.

## 3. 값과 사진

- **사진:** 임시 사본 시나리오에 `<CaptureEditorUI file="dbg/a.png"/>` 를 넣어 돌린다(`Bin/Saved/Automation/<시나리오>/<pid>/dbg/`). 이름과 `Scratch` 경로를 바꾼 사본을 쓰고 끝나면 지운다. 가장 최근 pid 폴더를 본다(`ls -td`).
- **값:** 임시 `SW_LOG_WARNING_ID( "DBG.X", … )` 를 넣고 증분 빌드(`cmake --build --preset Ninja-Debug --target App`)로 돌린다. 프레임 번호 · `GetActiveID` · 눌림 상태를 함께 찍는다. 끝나면 되돌린다 — 커밋 전에 `git diff` 에 `DBG` 가 없어야 한다. 편집기 코드는 `ImGui::IsKeyDown` 이 금지라 `GetKeyData( … )->Down` · `GetIO().MouseDown[]` 을 쓴다.
- 임시 파일은 스크래치패드에 둔다.

## 4. 고치기

시나리오의 시한을 늘리거나 고정 대기를 넣지 않는다(구조로 푸는 원칙은 CLAUDE.md 1).

- 입력 대상 대기는 단계가 맡는다(`holdUntilTextTarget` 등). 시나리오에 `<Wait>` 를 더하지 않는다.
- 레이블이 복합 위젯의 엉뚱한 항목에 찍히면 위젯 함수의 `pMark` 인자로 옮긴다.
- 고친 결함이 다시 나오지 않게 시나리오 단계나 단위 테스트를 남긴다. 음성 대조로 그 테스트가 진짜 잡는지 본다.

## 5. 거짓 통과 살피기

통과만 보지 않는다.

- 입력 뒤에 효과를 보는 `Expect` 가 있는가. 없으면 글자를 잃거나 엉뚱한 곳을 눌러도 통과한다.
- 끄고 켠 두 장이 실제로 다른가(`game/empty/automation/deferredtransparent` 처럼). 값을 바꿔도 그림이 같은 시나리오는 아무것도 보지 않는다.
- 기록 실행(`-scenario-update-golden`)의 통과는 그림의 증거가 아니다.
- 로그의 Error · Warning 을 선언한 것과 대조한다.
