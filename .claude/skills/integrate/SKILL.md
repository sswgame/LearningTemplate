---
name: integrate
description: 병합 대기 wt/* 브랜치를 한 워크트리에 모아 빌드 · 검증 · 측정을 한 번씩 돌리고 push 한 뒤 NEXT 를 덮어쓰고 정리한다
disable-model-invocation: true
argument-hint: "<통합 이름 예: q>"
---

# 통합 절차

정본은 `docs/11_Workflow.md`. 여기는 순서만 둔다. 통합이 도는 중에는 범위를 늘리지 않는다.

1. **모으기**: `git branch --no-merged main` 의 `wt/*`. 체인(뒤가 앞 위에 쌓인 것)은 앞에서부터 병합한다. `py -3 -m Scripts worktree-make integ-<이름> --base main` 후 병합, 충돌은 NEXT 의 충돌 경고를 먼저 본다.
2. **Debug**: `py -3 -m Scripts integ-gate run` (빌드 경고 0 · lint · `-L nogpu`). 실패는 원인별로 고친다. 기준선을 다시 잡아 가리지 않는다.
3. **Shipping → Release**: 구성별로 한 번씩. GPU 잠금은 `shared-lock`. 시나리오 Warning/Error 는 종료 코드 0 이어도 실패(15).
4. **검증**: `py -3 -m Scripts verify-batch` 한 번. 결과 표 하나.
5. **push**: 통과하면 `git fetch` 로 origin 을 보고 force 없이 push. CI 는 기다리지 않는다.
6. **NEXT 덮어쓰기**: `docs/plans/NEXT.md` 를 세 절(마지막 통합 한 줄 / 남은 일 / 확인할 것)로 덮어쓰고 첫 줄 근처에 `<!-- next-format: one-page -->` 를 둔다. 절을 추가하지 않고 20KB 를 넘기지 않는다.
7. **정리**: 병합된 `wt/*` 는 `py -3 -m Scripts worktree-remove <이름>` 로만 지운다(체인의 미병합은 남긴다).
8. **보고**: 한국어 표(항목 | 결과), push 여부, 못 통과한 것과 첫 일, 같은 결함이 다른 곳에 있을 가능성 한 줄.
