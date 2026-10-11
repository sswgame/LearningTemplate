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
도구: `py -3 Scripts/dev/RunBuildBaseline.py --preset Ninja-Debug` · `py -3 Scripts/lint/report/RunIncludeCost.py`. 같은 PC · 같은 프리셋에서 풀 빌드(콜드) · 헤더 하나 수정 · `.cpp` 하나 수정 · 워크트리 콜드를 3 회 중앙값으로 잰다.
기준선 표(PC `SW` · Ryzen 7 6800H, 풀 131.5 s · 헤더 하나 131.6 s · `.cpp` 하나 7.2 s · 워크트리 콜드 142.9 s)와 상위 헤더 20 개는 [검증과 측정](../08_Verification.md) "빌드 속도" 절에 있다.
헤더 시나리오가 풀 빌드와 같다 — Core 기본 헤더 하나를 고치면 사실상 전 TU 를 다시 컴파일한다(2 단계의 근거). 헤더 파싱 비율 82 %(구문 검사 기준), 1위 `Engine/pch.h`, 2위 `CoreMinimal.h`.

### 2. 헤더 다이어트 (Core 층 정리 뒤)
0 단계의 상위 헤더 20 개를 전방 선언 · include 정리로 줄인다(`RunForwardDeclarationCandidates.py --apply` 로 후보를 뽑는다). 한 헤더씩 커밋하고 전후 시간을 잰다.
`String/Logger.h` · `hashed_string.h` · `Container/vector.h` 처럼 모든 TU 가 읽는 헤더가 가장 크다. 기준: 헤더 파싱 비율(`Total Source` / 전체)을 57 % 에서 40 % 아래로.


### 3. PCH 공유 (GameFramework 폴더 재배치 뒤)
타깃마다 따로 만드는 PCH 86 개(370 CPU 초)를 `REUSE_PRECOMPILE_HEADERS_FROM` 으로 층마다 하나로(Core · Engine · 킷). 공유하려면 컴파일 정의가 같아야 한다 — 타깃마다 다른 `SW_LOG_TAG` · 내보내기 매크로를 PCH 가 읽는 정의에서 빼고 소스 쪽 매크로(`SW_LOG_CALLER`)와 헤더로 옮긴다.
**킷은 끝났다**(풀 빌드 131.1 → 113.0 s, [검증과 측정](../08_Verification.md) "빌드 속도"): clang 은 PCH 와 `-D` 가 하나라도 다르면 거부하므로(`definition of macro 'SW_LOG_TAG' differs`) 타깃 고유 정의(`SW_LOG_TAG` · `SW_GF_EXPORTS`)를
PCH 뒤에 강제 포함하는 생성 헤더로 옮기고(`sw_setTargetDefsHeader`), 자동 정의 `<타깃>_EXPORTS` 는 끄고, 정의 없는 중립 소유자 `sw_kit_pch`(`sw_addSharedPchOwner`)가 PCH 를 만든다. 특정 킷이 소유하면 그 킷의 정의가 PCH 에 구워져 샌다.
남은 것: 게임 모듈(`SWGame`) · 에디터 확장 · RHI 백엔드 · 시험 묶음. 정의 차이는 `SW_IMPORTS` 유무 · `SW_*_INTERNAL`(App · Engine · Test 가 서로 다름) · 서드파티 정의(Jolt · Tracy)뿐이므로 같은 방식으로 묶음마다 소유자를 하나 둔다.
`SW_*_INTERNAL` 은 `EngineServices.h` 가 읽는 헤더 조건이라 PCH 에 영향을 줄 수 있다 — 묶음 안에서 같아야 한다. 차이 목록은 `compile_commands.json` 의 `-D` 를 타깃별로 모아 뽑는다(`SW_LOG_TAG="…"` 처럼 값이 붙은 것은 이름만 비교).

### 4. sccache 와 PCH 의 충돌 (Windows `/Yu` 는 캐시되지 않는다)
구성을 둘로 나눈다: **개발 증분**(PCH ON, 유니티 OFF)과 **콜드 · 워크트리 · CI**(PCH OFF + 유니티 + sccache 웜). 프리셋 이름은 `Ninja-Debug`(개발) · `Ninja-Debug-Cold`(콜드). 0 단계 표에서 세 구성(PCH · PCH 없이 sccache · 유니티)의 풀 빌드와 워크트리 콜드를 비교해 쓸 가치가 있는 조합만 남긴다.

### 5. 유니티 범위 확대
지금은 게임 모듈만 `UNITY_BATCH 8` 이다. 헤더 파싱이 57 % 이므로 Engine · GameFramework 킷에 4 단계의 콜드 구성에서만 켠다(증분에는 불리하다). 배치 크기는 4 · 8 · 16 을 재서 정한다. 리플렉션 등록기와 이름 충돌(익명 네임스페이스 · `static` 중복)은 유니티에서 터지므로 `sw_skipUnitySources` 의 예외 목록을 같이 본다.

### 6. 무거운 단일 TU — `EngineLoop.cpp` 남음
`EngineLoop.cpp`(6.5 초, 1.6 천 줄)는 헤더 정리와 함께 본다(Jolt 래퍼는 끝났다 — [검증과 측정](../08_Verification.md) "빌드 속도").

## 하지 않는 것 — 필요할 때만 (사용자 결정)
7. 헤더 영향 범위(팬아웃) 감시 보고서, 8. Debug 디버그 정보 줄이기(`-gline-tables-only` 계열), 9. 링크 단위 분할(엔진 분할 계획 1 단계 이후).

## 순서
0 은 다른 빌드가 없을 때. 구조 정리(Core 층 · 폴더 재배치)는 끝났으니 2 → 3 → 4 · 5 · 6 순서로 한다. 각 단계가 끝나면 0 단계 표를 다시 채워 전후를 남기고, 효과가 없으면 되돌린다(`docs/09_Decisions.md` 3절에 기각 사유와 숫자를 적는다).
