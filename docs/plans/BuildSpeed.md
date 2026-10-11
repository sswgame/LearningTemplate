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

### 2. 헤더 다이어트 — 끝(OS 헤더 · `StdHeaders.h`)
OS 헤더(`Windows.h` 묶음)를 `EngineMinimal.h` 에서 빼고, `StdHeaders.h` 에서 몇 파일만 쓰는 무거운 표준 헤더(스트림 · `<random>` · `<regex>` · `<future>` · `<variant>` 등)를 뺐다.
PCH 없는 TU 넷의 구문 검사 합이 5.02 → 4.08 s(−19 %)다([검증과 측정](../08_Verification.md) "빌드 속도"). 헤더 파싱 비율 40 % 목표는 닿지 않았다(53 → 51 %) —
남은 헤더 시간의 약 30 % 는 UCRT `wchar.h` 가 무조건 읽는 clang `intrin.h`(TU 하나에 약 0.25 s)라 엔진 쪽에서 줄일 수 없고, 나머지 대부분은 모든 `.cpp` 가 첫 줄에 읽는 `pch.h` 자체다.


### 3. PCH 공유 — 끝(킷 전부가 `sw_kit_pch` 하나)
킷 약 60 개가 PCH 하나를 실제로 나눠 쓴다(`sw_useSharedPch`, 풀 빌드 135.8 → 115.2 s). 나머지 묶음(시험 9 · RHI 4 · App 3 · 에디터 확장 · `SWGame`)은
합쳐도 PCH 컴파일 약 15 개(개당 약 2.7 CPU 초, 16 스레드에서 2 ~ 3 s)라 측정 편차 안이어서 하지 않는다([결정 기록](../09_Decisions.md) 3절).

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
