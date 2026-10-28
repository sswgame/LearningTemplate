# 빌드 속도 계획

> 상태: 구현대기 · 선행: 다른 작업이 모두 끝난 뒤 혼자 · 영역: quality · 진행: 6/13

빌드 시간을 줄이는 남은 단계다. 범위는 1 ~ 6 단계이고 7 ~ 9 는 필요할 때만 한다. 12 · 13 은 코드가 들어갔으나 **빌드 · 측정 전**이다.

| 단계 | 내용 | 상태 |
|---|---|---|
| 0 · 2 · 3 · 4 · 10 · 11 | 기준선, 헤더 다이어트, PCH 공유(킷 전부 `sw_kit_pch` 하나), 콜드 프리셋(두지 않음), 임포트 라이브러리 갱신, 자주 바뀌는 헤더를 PCH 에서 빼기 | 끝 |
| 5 | 유니티 빌드 범위 확대 | 묶음 크기 변수만 들어감, 측정 남음 |
| 6 | `Engine.cpp` 분할 | 하지 않기로 함 |
| 12 | 리플렉션 헤더의 팬아웃 | 코드 끝남, 빌드 확인 전 |
| 13 | Shipping ThinLTO 링크 캐시 | 코드 끝남, 구성 · 빌드 전 |

## 12. 리플렉션 헤더의 팬아웃

`ReflectionTypes.h`(1,280 줄) · `TypeRegistry.h` · `ReflectionCore.h` 를 TU(번역 단위, .cpp 하나)의 약 1,200 개가 읽는다. 속성 키 같은 기능을 더할 때마다 약 1,200 TU 가 다시 컴파일되는 것이 문제다.

- 속성 키 표(`TypeMetadata` · `PropertyMetadata` · `FunctionMetadata`)를 `ReflectionMetadata.h` 로 갈랐다. `ReflectionTypes.h` 는 `ReflectMetadataBox`(정의 없이 값처럼 드는 상자)로만 들고 칸은 `->` 로 읽는다. include 하는 곳은 칸을 읽는 .cpp 약 40 개 · `SerializerUtil.h` · 생성 코드뿐이라 다시 컴파일되는 TU 가 약 1,200 → 생성 TU + 약 80 으로 줄 것으로 본다.
- `Component.h` · `RHITypes.h` 는 우산 헤더 `ReflectionCore.h` 대신 `ReflectionMacros.h` + `ReflectionTypes.h` 만 include 한다. include 그래프 전수로 의존 TU 가 `TypeRegistry.h` 1,080 → 366, `ReflectValue.h` 934 → 185 로 줄었다. 전선언만 쓴 파일은 이 방법으로 못 잡으니 빌드에서 나오면 직접 include 를 더한다.
- 기각: `GameObjectManager.h` 의 값 멤버 레지스트리를 `unique_ptr` 로 바꾸는 안. 남은 레지스트리 셋은 Core 만 include 하고 3 개월간 1 ~ 3 번 바뀌어 이득이 작다.
- 남은 일(첫 일): 전체 빌드가 서는지 확인하고, `ReflectionMetadata.h` · `TypeRegistry.h` 를 건드린 증분 시간과 직렬화 벤치 · 씬 로드 시간(상자의 간접 참조 비용)을 잰다.

## 13. Shipping ThinLTO 링크 캐시

Shipping 은 테스트 실행 파일 일곱과 App 이 링크마다 ThinLTO(링크 시점 최적화)를 처음부터 다시 돌려 링크가 EngineTest 69 · 85 s, 나머지 37 ~ 58 s 걸린다(Debug · Release 는 0.2 ~ 2.3 s).

- `cmake/Engine/BuildLayout.cmake` 의 IPO 블록이 `SW_ENABLE_LTO` + clang 일 때 링크 옵션으로 캐시 폴더를 더한다(Windows `/lldltocache:`, 리눅스 `--thinlto-cache-dir=`, 한도 8 GB · 168 h). 폴더 하나를 모든 링크가 같이 쓰고(lld 의 캐시 쓰기는 원자적) 빌드 폴더 아래라 빌드 폴더를 지우면 같이 사라진다.
- 링크 풀이 가득 찰 때 코어 수 × 풀 크기만큼 스레드가 도는 것을 막으려고 링크 하나의 백엔드 스레드를 코어 수 / 풀 크기(최소 2)로 나눴다. 마지막 링크 하나만 남은 꼬리에서는 오히려 느릴 수 있어 빈 캐시 판의 가장 오래 걸린 링크를 본다.
- 효과는 추정이다. 측정 전에는 얼마나 줄지 적지 않는다.
- 남은 일(첫 일, Shipping 프리셋, 혼자): 세 판을 잰다. ① 빈 캐시 전체 ② 캐시 찬 전체 ③ 엔진 `.cpp` 하나 고친 증분. 판마다 링크 항목별 시간 · 벽시계 · 최대 메모리를 위 기준선과 견주고, 캐시 옵션이 실제로 링크 명령에 붙는지 · 캐시 크기가 8 GB 안인지 본다.

## 5. 유니티 범위 확대

유니티 빌드(`SW_ENABLE_UNITY_BUILD`, .cpp 여럿을 한 TU 로 묶음)는 지금 Engine 12 · GameFramework 8 · Editor 8 · 게임 8 이다. Core 와 킷까지 묶으면 이름 충돌 없이 빌드됐고 PCH 끔 풀 빌드가 139.9 → 115.0 s 였으나 한 쌍뿐이라 되돌렸다.

- 묶음 크기를 캐시 변수 `SW_UNITY_BATCH_CORE` · `SW_UNITY_BATCH_KIT` 로 뺐다(기본 0 은 묶지 않음). 값을 정하면 변수를 지우고 호출부에 숫자를 쓴다.
- 남은 일: 공유 설치가 조용할 때 `-DSW_UNITY_BATCH_CORE=<4|8|12|16> -DSW_UNITY_BATCH_KIT=<4|8|16>` 교차 3 쌍(PCH 끔 + 캐시 없음 풀 빌드)을 재서 이긴 값을 넣는다.

## 6. `Engine.cpp` 분할 — 하지 않는다

`Engine.cpp`(6.5 초, 엔진 구성 루트)는 풀 빌드에서 다른 TU 와 나란히 돌아 벽시계에 안 보이고, 나눠도 얻는 것은 몇 초다. 시작 순서가 곧 의미인 코드라([LongFunctions](LongFunctions.md) 가 유지로 판정) 여러 파일에 흩지 않는다. 증분 시간이 문제로 보이면 `-ftime-trace` 로 먼저 잰다.
