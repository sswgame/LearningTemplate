---
name: bundle-verify
description: "이 저장소의 묶음 끝 전체 검증과 push 절차: Windows Debug · Release · 서버 · shipping-check, Linux, 로그 오류 · 경고 분류, 검증한 커밋만 push. 묶음을 끝낼 때, 검증 · 푸시 · 병합 전에, '다 됐다' 고 말하기 전에 쓴다."
---

# bundle-verify

단계마다는 코드 → 린트 → 커밋만 한다. 아래는 묶음이 끝난 뒤 한 번만 돌린다. 반복 수정 중에는 영향 파일 · 증분 빌드 · 실패 테스트만 돌린다.

## 순서

1. 커밋이 다 됐는지 본다: `git status`, HEAD 변화. 훅이 커밋을 막을 수 있으니 메시지만 믿지 않는다.
   **새 이름 검토:** `py -3 -m Scripts new-names --base <묶음 시작 커밋> [작업자 브랜치 …]` 가 낸 새 함수 이름을 언리얼 · 유니티 · Godot 의 같은 일 이름과 대조한다. 이름만으로 무엇을 하는지 모르는 것(`allowsOneMore` · `finishGraceFind`)은 검증 전에 바꾸고, 옛 이름은 `Config/Rules/Lint/NamingWords.json` 의 `_bannedNames` 에 올린다. `naming-words` 는 앞 단어만 보므로 이 단계를 건너뛰지 않는다(사용자 지적 2026-11-08). <!-- keep-old-name -->
2. **`bash Scripts/dev/VerifyBundle.sh` 를 백그라운드로 한 번.** 단계는 `build`(린트 ‖ Debug → Release → 서버 빌드) → `cpu`(Debug `ctest -L nogpu` ‖ 서버 단위 → launch-args → `shipping-check`) → `gpu-release`(Release ctest 전체 — 그림 · 골든 · 기록 대조의 정본) → `gpu-debug`(Debug `ctest -LE nogpu` — 체인은 시나리오마다 백엔드 하나, Debug 단언용)다. 시간을 판정하는 시나리오(GPU 단계)는 다른 일과 겹치지 않고, CPU 일끼리만 겹친다. 스크립트를 고치면 `--dry-run` 으로 명령 순서를 본다. ctest 슬롯 수는 `Config/Dev/Verify.json` 의 `_ctestJobs` 다. Windows 가 모두 통과해야 Linux(`linux-verify` 의 `all`, 이어서 `sanitize address`)가 시작한다. Windows 가 지면 Linux 를 돌리지 않는다(사용자 결정). 이 동안 본 트리의 Source · Content 를 고치지 않는다. 그 사이 코드는 다음 묶음으로 미룬다.
   `build` 단계는 `py -3 -m Scripts lint --all`(모든 규칙을 저장소 전체로, 로그 `build/verify.lint.log`)을 빌드와 겹쳐 돌린다 — 커밋 훅은 스테이지된 파일만 보므로 낡은 예외 · 기준선 줄은 여기서 처음 걸린다. 지면 표를 고쳐 커밋하고 다시 돌린다.
   ctest 는 격리 표(`Config/Rules/Test/Quarantine.json`, `TestHistory.py quarantine --ctest-exclude`)의 테스트를 `-E` 로 빼고 돈다 — 표의 만료가 지났으면 검증이 처음에 진다(고쳐서 빼거나 사유 · 새 만료를 적는다). ctest 가 끝날 때마다 보고 폴더(`build/<프리셋>/Bin/Saved/Automation`)를 `TestHistory.py ingest --commit <sha>` 로 이력(`Saved/TestHistory/history.sqlite`)에 쌓는다 — 진 실행도 쌓는다. 흔들림 · p95 추세는 `py -3 Scripts/dev/TestHistory.py report` 로 보고, 흔들리는 테스트를 격리할 때 그 줄을 근거로 적는다.
   한 단계가 지면 고친 뒤 처음부터 돌리지 않는다: 고친 것이 앞 단계에 영향을 주지 않으면 `--from cpu|gpu-release|gpu-debug|linux` 로 그 단계부터 돌린다.
3. **로그 분류:** `build/<프리셋>/Testing/Logs/*.log` 의 Error · Warning 을 id 별로 모아 테스트가 선언한 것인지 본다. 통과만 보지 않는다.
4. **push:** Windows 단계가 모두 통과하면(`WINDOWS_OK` 줄의 sha) Linux 를 기다리지 않고 `git push origin <sha>:main`(사용자 결정 2026-11-09). Linux 가 지면 고친 커밋을 다음 검증에서 다시 push 한다. 검증 뒤에 코드를 고쳤으면 그 커밋은 아직 검증되지 않았다.

실패하면 `scenario-debug` 로 고치고 **진 테스트만** 다시 돌린다(사용자 결정 2026-11-12 — 고친 것이 공통 코드여도 통과한 단계를 처음부터 다시 돌리지 않는다). 진 테스트가 통과하면 진 단계의 다음 단계부터 `--from` 으로 잇는다(예: Debug nogpu 에서 졌으면 그 테스트만 Debug 로 다시 돌리고 `--from gpu-release`, Release 체인에서 졌으면 그 체인만 다시 돌리고 `--from gpu-debug`). 시나리오 · 테스트 한 곳만 고쳤고 남은 단계가 없으면 그 체인만 통과해도 push 한다.

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
