# 빌드 속도 계획

> 상태: 구현대기 · 선행: 다른 작업이 모두 끝난 뒤 혼자 · 영역: quality · 옛번호: C-2 · 진행: 6/13

빌드 속도 개선의 남은 단계입니다. 범위는 1 ~ 6 단계까지 하고 7 ~ 9 는 필요할 때만 합니다. 끝난 단계: 0 기준선, 2 헤더 다이어트(OS 헤더 · `StdHeaders.h`), 3 PCH 공유(킷 전부가 `sw_kit_pch` 하나), 4 콜드 프리셋(두지 않음), 10 임포트 라이브러리 갱신(`.cpp` 하나 고친 증분 13.9 → 5 s), 11 PCH 에서 자주 바뀌는 헤더 빼기(핵심 헤더 증분 131 → 14 ~ 77 s, 풀 빌드 +15 %).

## 12. 리플렉션 헤더의 팬아웃

PCH 에서 핵심 헤더를 뺀 뒤에도 `ReflectionTypes.h`(1,280 줄, 3 개월 71 번 변경) · `TypeRegistry.h` · `ReflectionCore.h` 는 TU 1,150 ~ 1,250 개가 읽습니다(`REFLECT` 를 쓰는 모든 .cpp 와 코드젠 `.gen.cpp`). 기능 추가(`PROPERTY` 속성 키 등)가 한 번 들어갈 때마다 약 1,200 TU 입니다.

- 들어간 것(빌드 · 측정 전): 속성 키 표(`TypeMetadata` · `PropertyMetadata` · `FunctionMetadata`)를 `ReflectionMetadata.h` 로 갈랐다. `ReflectionTypes.h` 는 그 구조를 `ReflectMetadataBox`(복사 · 소멸을 만들 때 적은 함수 표로 정의 없이 값처럼 드는 상자)로만 들고 칸은 `->` 로 읽는다(`prop._metadata->_bTransient`). include 하는 곳은 칸을 읽는 .cpp 약 40 개, `SerializerUtil.h`, 생성 코드(`ReflectGenerated.h`)뿐이라 속성 키를 더할 때 다시 컴파일되는 TU 가 약 1,200 → 생성 TU + 약 80 으로 줄 것으로 본다.
- 둘째(2026-10-24): `Component.h` · `RHITypes.h` 가 우산 헤더 `ReflectionCore.h` 대신 `ReflectionMacros.h` + `ReflectionTypes.h` 만 include 한다(`REFLECT_BODY` 는 `generated::Registrar` 와 `TypeInfo` 만 쓴다). 그 헤더에 기대던 TU 는 빌드가 아니라 include 그래프 전수로 찾았고(`Source` · `Test` 의 로컬 include 를 풀어 전후 닫힘을 견줌) 걸린 `ObjectiveMarkerComponent.cpp`(`castTo` → `ReflectionCast.h`) · `TestObjectStateRoundTrip.cpp`(`TypeRegistry` → `TypeRegistry.h`)에 직접 include 를 더했다. 같은 그래프로 센 의존 TU: `TypeRegistry.h` 1,080 → 366, `ReflectValue.h` 934 → 185, `ReflectionValidation.h` 932 → 202, `ReflectionInvoke.h` 926 → 175. 전선언만 쓴 파일(인라인 본문에서 완전 타입이 필요한 경우)은 이 방법으로 못 잡아 빌드에서 나오면 직접 include 를 더한다. `GameObjectManager.h` 의 값 멤버 레지스트리를 `unique_ptr` 로 바꾸는 안은 같은 잣대(바뀐 횟수 × TU 수)로 하지 않기로 했다(2026-10-21 — 남은 레지스트리 셋은 Core 만 include 하며 3 개월간 1 ~ 3 번 바뀌었다).
- 확인(통합에서): 전체 빌드가 서는지(전수가 놓친 파일), 후보 `pch.h` 를 `-I` 맨 앞에 둔 `/clang:-M` 전처리로 `ReflectionTypes.h` · `ReflectionMetadata.h` · `TypeRegistry.h` 의 의존 TU 수를 전후로 견주고 `ReflectionMetadata.h` · `TypeRegistry.h` 를 건드린 증분 시간을 잰다. 상자가 더한 간접 참조 한 번이 직렬화 벤치 · 씬 로드 시간에 보이는지도 본다.

## 13. Shipping ThinLTO 링크 캐시 (U4)

Shipping 은 테스트 실행 파일 일곱과 App 이 링크마다 ThinLTO 를 처음부터 다시 돌립니다(`build/Ninja-Shipping/.ninja_log`: EngineTest 69 · 85 s, 나머지 37 ~ 58 s, Debug · Release 는 0.2 ~ 2.3 s). 같은 엔진 정적 라이브러리를 링크하는 파일이 여럿이고 `.cpp` 하나를 고친 증분에서도 나머지 모듈의 코드 생성을 다시 하기 때문입니다.

- 들어간 것(구성 문법만 읽고 검토, 구성 · 빌드 전): `cmake/Engine/BuildLayout.cmake` 의 IPO 블록이 `SW_ENABLE_LTO` 가 켜지고 clang 일 때 `sw_global_options` 에 링크 옵션을 더한다 — Windows `/lldltocache:<빌드>/LTOCache` · `/lldltocachepolicy:cache_size_bytes=8g:prune_after=168h`, 리눅스 `--thinlto-cache-dir=` · `--thinlto-cache-policy=`. 캐시 폴더 하나를 모든 링크가 같이 쓴다(lld 의 캐시 쓰기는 원자적). 폴더는 빌드 폴더 아래라 `.gitignore` 의 `/build/` 가 가리고, 빌드 폴더를 지우면 같이 사라진다. `ninja clean` 은 지우지 않는다.
- 링크 스레드: 링크 풀(`BuildOptions.cmake` 5, 코어 수 / 4 를 2 ~ 4 로 조임)이 가득 차면 ThinLTO 백엔드가 링크마다 코어 수만큼 스레드를 띄워 코어 수 × 풀 크기가 동시에 코드 생성을 한다. 그래서 링크 하나의 백엔드 스레드를 코어 수 / 풀 크기(최소 2)로 나눠 합이 코어 수가 되게 했다(`/opt:lldltojobs=` · `--thinlto-jobs=`). 마지막에 링크가 하나만 남은 꼬리에서는 스레드가 줄어 오히려 느릴 수 있어, 아래 세 판 가운데 첫 판(빈 캐시)의 `.ninja_log` 에서 가장 오래 걸린 링크를 본다.
- 효과는 추정이다(캐시가 맞는 모듈 비율을 아직 모른다). 측정이 나오기 전에는 얼마나 줄지 적지 않는다.
- 측정 절차(통합, Shipping 프리셋, 빌드 슬롯 독점): ① 빈 캐시 전체 — `build/Ninja-Shipping/LTOCache` 폴더를 지우고 `ninja -t clean` 뒤 전체 빌드(`ninja clean` 은 캐시를 지우지 않는다. `RunBuildBaseline full` 은 `clean` 뒤 `cmake/Config/BuildCaches.json` 의 `cold_clear` 목록 — 링크 캐시 · `Cooked` · `Bin/Packs` · `Saved/CookRecord` · `Saved/DDC` — 을 지워 콜드로 만든다. 지우지 않는 것(vcpkg · `Tools/_cache` 링크 폴더, 커밋된 셰이더 쿠킹 산출물)과 이유는 같은 파일의 `keep` 에 있고, 컴파일러 캐시는 쓰지 않는다). ② 캐시 찬 전체 — 캐시를 두고 `ninja -t clean` 뒤 다시 전체 빌드. ③ 증분 — 엔진 `.cpp` 하나를 고치고 다시 빌드. 판마다 `.ninja_log` 의 링크 항목(EngineTest · 나머지 테스트 · App)별 시간, 벽시계, 링크가 도는 동안 최대 메모리 사용량을 적고 위의 기준선(69 · 85 s, 37 ~ 58 s)과 견준다. 구성 직후 `ninja -t commands <실행 파일>` 에 캐시 옵션이 실제로 붙는지, 캐시 폴더 크기가 8 GB 안에 드는지도 본다.

## 5. 유니티 범위 확대 — 측정 한 쌍뿐이라 되돌림

유니티(`SW_ENABLE_UNITY_BUILD`)는 지금 Engine 12 · GameFramework 8 · Editor 8 · 게임 8 입니다. Core(`sw_setUnityBuild(Core_objects BATCH_SIZE 12)`)와 킷(`sw_addGameFrameworkKit` 에 `UNITY_BATCH 8`)을 더하면 Windows 에서 이름 충돌 없이 빌드됐고 PCH 끔 + 유니티 · 캐시 없음 풀 빌드가 139.9 → 115.0 s(한 쌍)였습니다. 두 번째 쌍과 배치 4 · 16 은 다른 쪽의 vcpkg 설치가 공유 설치 폴더를 흔들어(`TracyClient.dll` 없음) 측정하지 못해 커밋하지 않았습니다.

- 들어간 것(2026-10-24): 묶음 크기를 캐시 변수로 뺐다 — `SW_UNITY_BATCH_CORE`(`Core_objects`) · `SW_UNITY_BATCH_KIT`(`sw_addGameFrameworkKit`), 기본 0 은 묶지 않음, `SW_ENABLE_UNITY_BUILD` 가 켜졌을 때만 듣는다. 숫자를 정하면 변수를 지우고 호출부에 숫자를 쓴다.
- 남은 일(측정, 혼자): 공유 설치가 조용할 때 `cmake --preset Ninja-Debug -DSW_ENABLE_UNITY_BUILD=ON -DSW_UNITY_BATCH_CORE=<4|8|12|16> -DSW_UNITY_BATCH_KIT=<4|8|16>` 로 교차 3 쌍(0 대 값, PCH 끔 + 캐시 없음 풀 빌드)을 잰다. 이긴 값을 넣는다.

## 6. 무거운 단일 TU — `Engine.cpp`(하지 않는다, 2026-10-24 정함)

`Engine.cpp`(6.5 초, 1.6 천 줄, include 101 개 — 엔진 구성 루트)는 풀 빌드에서는 다른 TU 와 나란히 돌아 벽시계에 거의 보이지 않고 고친 뒤 증분 비용은 그 6.5 초 하나다. 나누면 얻는 것은 많아야 몇 초이고 엔진 시작 순서(`Engine::initialize` · `update` 는 순서가 곧 의미라 [LongFunctions](LongFunctions.md) 가 유지로 판정)를 여러 파일에 흩는 것이라 하지 않는다. 그 파일을 고치는 일이 잦아져 증분 시간이 문제로 보이면 `-ftime-trace` 로 먼저 잰다.
