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
- 남은 일: 검증(`ReflectionValidation.h`)과 값 변환(`ReflectValue.h`)은 `ReflectionCore.h` 묶음을 거쳐 `Component.h` · `RHITypes.h` 가 읽는다. 두 헤더가 묶음 대신
  쓰는 것만 include 하게 바꾸는 것이 다음 후보다(그 헤더에 기대던 TU 를 빌드로 찾아야 해서 빌드할 수 있을 때 한다).
  `GameObjectManager.h`(3 개월 107 번, TU 244 개)의 값 멤버 서브시스템 넷을 `unique_ptr` 로 바꾸는 안(후보 `HeaderIncludeSlimming` ②)도 같은 잣대(바뀐 횟수 × TU 수)로 다시 본다.
- 확인: 후보 `pch.h` 를 `-I` 맨 앞에 둔 `/clang:-M` 전처리로 `ReflectionTypes.h` · `ReflectionMetadata.h` 의 의존 TU 수를 전후로 견주고, `ReflectionMetadata.h` 를 건드린 증분 시간을 잰다.
  상자가 더한 간접 참조 한 번이 직렬화 벤치 · 씬 로드 시간에 보이는지도 본다.

## 5. 유니티 범위 확대 — 측정 한 쌍뿐이라 되돌림

유니티(`SW_ENABLE_UNITY_BUILD`, Windows CI 구성)는 지금 Engine 12 · GameFramework 8 · Editor 8 · 게임 8 입니다. Core(`sw_setUnityBuild(Core_objects BATCH_SIZE 12)`)와
킷(`sw_addGameFrameworkKit` 에 `UNITY_BATCH 8`)을 더하면 Windows 에서 이름 충돌 없이 빌드됐고, PCH 끔 + 유니티 · 캐시 없음 풀 빌드가 139.9 → 115.0 s(한 쌍)였습니다.
두 번째 쌍과 배치 4 · 16 은 다른 쪽의 vcpkg 설치가 공유 설치 폴더를 흔들어(`TracyClient.dll` 없음) 측정하지 못해 커밋하지 않았습니다.

- 남은 일: 공유 설치가 조용할 때 교차 3 쌍 + 배치 4 · 8 · 16 을 재고, 리눅스 유니티(CI)에서 킷과 Core 의 이름 충돌을 확인한 뒤 넣습니다. 다른 빌드가 없을 때만 합니다.

## 6. 무거운 단일 TU — `Engine.cpp`(시작하지 않음)

`Engine.cpp`(6.5 초, 1.6 천 줄, include 101 개 — 엔진 구성 루트)는 풀 빌드에서는 다른 TU 와 나란히 돌아 벽시계에 거의 보이지 않습니다.
볼 것은 그 파일을 고친 뒤의 증분 시간입니다. 나누기 전후를 `-ftime-trace` 로 잽니다.
