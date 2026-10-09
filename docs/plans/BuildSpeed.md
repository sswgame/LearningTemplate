# 빌드 속도 계획

구조 정리(V1, 중복 정리, Core 층 정리, Engine · GameFramework 폴더 재배치)가 끝난 뒤의 빌드 속도 개선이다. 사용자 결정(2026-10-10): **1 ~ 6 단계까지 진행하고 7 ~ 9 는 필요할 때만 한다.**

## 측정한 현재 상태 (Debug `.ninja_log`, 명령 하나당 한 번 센 CPU 시간)

| 구분 | CPU 시간 | 개수 | 비율 |
|---|---|---|---|
| 일반 컴파일 | 2,435 s | 2,413 TU | 약 85 % |
| PCH 생성 | 370 s | 86 개(타깃마다 하나) | 13 % |
| 링크 | 23 s | 85 | 1 % 미만 |
| 리플렉션 코드젠 | 40 s | 28 | 1 % |

- 모듈별 컴파일: Engine 688 TU 805 s · GameFramework 833 TU 671 s · Editor 143 TU 123 s · Core 84 TU 72 s · **시험 614 TU 732 s(26 %)**.
- 16 스레드 기준 풀 빌드의 이론 하한은 약 170 ~ 180 초다.
- `GameObjectManager.cpp` 를 PCH 없이 `-ftime-trace` 로 재면 전체 2.16 초 중 헤더 파싱(`Total Source`)이 1.22 초(57 %)다.
- 가장 느린 TU: `Physics/Jolt/*` 래퍼 여섯이 각 5.3 ~ 6.7 초(합 약 36 초), `EngineLoop.cpp` 6.5 초.
- 수치는 `.ninja_log` 에 남은 한 번의 기록이라 시점이 섞였을 수 있다. 정확한 기준선은 0 단계에서 새로 잰다.

## 단계

### 0. 기준선 (먼저, 다른 빌드가 돌지 않을 때) — 도구 완료, 측정 남음
도구: `py -3 Scripts/dev/RunBuildBaseline.py --preset Ninja-Debug --preset Ninja-Release` · `py -3 Scripts/lint/report/RunIncludeCost.py`.

같은 PC · 같은 프리셋(`Ninja-Debug`, `Ninja-Release`)에서 표 하나를 채운다: 풀 빌드(콜드, sccache 비움) · 헤더 하나 수정(`Core/Container/vector.h`) · `.cpp` 하나 수정 · 워크트리 콜드(sccache 웜) 각 3 회의 중앙값.
측정 PC 이름(CPU)을 표 머리에 적는다 — 이 저장소의 측정은 PC 마다 다르다. 전 TU `-ftime-trace` 집계 스크립트(`Scripts/lint/report/RunIncludeCost.py`)를 만들어 누적 파싱 시간 상위 헤더 20 개를 뽑는다. 결과는 [검증과 측정](../08_Verification.md) 에 둔다.

### 1. 시험을 기본 빌드에서 뺀다 — 완료(`AllTests` 타깃 + ctest 픽스처, 결정은 [docs/09](../09_Decisions.md) 5-2)
`cmake --build --preset Ninja-Debug` 가 시험 614 TU(26 %)까지 짓는다. 개발 기본 타깃을 `App` · `Server` 로 줄이고(시험 실행 파일은 `EXCLUDE_FROM_ALL` 이 아니라 별도 `all-with-tests` 타깃과 `ctest` 가 짓게 하는 쪽을 비교), CI · 검증 프리셋은 시험까지 짓는다.
주의: `ctest` 만 부르면 시험 실행 파일이 없어 실패하므로 `Scripts` 의 `test` 명령과 `docs/08_Verification.md` 의 "무엇을 돌려야 하나" 절이 시험 빌드를 먼저 부르게 한다.

### 2. 헤더 다이어트 (Core 층 정리 뒤)
0 단계의 상위 헤더 20 개를 전방 선언 · include 정리로 줄인다(`RunForwardDeclarationCandidates.py --apply` 로 후보를 뽑는다). 한 헤더씩 커밋하고 전후 시간을 잰다.
`String/Logger.h` · `hashed_string.h` · `Container/vector.h` 처럼 모든 TU 가 읽는 헤더가 가장 크다. 기준: 헤더 파싱 비율(`Total Source` / 전체)을 57 % 에서 40 % 아래로.

### 3. PCH 공유 (GameFramework 폴더 재배치 뒤)
타깃마다 따로 만드는 PCH 86 개(370 CPU 초)를 `REUSE_PRECOMPILE_HEADERS_FROM` 으로 층마다 하나로(Core · Engine · 킷). 공유하려면 컴파일 정의가 같아야 한다 — 타깃마다 다른 `SW_LOG_TAG` · 내보내기 매크로를 PCH 가 읽는 정의에서 빼고 소스 쪽 매크로(`SW_LOG_CALLER`)와 헤더로 옮긴다.
확인: 모든 타깃의 `compile_commands.json` 정의 차이 목록(스크립트로)을 먼저 뽑는다.

### 4. sccache 와 PCH 의 충돌 (Windows `/Yu` 는 캐시되지 않는다)
구성을 둘로 나눈다: **개발 증분**(PCH ON, 유니티 OFF)과 **콜드 · 워크트리 · CI**(PCH OFF + 유니티 + sccache 웜). 프리셋 이름은 `Ninja-Debug`(개발) · `Ninja-Debug-Cold`(콜드). 0 단계 표에서 세 구성(PCH · PCH 없이 sccache · 유니티)의 풀 빌드와 워크트리 콜드를 비교해 쓸 가치가 있는 조합만 남긴다.

### 5. 유니티 범위 확대
지금은 게임 모듈만 `UNITY_BATCH 8` 이다. 헤더 파싱이 57 % 이므로 Engine · GameFramework 킷에 4 단계의 콜드 구성에서만 켠다(증분에는 불리하다). 배치 크기는 4 · 8 · 16 을 재서 정한다. 리플렉션 등록기와 이름 충돌(익명 네임스페이스 · `static` 중복)은 유니티에서 터지므로 `sw_skipUnitySources` 의 예외 목록을 같이 본다.

### 6. 무거운 단일 TU
`Physics/Jolt/*` 래퍼 여섯(합 약 36 초)을 한 유니티로 묶거나 Jolt 전용 PCH 를 둔다. `EngineLoop.cpp`(6.5 초, 1.6 천 줄)는 헤더 정리와 함께 본다.

## 하지 않는 것 — 필요할 때만 (사용자 결정)
7. 헤더 영향 범위(팬아웃) 감시 보고서, 8. Debug 디버그 정보 줄이기(`-gline-tables-only` 계열), 9. 링크 단위 분할(엔진 분할 계획 1 단계 이후).

## 순서
1(시험 빌드 분리)은 지금 해도 된다. 0 은 다른 빌드가 없을 때. 2 는 Core 층 정리 뒤, 3 은 폴더 재배치 뒤, 4 · 5 · 6 은 그 뒤. 각 단계가 끝나면 0 단계 표를 다시 채워 전후를 남기고, 효과가 없으면 되돌린다(`docs/09_Decisions.md` 3절에 기각 사유와 숫자를 적는다).
