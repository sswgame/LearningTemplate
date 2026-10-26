---
name: ci-triage
description: main CI 실패를 원인별로 묶어 보고한다 — 실패한 잡 · 주석을 읽고 같은 원인끼리 모아 첫 일 한 줄을 붙인다
argument-hint: "[실행 번호]"
---

# CI 실패 분류

읽기 전용이다. 고치는 일은 원인별로 작업자에게 맡긴다(새 에이전트 대신 그 영역 작업자에게 SendMessage).

1. 실행 번호가 없으면 SessionStart 훅이 알린 번호를 쓴다. 없으면 `py -3 -m Scripts ci-jobs --summary`.
2. `py -3 -m Scripts ci-jobs <번호> --annotations` 로 실패 잡마다 주석(파일:줄, 메시지)을 읽는다. 전체 로그는 열지 않는다.
   출력이 길면 `| grep -E '^(=== 잡|-- .github:[0-9]+ ctest)'` 로 실패 ctest 줄만 먼저 뽑는다.
3. 같은 메시지 · 같은 파일이면 한 원인으로 묶는다(구성만 다른 Clang 잡 여럿은 보통 한 원인).
4. 표로 보고한다: 원인 | 잡 | 파일:줄 | 첫 일 한 줄 | 로컬 재현 구성(Debug / Shipping / Release).
5. 끝에 같은 모양의 결함이 다른 곳에 있을 가능성을 한 줄 적는다.
