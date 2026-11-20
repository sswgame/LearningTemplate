---
name: preflight
description: "이 저장소의 묶음 끝 전체 검증과 push 절차: Windows Release · ASan · 서버(Release) · shipping-check, Linux, 로그 오류 · 경고 분류, 검증한 커밋만 push. 묶음을 끝낼 때, 검증 · 푸시 · 병합 전에, '다 됐다' 고 말하기 전에 쓴다."
---

# preflight

단계마다는 코드 → 린트 → 커밋만 한다. 아래는 묶음이 끝난 뒤 한 번만 돌린다. 반복 수정 중에는 영향 파일 · 증분 빌드 · 실패 테스트만 돌린다.

## 순서

1. 커밋이 다 됐는지 본다: `git status`, HEAD 변화. 훅이 커밋을 막을 수 있으니 메시지만 믿지 않는다.
   **새 이름 검토:** `py -3 -m Scripts api-diff --base <묶음 시작 커밋> [작업자 브랜치 …]` 가 낸 새 함수 이름을 언리얼 · 유니티 · Godot 의 같은 일 이름과 대조한다. 이름만으로 무엇을 하는지 모르는 것(`allowsOneMore` · `finishGraceFind`)은 검증 전에 바꾸고, 옛 이름은 이름 이력 표 `Config/Rules/Lint/NamingWordsBannedNames.json` 의 `_bannedNames` 에 올린다. `naming-words` 는 낱말이 표에 있는지만 보므로 이 단계를 건너뛰지 않는다(사용자 지적 2026-11-08). 브랜치를 둘 이상 주면 두 갈래가 각자 더한 같은 함수 · 멤버 · 등록 이름 · Config 키를 `겹침` 으로 내고 실패한다 — 병합 전에 한 갈래로 모은다. <!-- keep-old-name -->
   **합칠 때마다 먼저:** 작업자 갈래를 하나 합칠 때마다 메인 작업 폴더에서 Release 증분 빌드를 하고, 그 갈래가 더하거나 바꾼 테스트 · 시나리오만 dx12 로 혼자 돌려 고친 뒤 다음 갈래를 합친다(사용자 결정 2026-11-19). 실행해 본 적 없는 시나리오가 preflight `changed` 에서 한꺼번에 지면 고치는 데 몇 시간이 든다.
2. **`bash Scripts/dev/preflight.sh --no-linux` 를 백그라운드로 한 번.** 단계 · 갈래 · 끝 줄 · 로그 자리의 정본은 `bash Scripts/dev/preflight.sh --help`(스크립트 머리말)와 `--dry-run`(돌릴 명령 차례)이다 — 여기에 다시 적지 않는다(두 곳에 적은 끝 줄 이름이 어긋났다). 스크립트를 고치면 `--dry-run` 으로 명령 순서를 본다. 판단은 아래만:
   - 작업 트리가 깨끗해야 시작하고(커밋 그 자체를 검증), 끝에 HEAD 가 그대로이고 추적 파일이 바뀌지 않았어야 통과다. 이 동안 본 트리의 Source · Content 를 고치지 않는다 — 그 사이 코드는 다음 묶음으로 미룬다.
   - 묶음마다는 `--no-linux` 로 Windows 까지만 돌린다 — Linux(`linux-verify` 의 `all`, 이어서 `sanitize address`)는 여러 묶음을 push 한 뒤 모아서 한 번(할 일을 끝낼 때 · Linux 전용 코드를 고친 묶음일 때, 사용자 결정 2026-11-18 "리눅스는 나중에 해"). 그때도 Windows 가 지면 Linux 를 돌리지 않는다(사용자 결정). Server 배포 구성은 묶음마다 돈다(사용자 규칙).
   - Debug 를 짓지 않는다 — 단언 · 검사는 `SW_DO_CHECKS` 로 Release 에 있다(사용자 결정 2026-11-18). Debug 프리셋은 사람이 디버거로 볼 때만 쓴다.
   - 단계 기록: 단계가 통과한 sha 뒤 바뀐 파일이 모두 `Config/Dev/Verify.json` 의 `_nonInputPaths`(문서 · 스킬) 안이면 그 단계는 건너뛴다. 목록에는 확실한 것만 더한다(잘못 넣으면 검증 안 된 단계를 건너뛴다). 드라이버 · 도구처럼 git 밖 입력을 바꿨으면 `build/verify.stages.json` 을 지우고 돌린다.
   - `lint --all` 이 지면(낡은 예외 · 기준선 줄은 여기서 처음 걸린다) 표를 고쳐 커밋하고 다시 돌린다. clang-tidy 경고는 집계만 찍히니 읽고 고칠 것은 고친다.
   - `changed` 표의 줄마다 실패 종류 · 까닭 · 그 파일을 더한 갈래가 있으니 그 갈래 몫으로 고친다(`전제` 는 시나리오의 가정이 틀렸다, `백엔드 <이름>` 은 그 백엔드만). `MissingBaseline` 은 없던 골든 · 스냅숏을 처음 쓴 것이다 — 그림 · 글을 보고 맞으면 커밋하고 `--from changed` 로 기록 없이 통과시킨다(`-scenario-update-golden` · `--update-snapshots` 로 다시 뜨지 않는다 — 진 기준 파일까지 덮어 회귀를 가린다).
   - 격리 표(`Config/Rules/Test/Quarantine.json`)의 만료가 지났으면 처음에 진다 — 고쳐서 빼거나 사유 · 새 만료를 적는다. 끝 줄 `FLAKY=` 가 지면(격리 표 밖 흔들림이 문턱을 넘었다) 다시 돌려 넘기지 않는다: `py -3 -m Scripts test-history report` 의 줄을 근거로 고치거나 격리 표에 적는다. 느려짐 경고 줄은 판정을 바꾸지 않지만 원인을 본다.
   - 한 단계가 지면 고친 뒤 처음부터 돌리지 않는다: 고친 것이 앞 단계에 영향을 주지 않으면 `--from changed|cpu|gpu-release|linux` 로 그 단계부터 돌린다.
3. **로그 분류:** `build/<프리셋>/Testing/Logs/*.log` 의 Error · Warning 을 id 별로 모아 테스트가 선언한 것인지 본다. 통과만 보지 않는다.
4. **push:** Windows 단계가 모두 통과하면 끝 줄 `WINDOWS_OK=<sha>` 의 sha 를 Linux 를 기다리지 않고 `git push origin <sha>:main`(사용자 결정 2026-11-09). git pre-push 훅(`py -3 -m Scripts setup` 이 깐다)이 main 으로 가는 sha 가 `build/verify.ok`(그 통과 기록)와 다르면 막는다 — 막히면 검증하지 않은 커밋이다. Linux 가 지면 고친 커밋을 다음 검증에서 다시 push 한다. 검증 뒤에 코드를 고쳤으면 그 커밋은 아직 검증되지 않았다.

실패하면 `scenario-debug` 로 고치고 **진 테스트만** 다시 돌린다 — 체인의 진 단계는 `py -3 -m Scripts scenario-bisect <체인> --rerun-failed --preset <프리셋>`(Linux 는 `py -3 -m Scripts linux-scenarios rerun <체인>`)가 마지막 보고에서 골라 혼자 → 앞을 붙여 → 이분 탐색으로 돈다(사용자 결정 2026-11-12 — 고친 것이 공통 코드여도 통과한 단계를 처음부터 다시 돌리지 않는다). 진 테스트가 통과하면 진 단계의 다음 단계부터 `--from` 으로 잇는다(예: ASan nogpu 에서 졌으면 그 테스트만 ASan 으로 다시 돌리고 `--from gpu-release`, Release 체인에서 졌으면 그 체인만 다시 돌리고 `--from linux`). 시나리오 · 테스트 한 곳만 고쳤고 남은 단계가 없으면 그 체인만 통과해도 push 한다.

## 파이썬 도구

검증 · 다시 돌리기에 쓰는 파이썬 도구는 `py -3 -m Scripts <명령>` 표 하나다(`py -3 -m Scripts --help`). `preflight.sh` 는 셸이라 표에 없다 — `bash Scripts/dev/preflight.sh`.

| 명령 | 쓰는 때 |
| --- | --- |
| `api-diff --base <묶음 시작> [브랜치 …]` | 1 단계 새 이름 검토 · 갈래 사이 겹침 |
| `test-history report` · `flaky` | 흔들림 · p95 추세(격리 근거), 흔들림 판정(끝 줄 `FLAKY=`) |
| `scenario-bisect <체인> --rerun-failed --preset <프리셋>` | 진 체인 단계만 다시(혼자 → 앞 단계 → 이분 탐색) |
| `linux-scenarios rerun <체인>` | Linux 진 단계만 다시 |
| `verify-record skip <단계> <sha>` | 단계 기록으로 건너뛸지 미리 본다 |
| `shipping-check [--skip-build]` · `tidy --changed origin/main` | 배포 기동 · 바뀐 파일 clang-tidy 를 따로 볼 때 |

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
