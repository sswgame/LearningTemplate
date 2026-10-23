# 빌드 속도 계획

> 상태: 구현대기 · 선행: 다른 작업이 모두 끝난 뒤 혼자 · 영역: quality · 옛번호: C-2 · 진행: 6/13

빌드 속도 개선의 남은 단계입니다. 범위(1 ~ 6 단계까지 하고 7 ~ 9 는 필요할 때만)는 [결정 기록](../09_Decisions.md) 5-2 에 있습니다.

끝난 단계는 0 기준선, 2 헤더 다이어트(OS 헤더 · `StdHeaders.h`), 3 PCH 공유(킷 전부가 `sw_kit_pch` 하나), 4 콜드 프리셋(두지 않음),
10 임포트 라이브러리 갱신(`.cpp` 하나 고친 증분 13.9 → 5 s), 11 PCH 에서 자주 바뀌는 헤더 빼기(핵심 헤더 증분 131 → 14 ~ 77 s, 풀 빌드 +15 %)입니다.
측정값과 측정하는 법은 [검증과 측정](../08_Verification.md) "빌드 속도", 기각한 안은 [결정 기록](../09_Decisions.md) 3절에 있습니다.
단계마다 같은 표로 전후를 남기고, 효과가 없으면 되돌린 뒤 결정 기록 3절에 숫자와 함께 적습니다.

## 12. 리플렉션 헤더의 팬아웃

PCH 에서 핵심 헤더를 뺀 뒤에도 `ReflectionTypes.h`(1,280 줄, 3 개월 71 번 변경) · `TypeRegistry.h` · `ReflectionCore.h` 는 TU 1,150 ~ 1,250 개가 읽습니다. `REFLECT` 매크로를 쓰는 모든 .cpp 와
코드젠 `.gen.cpp` 가 필요로 해서입니다. 바뀐 내용 대부분은 이름 · 주석 일괄 치환이지만, 기능 추가(`PROPERTY` 속성 키 등)가 한 번 들어갈 때마다 약 1,200 TU 입니다.

- 들어간 것(빌드 · 측정 전): 속성 키 표(`TypeMetadata` · `PropertyMetadata` · `FunctionMetadata`)를 `ReflectionMetadata.h` 로 갈랐다. `ReflectionTypes.h` 는 그 구조를
  `ReflectMetadataBox`(복사 · 소멸을 만들 때 적은 함수 표로 해서 정의 없이 값처럼 드는 상자)로만 들고, 칸은 `->` 로 읽는다(`prop._metadata->_bTransient`).
  include 하는 곳은 칸을 읽는 .cpp 약 40 개, `SerializerUtil.h`, 생성 코드(`ReflectGenerated.h`)뿐이다. 속성 키를 더할 때 다시 컴파일되는 TU 가 약 1,200 → 생성 TU + 약 80 으로 줄 것으로 본다.
- 들어간 것 둘째(2026-10-24, 빌드 · 측정 전): `Component.h` · `RHITypes.h` 가 우산 헤더 `ReflectionCore.h` 대신 `ReflectionMacros.h` + `ReflectionTypes.h` 만 include 한다
  (`REFLECT_BODY` 는 `generated::Registrar` 와 `TypeInfo` 만 쓴다). 그 헤더에 기대던 TU 는 빌드가 아니라 include 그래프 전수로 찾았다 — `Source` · `Test` 의 로컬 include 를 풀어
  전후 닫힘을 견주고(PCH 로 닿는 헤더 제외), 잃은 헤더의 선언 이름을 쓰는 파일을 골랐다. 걸린 것은 `ObjectiveMarkerComponent.cpp`(`castTo` → `ReflectionCast.h`) ·
  `TestObjectStateRoundTrip.cpp`(`TypeRegistry` → `TypeRegistry.h`) 둘뿐이라 직접 include 를 더했다. 같은 그래프로 센 의존 TU: `TypeRegistry.h` 1,080 → 366 ·
  `ReflectValue.h` 934 → 185 · `ReflectionValidation.h` 932 → 202 · `ReflectionInvoke.h` 926 → 175 (`ReflectionCast.h` 는 `GameObject.h` · `WidgetTree.h` 가 직접 읽어 1,102 → 874).
  전선언만 쓴 파일(`class X;` 를 직접 선언하고 인라인 본문에서 완전 타입이 필요한 경우)은 이 방법으로 못 잡는다 — 빌드에서 나오면 그 파일에 직접 include 를 더한다.
  `GameObjectManager.h` 의 값 멤버 레지스트리를 `unique_ptr` 로 바꾸는 안은 같은 잣대(바뀐 횟수 × TU 수)로 하지 않기로 했다(2026-10-21). `AnimationSystem` 은 이미 씬 서비스로 나갔고,
  남은 레지스트리 셋은 Core 만 include 하며 3 개월 동안 1 ~ 3 번 바뀌어 줄일 것이 없다. 이유는 그 멤버 주석에 있다.
- 확인(통합에서): 전체 빌드가 서는지(위 전수가 놓친 파일), 후보 `pch.h` 를 `-I` 맨 앞에 둔 `/clang:-M` 전처리로 `ReflectionTypes.h` · `ReflectionMetadata.h` · `TypeRegistry.h` 의 의존 TU 수를 전후로 견주고(예상은 위 숫자),
  `ReflectionMetadata.h` · `TypeRegistry.h` 를 건드린 증분 시간을 잰다.
  상자가 더한 간접 참조 한 번이 직렬화 벤치 · 씬 로드 시간에 보이는지도 본다.

## 5. 유니티 범위 확대 — 측정 한 쌍뿐이라 되돌림

유니티(`SW_ENABLE_UNITY_BUILD`, Windows CI 구성)는 지금 Engine 12 · GameFramework 8 · Editor 8 · 게임 8 입니다. Core(`sw_setUnityBuild(Core_objects BATCH_SIZE 12)`)와
킷(`sw_addGameFrameworkKit` 에 `UNITY_BATCH 8`)을 더하면 Windows 에서 이름 충돌 없이 빌드됐고, PCH 끔 + 유니티 · 캐시 없음 풀 빌드가 139.9 → 115.0 s(한 쌍)였습니다.
두 번째 쌍과 배치 4 · 16 은 다른 쪽의 vcpkg 설치가 공유 설치 폴더를 흔들어(`TracyClient.dll` 없음) 측정하지 못해 커밋하지 않았습니다.

- 들어간 것(2026-10-24): 측정에 코드를 고치지 않도록 묶음 크기를 캐시 변수로 뺐다 — `SW_UNITY_BATCH_CORE`(`Core_objects`) · `SW_UNITY_BATCH_KIT`(`sw_addGameFrameworkKit`), 기본 0 은 묶지 않음(지금 동작 그대로),
  `SW_ENABLE_UNITY_BUILD` 가 켜졌을 때만 듣는다. 숫자를 정하면 변수를 지우고 호출부에 숫자를 쓴다.
- 남은 일(측정, 혼자): 공유 설치가 조용할 때 `cmake --preset CI-Debug -DSW_UNITY_BATCH_CORE=<4|8|12|16> -DSW_UNITY_BATCH_KIT=<4|8|16>` 로 교차 3 쌍(0 대 값, PCH 끔 + 캐시 없음 풀 빌드)을 잰다.
  리눅스 유니티(CI)에서 킷과 Core 의 이름 충돌 확인은 `Linux` CI 프리셋에 같은 `-D` 를 줘 한 번 짓는 것으로 한다. 이긴 값을 넣는다.

## 6. 무거운 단일 TU — `Engine.cpp`(하지 않는다, 2026-10-24 정함)

`Engine.cpp`(6.5 초, 1.6 천 줄, include 101 개 — 엔진 구성 루트)는 풀 빌드에서는 다른 TU 와 나란히 돌아 벽시계에 거의 보이지 않고, 고친 뒤의 증분 비용은 그 6.5 초 하나다.
나누면 얻는 것은 많아야 몇 초이고, 나누는 쪽이 엔진 시작 순서(`Engine::initialize` 250 줄 · `update` 160 줄은 순서가 곧 의미라 [LongFunctions](LongFunctions.md) 가 유지로 판정했다)를 여러 파일에 흩는 것이라 하지 않는다.
그 파일을 고치는 일이 잦아져 증분 시간이 문제로 보이면 그때 `-ftime-trace` 로 먼저 잰다.
