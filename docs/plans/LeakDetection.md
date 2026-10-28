# 누수 탐지

> 상태: 구현중 · 선행: 없음 · 영역: engine · 진행: 표

"기준선 뒤의 살아 있는 양 차이" 로 재던 누수 보고를, "창 안에서 태어나 창이 닫힐 때 아직 살아 있는 블록" 을 세는 측정 창으로 바꾸는 계획이다. 창 밖에서 태어난 블록을 풀어도 새 누수가 가려지지 않게 하는 것이 목적이다. L1 은 끝났고 L3 · L4 는 코드가 일부 들어갔다(**빌드 · 테스트 안 돌림**).

| 단계 | 내용 | 상태 |
|---|---|---|
| L1 | 측정 창: 블록 헤더의 `_windowStamp`, `MemoryProfiler::beginLeakWindow` · `beginLeakRound` · `endLeakWindow` · `printLeakWindowReport`, 기준선 배열을 창으로 합침 | 끝 |
| L3 | 반복 로드 곡선 검사(워밍업 1 + 측정 8 회, 기울기 > 0 이면 실패). 판정 `judgeLeakCurve` 와 흉내 낸 로드 테스트는 있고 기울기 허용치(256 B · 1 블록 / 회차)는 잠정 | 코드 끝남(빌드 · 테스트 전), 진짜 씬 로드 연결 남음 |
| L4 | 보고 연결: 종료 보고 · 에디터 프로파일러 패널이 `FullStacks` 보고서를 보인다 | Core 끝남, 엔진 `gv_*` 연결 · 패널 남음 |

## 설계 결정

- **누수는 블록의 출생으로 잰다.** 창 안에서 태어난 블록에 회차 스탬프를 찍고 창이 닫힐 때 살아 있는 것만 보고한다. 기존 방식은 `Scene` 에서 100 B 를 풀고 50 B 가 샌 경우 차이가 -50 이라 보고되지 않았다. 원인이 "출생 시점을 기록하지 않는 구조"라 헤더에 스탬프를 남기는 구조로 푼다.
- **스탬프는 `Memory` 가 나른다.** `AllocationRecord::_windowStamp`(uint32)를 블록 헤더 패딩에 넣어 48 B 를 유지하고 해제 때 `recordFree` 로 돌려준다. `Memory`(티어 2)는 스탬프의 뜻을 모르고 창은 `MemoryProfiler`(티어 10)가 맡는다.
- **창은 하나다.** 태그별 기준선 배열을 지웠다. `captureMemoryLeakBaseline` 이 창을 열고 종료 끝(`DiagnosticsSubsystem`)이 닫아 찍는다. 두 번째 경로는 없다.
- **기록 수준은 연 쪽이 고른다(기본값 없음).** `TagTotals` 는 태그별 바이트 · 개수만 세고, `FullStacks` 는 창 안 모든 할당의 스택을 블록마다 기록한다. 긴 기준선 창까지 전수 캡처하면 Debug 가 느려져 기준선 창만 `TagTotals` 로 둔다.
- **`FullStacks` 테이블은 블록 주소로 나눈 슬롯이다.** 해제 스레드가 할당 스레드와 다를 수 있어 스레드 슬롯으로는 지울 슬롯을 모른다.
- **오용은 오류다.** 창 없이 `endLeakWindow`, 열린 채 `beginLeakWindow` 는 단언이다.
- **다른 스레드의 할당이 섞이는 한계는 숨기지 않는다.** `_foreignThreadBlockCount` · `_foreignThreadBytes` 가 따로 알린다(`FullStacks` 만).
- **회차**: `beginLeakRound()` 로 넘기면 보고 줄에 태어난 회차가 남아 씬 로드를 되풀이할 때 어느 회차의 블록이 남았나를 한 창으로 본다.
- 태그만으로는 누수 코드를 못 찾으므로 첫 프레임(Core/Memory · Core/Container 는 거름)을 "누가 할당했나"로 쓴다. 심볼 변환은 느려서 창이 닫힐 때 스택마다 한 번만 한다.

## 별도 단위

- **소유 형식 강화**(이 작업 범위 밖): 원시 `sw_new`(25곳)와 `Memory::allocate`(31곳)를 `make_unique` · 컨테이너로 바꿔 누수 모양을 줄인다. 창은 그 뒤에도 남는다(컨테이너 증가 · 캐시 누적을 잡는다).

## 읽을 곳 · 추정

- 코드: `Source/Core/Memory/Memory.{h,cpp}` · `Source/Core/Diagnostics/MemoryProfiler.{h,cpp}` · `Source/Engine/Bootstrap/DiagnosticsSubsystem.cpp` · `Test/CoreTest/Memory/TestMemoryProfiler.cpp`. 층은 `Scripts/lint/rules/CheckCoreLayers.toml` 의 Memory 2 · Diagnostics 10.
- 추측: 첫 프레임 거르기는 경로 · 심볼 이름 부분 문자열이라 PDB 줄 정보가 없는 구성(Linux)에서는 컨테이너 프레임이 안 걸러질 수 있다. 스탬프 32 비트는 회차가 수십 개라 돌아갈 일이 없다고 본다.
- 남은 블록의 출처를 찾을 때는 기준선 창을 잠시 `FullStacks` 로 바꿔 본다.
