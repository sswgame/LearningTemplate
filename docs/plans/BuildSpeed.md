# 빌드 속도 계획

빌드 속도 개선의 남은 단계입니다. 범위(1 ~ 6 단계까지 하고 7 ~ 9 는 필요할 때만)는 [결정 기록](../09_Decisions.md) 5-2 에 있습니다.

끝난 단계는 0 기준선, 2 헤더 다이어트(OS 헤더 · `StdHeaders.h`), 3 PCH 공유(킷 전부가 `sw_kit_pch` 하나), 4 콜드 프리셋(두지 않음)입니다.
측정값과 측정하는 법은 [검증과 측정](../08_Verification.md) "빌드 속도", 기각한 안은 [결정 기록](../09_Decisions.md) 3절에 있습니다.
단계마다 같은 표로 전후를 남기고, 효과가 없으면 되돌린 뒤 결정 기록 3절에 숫자와 함께 적습니다.

## 5. 유니티 범위 확대 — 측정 한 쌍뿐이라 되돌림

유니티(`SW_ENABLE_UNITY_BUILD`, Windows CI 구성)는 지금 Engine 12 · GameFramework 8 · Editor 8 · 게임 8 입니다. Core(`sw_setUnityBuild(Core_objects BATCH_SIZE 12)`)와
킷(`sw_addGameFrameworkKit` 에 `UNITY_BATCH 8`)을 더하면 Windows 에서 이름 충돌 없이 빌드됐고, PCH 끔 + 유니티 · 캐시 없음 풀 빌드가 139.9 → 115.0 s(한 쌍)였습니다.
두 번째 쌍과 배치 4 · 16 은 다른 쪽의 vcpkg 설치가 공유 설치 폴더를 흔들어(`TracyClient.dll` 없음) 측정하지 못해 커밋하지 않았습니다.

- 남은 일: 공유 설치가 조용할 때 교차 3 쌍 + 배치 4 · 8 · 16 을 재고, 리눅스 유니티(CI)에서 킷과 Core 의 이름 충돌을 확인한 뒤 넣습니다. 다른 빌드가 없을 때만 합니다.

## 6. 무거운 단일 TU — `Engine.cpp`(시작하지 않음)

`Engine.cpp`(6.5 초, 1.6 천 줄, include 101 개 — 엔진 구성 루트)는 풀 빌드에서는 다른 TU 와 나란히 돌아 벽시계에 거의 보이지 않습니다.
볼 것은 그 파일을 고친 뒤의 증분 시간입니다. 나누기 전후를 `-ftime-trace` 로 잽니다.
