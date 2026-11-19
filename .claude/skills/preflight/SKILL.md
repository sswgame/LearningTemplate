---
name: preflight
description: "이 저장소의 묶음 끝 전체 검증과 push 절차: Windows Release · ASan · 서버(Release) · shipping-check, Linux, 로그 오류 · 경고 분류, 검증한 커밋만 push. 묶음을 끝낼 때, 검증 · 푸시 · 병합 전에, '다 됐다' 고 말하기 전에 쓴다."
---

# preflight

단계마다는 코드 → 린트 → 커밋만 한다. 아래는 묶음이 끝난 뒤 한 번만 돌린다. 반복 수정 중에는 영향 파일 · 증분 빌드 · 실패 테스트만 돌린다.

## 순서

1. 커밋이 다 됐는지 본다: `git status`, HEAD 변화. 훅이 커밋을 막을 수 있으니 메시지만 믿지 않는다.
   **새 이름 검토:** `py -3 -m Scripts api-diff --base <묶음 시작 커밋> [작업자 브랜치 …]` 가 낸 새 함수 이름을 언리얼 · 유니티 · Godot 의 같은 일 이름과 대조한다. 이름만으로 무엇을 하는지 모르는 것(`allowsOneMore` · `finishGraceFind`)은 검증 전에 바꾸고, 옛 이름은 `Config/Rules/Lint/NamingWords.json` 의 `_bannedNames` 에 올린다. `naming-words` 는 낱말이 표에 있는지만 보므로 이 단계를 건너뛰지 않는다(사용자 지적 2026-11-08). 브랜치를 둘 이상 주면 두 갈래가 각자 더한 같은 함수 · 멤버 · 등록 이름 · Config 키를 `겹침` 으로 내고 실패한다 — 병합 전에 한 갈래로 모은다. <!-- keep-old-name -->
   **합칠 때마다 먼저:** 작업자 갈래를 하나 합칠 때마다 메인 작업 폴더에서 Release 증분 빌드를 하고, 그 갈래가 더하거나 바꾼 테스트 · 시나리오만 dx12 로 혼자 돌려 고친 뒤 다음 갈래를 합친다(사용자 결정 2026-11-19). 실행해 본 적 없는 시나리오가 preflight `changed` 에서 한꺼번에 지면 고치는 데 몇 시간이 든다.
2. **`bash Scripts/dev/preflight.sh` 를 백그라운드로 한 번.** 단계는 `build`(린트 ‖ Release → 임포트 → ASan(`Ninja-Release-ASan`) → 서버(`Ninja-Release-Server`) 빌드) → `changed`(`origin/main` 뒤에 들어온 · 바뀐 테스트 · 시나리오만 Release 로 먼저 — 표 하나와 끝 줄 `CHANGED_OK=<sha>` · `CHANGED_FAILED`) → `cpu`(ASan `ctest -L nogpu` ‖ 서버 단위 → launch-args → `shipping-check`) → `gpu-release`(Release ctest 전체 — 그림 · 골든 · 기록 대조의 정본)다. 단언 · 검사는 배포가 아니면 켜져(`SW_DO_CHECKS`) Release 체인이 지나므로 Debug 를 짓지 않는다(사용자 결정 2026-11-18) — CPU 단위는 ASan 구성(Release + clang-cl ASan)이 돌고, 자동화 체인(`Scenario.*`, 부하 측정 제외)은 그래픽 API 검증 층(`-rhi-validation`) · 교착 탐지(`-deadlock-detection`) · bindless 죽은 번호 검사를 인자로 켠다 — 검증 층 오류 · 경고는 로그 게이트에서 진다. Debug 프리셋은 사람이 디버거로 볼 때만 쓴다. 시간을 판정하는 시나리오(GPU 단계)는 다른 일과 겹치지 않고, CPU 일끼리만 겹친다. 스크립트를 고치면 `--dry-run` 으로 명령 순서를 본다. ctest 슬롯 수는 `Config/Dev/Verify.json` 의 `_ctestJobs` 다. 묶음마다는 `--no-linux` 로 Windows 까지만 돌린다 — Linux(`linux-verify` 의 `all`, 이어서 `sanitize address`)는 여러 묶음을 push 한 뒤 모아서 한 번(할 일을 끝낼 때 · Linux 전용 코드를 고친 묶음일 때, 사용자 결정 2026-11-18 "리눅스는 나중에 해"). 그때도 Windows 가 지면 Linux 를 돌리지 않는다(사용자 결정). 이 동안 본 트리의 Source · Content 를 고치지 않는다. 그 사이 코드는 다음 묶음으로 미룬다.
   `build` 단계는 `py -3 -m Scripts lint --all`(모든 규칙을 저장소 전체로, 로그 `build/verify.lint.log`)을 빌드와 겹쳐 돌린다 — 커밋 훅은 스테이지된 파일만 보므로 낡은 예외 · 기준선 줄은 여기서 처음 걸린다. 지면 표를 고쳐 커밋하고 다시 돌린다.
   ASan 빌드 뒤 `py -3 -m Scripts tidy --changed origin/main` 이 묶음이 바꾼 파일만 clang-tidy 로 본다(겹쳐 돈다, 컴파일 DB 는 유니티를 끈 ASan 구성, 로그 `build/Ninja-Release-ASan/Testing/Logs/verify.tidy.log`) — `.clang-tidy` 의 WarningsAsErrors 검사가 하나라도 나오면 진다. 경고는 집계만 찍힌다. 읽고 고칠 것은 고친다.
   `cpu` 단계의 `shader warm` 갈래는 지난 Release 체인이 남긴 셰이더 요청 기록(`build/Ninja-Release/Testing/ShaderRequests/*.shaderrequests` — 체인 자식마다 하나, `-shader-request-log`)만 `App --cook-shaders -shader-request-dir=` 로 컴파일해 그 빌드의 셰이더 DDC 를 네 백엔드 모두 채운다 — 셰이더 · 공용 hlsli 를 고친 묶음도 GPU 단계가 캐시 미스 컴파일을 떠안지 않는다(로그 `build/Ninja-Release/Testing/Logs/ShaderWarm.log`, 첫 실행은 기록이 없어 할 일이 없다).
   ctest 는 격리 표(`Config/Rules/Test/Quarantine.json`, `test_history.py quarantine --ctest-exclude`)의 테스트를 `-E` 로 빼고 돈다 — 표의 만료가 지났으면 검증이 처음에 진다(고쳐서 빼거나 사유 · 새 만료를 적는다). ctest 가 끝날 때마다 보고 폴더(`build/<프리셋>/Bin/Saved/Automation`)를 `test_history.py ingest --commit <sha>` 로 이력(`Saved/TestHistory/history.sqlite`)에 쌓는다 — 진 실행도 쌓는다. 흔들림 · p95 추세는 `py -3 Scripts/dev/test_history.py report` 로 보고, 흔들리는 테스트를 격리할 때 그 줄을 근거로 적는다.
   `changed` 단계(`test_history.py changed`)는 빌드 없이 쓴 테스트 · 시나리오가 처음 도는 곳이다. 표의 줄마다 실패 종류 · 까닭 · 그 파일을 더한 갈래가 있으니 그 갈래 몫으로 고친다(`전제` 는 시나리오의 가정이 틀렸다, `백엔드 <이름>` 은 그 백엔드만). `MissingBaseline` 은 없던 골든 · 스냅숏을 이 단계가 처음 쓴 것이다 — 목록의 그림 · 글을 보고 맞으면 커밋하고 `--from changed` 로 기록 없이 통과시킨다(`-scenario-update-golden` · `--update-snapshots` 로 다시 뜨지 않는다 — 진 기준 파일까지 덮어 회귀를 가린다). 지면 cpu · gpu-release 는 돌지 않는다.
   한 단계가 지면 고친 뒤 처음부터 돌리지 않는다: 고친 것이 앞 단계에 영향을 주지 않으면 `--from changed|cpu|gpu-release|linux` 로 그 단계부터 돌린다.
3. **로그 분류:** `build/<프리셋>/Testing/Logs/*.log` 의 Error · Warning 을 id 별로 모아 테스트가 선언한 것인지 본다. 통과만 보지 않는다.
4. **push:** Windows 단계가 모두 통과하면(`WINDOWS_OK` 줄의 sha) Linux 를 기다리지 않고 `git push origin <sha>:main`(사용자 결정 2026-11-09). Linux 가 지면 고친 커밋을 다음 검증에서 다시 push 한다. 검증 뒤에 코드를 고쳤으면 그 커밋은 아직 검증되지 않았다.

실패하면 `scenario-debug` 로 고치고 **진 테스트만** 다시 돌린다 — 체인의 진 단계는 `py -3 Scripts/dev/scenario_bisect.py <체인> --rerun-failed --preset <프리셋>`(Linux 는 `linux_scenarios.py rerun <체인>`)가 마지막 보고에서 골라 혼자 → 앞을 붙여 → 이분 탐색으로 돈다(사용자 결정 2026-11-12 — 고친 것이 공통 코드여도 통과한 단계를 처음부터 다시 돌리지 않는다). 진 테스트가 통과하면 진 단계의 다음 단계부터 `--from` 으로 잇는다(예: ASan nogpu 에서 졌으면 그 테스트만 ASan 으로 다시 돌리고 `--from gpu-release`, Release 체인에서 졌으면 그 체인만 다시 돌리고 `--from linux`). 시나리오 · 테스트 한 곳만 고쳤고 남은 단계가 없으면 그 체인만 통과해도 push 한다.

## 보고

- `App --test` 와 에디터 자체 테스트 수를 따로 적는다(`editor.selftests` 의 `EditorSelfTest|DONE|<개수>|<실패>`).
- OS · 구성마다 건수와 빌드 경고 수를 적는다. 돌리지 않은 것은 돌리지 않았다고 적는다.
- 실패는 출력과 함께 그대로 적는다. 끝난 것은 단정해서 적는다.
- 고친 결함의 근본 원인과 같은 결함이 다른 곳에 있을 가능성을 한 줄로 적는다.
- `docs/NEXT.md` 에서 끝난 줄을 지운다. 지식은 정본 문서로 옮긴다.

## 지킬 것

- 사용자가 볼 때는 `--show` 로 돌린다(게임 · 에디터 테스트 창이 뜨고 테스트를 하나씩 돈다 — 사용자 요청 2026-11-08). 판정 · 골든은 같다.

- 키트는 Release 에서만 지어진다. 온라인 키트 테스트는 `UnitTests.OnlineKits`(OnlineLoadBot)가 돌린다.
- 검증 빌드 중에 그 트리를 다시 빌드하거나, 테스트 단계 중에 다시 짓지 않는다.
- 이름으로 프로세스를 죽이지 않는다(`App.exe` · `ninja` · `ctest`).
- 테스트 실행 명령 뒤에 `git commit` 을 잇지 않는다. 결과를 보고 커밋한다.
