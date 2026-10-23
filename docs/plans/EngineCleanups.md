# 엔진 정리 묶음 (2026-10-24)

> 상태: 구현중 · 선행: 없음 · 영역: engine · 옛번호: C-49, C-63, C-64, C-47, C-34 · 진행: 0/7

`Candidates-engine.md` 의 정리 후보 중 설계 판단이나 측정이 먼저인 것을 단위로 나눠 옮겼습니다. 단위마다 첫 걸음이 작아서 순서대로 하나씩 합니다. 이름은 옛 후보 절의 이름입니다.

## 1. 단위

| 단위 | 내용 | 선행 |
|---|---|---|
| StableAddressMap-1 | `sw::unordered_map` 의 `&it->second` 를 오래 쥐는 호출부를 전수 조사한다. 확인된 위험은 `LiveReloadManager.cpp` 의 연쇄 리로드(`ModuleContext*` 를 쥔 채 `_mapModule` 에 삽입이 들어오면 매달림). `find` 가 `&it->second` 를 돌려주는 `AbilityCatalog` · `CultureInfo` · `ReflectionTypes` 등은 결과를 오래 쥐는 자리만 가린다. | 없음 |
| StableAddressMap-2 | 값 주소가 고정된 맵 형식(값을 `unique_ptr` 로 두고 `&value` 는 이 형식만 내줌)을 두고, 1 에서 가린 자리만 그 형식으로 옮긴다. 값 주소가 옮겨지는 맵은 주소를 내주는 API 를 없애거나 이름으로 드러낸다. | StableAddressMap-1 |
| VectorAliasLoops | `sw::vector` 의 `_list[i]` 쓰기 루프가 벡터 내부를 가려(별칭) 벡터화되지 않는다(`DynamicBitset` 은 원시 포인터 루프로 고침). 다른 컨테이너의 블록 루프와 `sw::vector::operator==` 를 Release 로 재 보고 이득이 있는 곳만 바꾼다. | 없음 |
| StlContainerConfig | `SW_ENABLE_STL_CONTAINER` 를 할당자만 바꾸게 줄인다. ⓐ STL 구성의 목적(표준 컨테이너 비교 · 디버그 STL 검사인가, `std::allocator` 로 메모리 추적을 빼는 것인가)을 사용자와 정한다. ⓑ 목적이 할당자뿐이면 자체 `vector` · `array` · `map` · `set` · `pair` 를 두 구성에 늘 쓰고 `string` · `list` · `deque` 는 `std::*<T, 구성별 할당자>` 하나로 둔다. ⓒ STL 구성에서만 건너뛰는 테스트(`TestVector` · `TestDataStructure` · `TestMemory` · `TestMemoryTag`)를 다시 본다. 함정: `vector` · `array` · `string` · `list` · `deque` · `map` · `set` · `pair` · `span` 이 모두 구성에 따라 std 별칭과 자체 형식 사이를 오가고, `string` · `list` · `deque` 는 자체 형식도 std 를 상속한다. `map` · `set` 은 두 구성 모두 키 순서지만 주소 · 반복자 보장은 기본 구성 쪽이 약하다. | ⓐ 결정 |
| ReadNameAudit | `readName` 호출부 약 375 곳을 전수로 보고, 필수 이름이 비었을 때 조용히 지나가는 자리를 찾는다. 이름을 경로 글로 바꿔 쓰는 `.c_str()` (`hashed_string` 의 FName 규칙: none = 빈 이름, 빈 이름의 `c_str()` = "None")는 형식으로 막지 못해 이름 읽기 창구에서 글 none 만 막은 상태다. 찾은 자리는 오류로 멈추게 한다. | 없음 |
| EditorCommandIdForm | 시나리오 `<DevCommand line="editor <명령>">` 의 에디터 명령 id 를 정적으로 보려면 `SW_DEV_COMMAND` 처럼 선언으로 등록하는 형식이 먼저다(`EditorCommandRegistry` 는 지금 실행 중 등록). 형식을 정하고 린트 `CheckNameReferences` 가 그 선언을 읽게 한다. 남은 이름 참조(입력 액션 기본값, 팩 둘 말고 나머지 카탈로그 안 id)는 각각 게임별 맵 · 에셋 검증 규칙 쪽 일이라 이 단위에서 같이 본다. | C++ 구조 결정 |
| AnimationUpdateAnomalies | 합성 리그 벤치(`SyntheticRigBenchTest`, [검증과 측정](../08_Verification.md) "영역별 측정값")가 드러낸 셋. (1) 캐릭터 1000 에서 본 150 → 300 이 2.5 배로 선형(2 배)보다 큼 — 캐시 · 워커 분배. (2) 본 300 + 리그 노드의 p99 40.6 ms 가 p50 의 3 배 — 히치 원인. (3) 네 발 60 본이 실행마다 흔들림(캐릭터 1000 p50 1223 ~ 3162 us). GPU 스키닝 시간은 런타임 .mesh 가 인덱스 없는 삼각형 목록이라 벤치 씬에 싣지 못해 재지 않았다. | Release 측정 |

## 2. 끝난 것

- `ConfigManager::isWithinRange` 를 지웠습니다(2026-10-24, 호출부 없는 죽은 코드였고 범위 검사는 `readConfigXMLElement` 와 `ConfigRangeUtil` 이 한 곳 `collectRangeErrors` 로 본다).
