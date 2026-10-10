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
- 가장 느린 TU: `EngineLoop.cpp` 6.5 초. `Physics/Jolt/*` 래퍼(5.3 ~ 6.7 초로 보였던 것)는 다른 빌드와 겹쳐 잰 값이었고, 조용할 때 각 1.9 ~ 2.5 초였다(6 단계).
- 수치는 `.ninja_log` 에 남은 한 번의 기록이라 시점이 섞였을 수 있다. 정확한 기준선은 0 단계에서 새로 잰다.

## 단계

### 0. 기준선 — Debug 측정 완료(Release 는 사용자 결정으로 재지 않음)
도구: `py -3 Scripts/dev/RunBuildBaseline.py --preset Ninja-Debug --preset Ninja-Release` · `py -3 Scripts/lint/report/RunIncludeCost.py`.

같은 PC · 같은 프리셋(`Ninja-Debug`, `Ninja-Release`)에서 표 하나를 채운다: 풀 빌드(콜드, sccache 비움) · 헤더 하나 수정(`Core/Container/vector.h`) · `.cpp` 하나 수정 · 워크트리 콜드(sccache 웜) 각 3 회의 중앙값.
측정 PC 이름(CPU)을 표 머리에 적는다 — 이 저장소의 측정은 PC 마다 다르다. 전 TU `-ftime-trace` 집계 스크립트(`Scripts/lint/report/RunIncludeCost.py`)를 만들어 누적 파싱 시간 상위 헤더 20 개를 뽑는다. 결과는 [검증과 측정](../08_Verification.md) 에 둔다.

2026-10-10, PC `SW` · AMD Ryzen 7 6800H(논리 코어 16), 커밋 `0d054352a`(main `8653de727` + 스크립트 수정), `Ninja-Debug`, 타깃 `all`, 3 회 중앙값:

| 시나리오 | 중앙값(s) | 회차(s) |
|---|---:|---|
| 풀 빌드(sccache 없음) | 131.5 | 125.9 · 131.5 · 131.7 |
| 헤더 하나 수정(`Core/Container/vector.h`) | 131.6 | 131.6 · 132.7 · 130.7 |
| `.cpp` 하나 수정(`GameObjectManager.cpp`) | 7.2 | 7.4 · 7.2 · 7.0 |
| 워크트리 콜드(sccache 웜) | 142.9 | 141.2 · 146.8 · 142.9 |

헤더 시나리오가 풀 빌드와 같다 — Core 기본 헤더 하나를 고치면 사실상 전 TU 를 다시 컴파일한다(2 단계의 근거). 표의 원본은 [검증과 측정](../08_Verification.md) "빌드 속도" 절이다.

### 1. 시험을 기본 빌드에서 뺀다 — 완료(`AllTests` 타깃 + ctest 픽스처, 결정은 [docs/09](../09_Decisions.md) 5-2)
`cmake --build --preset Ninja-Debug` 가 시험 614 TU(26 %)까지 짓는다. 개발 기본 타깃을 `App` · `Server` 로 줄이고(시험 실행 파일은 `EXCLUDE_FROM_ALL` 이 아니라 별도 `all-with-tests` 타깃과 `ctest` 가 짓게 하는 쪽을 비교), CI · 검증 프리셋은 시험까지 짓는다.
주의: `ctest` 만 부르면 시험 실행 파일이 없어 실패하므로 `Scripts` 의 `test` 명령과 `docs/08_Verification.md` 의 "무엇을 돌려야 하나" 절이 시험 빌드를 먼저 부르게 한다.

### 2. 헤더 다이어트 (Core 층 정리 뒤)
0 단계의 상위 헤더 20 개를 전방 선언 · include 정리로 줄인다(`RunForwardDeclarationCandidates.py --apply` 로 후보를 뽑는다). 한 헤더씩 커밋하고 전후 시간을 잰다.
`String/Logger.h` · `hashed_string.h` · `Container/vector.h` 처럼 모든 TU 가 읽는 헤더가 가장 크다. 기준: 헤더 파싱 비율(`Total Source` / 전체)을 57 % 에서 40 % 아래로.

0 단계 집계(2026-10-10, 위와 같은 PC · 커밋, `RunIncludeCost.py --preset Ninja-Debug --jobs 8`, 다른 빌드와 겹쳐 돌았으므로 순위만 믿는다):
TU 1936 개 중 헤더 파싱 합 2619.9 s / 컴파일러 합 3190.5 s = **82 %**(구문 검사만이라 진짜 빌드 비율보다 높다).
시험을 짓지 않은 트리라 시험 TU 372 개는 코드젠 산출물이 없어 구문 검사가 졌다 — 시험까지 넣으려면 `--target all AllTests` 로 지은 뒤 다시 돌린다.

| 순위 | 누적(s) | TU | 평균(ms) | 헤더 |
|---:|---:|---:|---:|---|
| 1 | 2376.3 | 1323 | 1796.1 | `Source/Engine/pch.h` |
| 2 | 1709.1 | 1426 | 1198.5 | `Source/Core/CoreMinimal.h` |
| 3 | 891.4 | 1426 | 625.1 | `Source/Core/Common/StdHeaders.h` |
| 4 | 682.6 | 1426 | 478.7 | `Source/Core/Common/Defines.h` |
| 5 | 681.7 | 1426 | 478.1 | `Source/Core/Common/Types.h` |
| 6 | 679.8 | 1426 | 476.7 | `MSVC/include/string_view` |
| 7 | 666.9 | 1426 | 467.7 | `MSVC/include/__msvc_string_view.hpp` |
| 8 | 593.8 | 1355 | 438.2 | `Source/Core/Common/PlatformOsHeaders.h` |
| 9 | 590.2 | 1336 | 441.8 | `Source/Engine/EngineMinimal.h` |
| 10 | 590.1 | 1426 | 413.8 | `MSVC/include/iosfwd` |
| 11 | 587.3 | 1326 | 442.9 | `Source/Engine/Common/Common.h` |
| 12 | 535.1 | 1426 | 375.2 | `MSVC/include/cwchar` |
| 13 | 533.9 | 1426 | 374.4 | `WinSDK/ucrt/wchar.h` |
| 14 | 509.6 | 1426 | 357.4 | `clang/intrin.h` |
| 15 | 503.4 | 1426 | 353.0 | `clang/x86intrin.h` |
| 16 | 483.8 | 1426 | 339.3 | `clang/immintrin.h` |
| 17 | 333.8 | 1355 | 246.4 | `WinSDK/um/Windows.h` |
| 18 | 205.9 | 1355 | 151.9 | `WinSDK/um/Unknwn.h` |
| 19 | 164.7 | 1357 | 121.4 | `WinSDK/um/ole2.h` |
| 20 | 145.6 | 1357 | 107.3 | `WinSDK/um/objbase.h` |

### 3. PCH 공유 (GameFramework 폴더 재배치 뒤)
타깃마다 따로 만드는 PCH 86 개(370 CPU 초)를 `REUSE_PRECOMPILE_HEADERS_FROM` 으로 층마다 하나로(Core · Engine · 킷). 공유하려면 컴파일 정의가 같아야 한다 — 타깃마다 다른 `SW_LOG_TAG` · 내보내기 매크로를 PCH 가 읽는 정의에서 빼고 소스 쪽 매크로(`SW_LOG_CALLER`)와 헤더로 옮긴다.
확인: 모든 타깃의 `compile_commands.json` 정의 차이 목록(스크립트로)을 먼저 뽑는다.

### 4. sccache 와 PCH 의 충돌 (Windows `/Yu` 는 캐시되지 않는다)
구성을 둘로 나눈다: **개발 증분**(PCH ON, 유니티 OFF)과 **콜드 · 워크트리 · CI**(PCH OFF + 유니티 + sccache 웜). 프리셋 이름은 `Ninja-Debug`(개발) · `Ninja-Debug-Cold`(콜드). 0 단계 표에서 세 구성(PCH · PCH 없이 sccache · 유니티)의 풀 빌드와 워크트리 콜드를 비교해 쓸 가치가 있는 조합만 남긴다.

### 5. 유니티 범위 확대
지금은 게임 모듈만 `UNITY_BATCH 8` 이다. 헤더 파싱이 57 % 이므로 Engine · GameFramework 킷에 4 단계의 콜드 구성에서만 켠다(증분에는 불리하다). 배치 크기는 4 · 8 · 16 을 재서 정한다. 리플렉션 등록기와 이름 충돌(익명 네임스페이스 · `static` 중복)은 유니티에서 터지므로 `sw_skipUnitySources` 의 예외 목록을 같이 본다.

### 6. 무거운 단일 TU — Jolt 완료, `EngineLoop.cpp` 남음
Jolt 래퍼 다섯은 자기 PCH 를 가진 `EngineJolt_objects` 로 옮겼다(합 11.1 → 5.5 s, 숫자는 [검증과 측정](../08_Verification.md) "빌드 속도"). 시간의 63 % 가 PCH 없이 다시 읽던 엔진 `pch.h` 였고 Jolt 헤더는 10 % 였다.
유니티 하나로 묶는 안은 합이 비슷하지만(추정 ~6 s) 파일 하나를 고칠 때마다 다섯 개를 다시 짓고, 소스가 컴파일 DB 에서 빠져 `CheckSourceGlob` · clangd 와 부딪쳐 고르지 않았다.
`EngineLoop.cpp`(6.5 초, 1.6 천 줄)는 헤더 정리와 함께 본다.

## 하지 않는 것 — 필요할 때만 (사용자 결정)
7. 헤더 영향 범위(팬아웃) 감시 보고서, 8. Debug 디버그 정보 줄이기(`-gline-tables-only` 계열), 9. 링크 단위 분할(엔진 분할 계획 1 단계 이후).

## 순서
1(시험 빌드 분리)은 지금 해도 된다. 0 은 다른 빌드가 없을 때. 2 는 Core 층 정리 뒤, 3 은 폴더 재배치 뒤, 4 · 5 · 6 은 그 뒤. 각 단계가 끝나면 0 단계 표를 다시 채워 전후를 남기고, 효과가 없으면 되돌린다(`docs/09_Decisions.md` 3절에 기각 사유와 숫자를 적는다).
