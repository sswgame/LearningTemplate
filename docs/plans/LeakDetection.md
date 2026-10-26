# 누수 탐지

> 상태: 구현중 · 선행: 없음 · 영역: engine · 옛번호: 없음 · 진행: 표

2026-10-24 설계 검토(design-reviewer) 결론: **"기준선 뒤의 살아 있는 양 차이" 로 재던 누수 보고를 "창 안에서 태어나 창이 닫힐 때 아직 살아 있는 블록" 을 세는 측정 창으로 바꾼다.** 결정은 1절, 근거는 2절, 측정으로 가릴 질문은 4절, 추정은 6절.

## 1. 결정

- **누수는 블록의 출생으로 잰다.** 창 안에서 태어난 블록에 스탬프(회차 번호)를 찍고 창이 닫힐 때 아직 살아 있는 블록만 보고한다. 창 밖에서 태어난 오래된 블록의 해제는 창의 값을 줄이지 않는다.
- **스탬프는 `Memory` 가 나른다.** `AllocationRecord::_windowStamp`(uint32)를 블록 헤더에 적었다가 해제 때 `recordFree` 로 돌려준다(헤더 패딩 `_arrPad[6]` 를 `uint8[2] + uint32` 로 나눠 48 B 와 `static_assert` 유지). `Memory`(티어 2)는 스탬프의 뜻을 모르고 창은 `MemoryProfiler`(Diagnostics, 티어 10)가 맡는다.
- **창 하나로 합친다.** 기존 태그별 기준선 배열은 지웠다. `captureMemoryLeakBaseline` 이 `TagTotals` 창을 열고 종료 끝(`DiagnosticsSubsystem`)이 `endLeakWindow` 로 닫아 `printLeakWindowReport` 로 찍는다. 두 번째 경로는 없다.
- **창의 기록 수준은 연 쪽이 고른다(기본값 없음).** `TagTotals` 는 태그별 바이트 · 개수만 센다(할당마다 원자 덧셈 둘). `FullStacks` 는 창 안의 모든 할당의 스택을 잡고 블록마다 기록한다. 설계 검토는 "창 안은 전수 스택 캡처" 였지만 기동부터 종료까지의 긴 창(기준선)까지 전수 캡처하면 Debug 실행이 느려져 기준선 창만 `TagTotals` 로 둔다(4절 측정으로 확인).
- **`FullStacks` 의 테이블은 블록 주소로 나눈 슬롯이다**(`ShardArray<LeakWindowShard>`, 결정 기록 5-3 메모리 절). 해제하는 스레드가 할당한 스레드와 다를 수 있어 스레드 슬롯으로는 지울 슬롯을 모른다.
- **창 없이 `endLeakWindow` 를 부르면 오류다**(단언 + `_bValid` false 인 빈 보고서). `beginLeakWindow` 를 열린 채 다시 부르는 것도 단언이다.
- **다른 스레드의 할당이 섞이는 한계는 숨기지 않는다.** 창은 프로세스 전체의 할당을 세고 `_foreignThreadBlockCount` · `_foreignThreadBytes` 가 창을 연 스레드가 아닌 스레드의 몫을 따로 알린다(`FullStacks` 만).
- **회차**: 한 창 안에서 `beginLeakRound()` 로 회차를 넘기면 보고서 줄에 태어난 회차(`_bornRound`)가 남는다(`FullStacks` 만, 씬 로드를 되풀이할 때 어느 회차의 블록이 남았나를 한 창으로 본다).

## 2. 근거

- 기존 기준선은 태그별 살아 있는 양의 차이라 창 밖에서 태어난 블록을 풀면 차이가 줄어 새 누수를 가린다(`Scene` 에서 100 B 해제, 50 B 누수 → 차이 -50 이라 보고되지 않음). 근본 원인은 블록이 어느 시점에 태어났는지를 기록하지 않는 데이터 구조라 블록 헤더에 출생 스탬프를 남기는 구조로 푼다(헤더 패딩 6 B 라 48 B 그대로, 해제마다 비교 하나만 는다).
- 태그만으로는 누수를 낸 코드를 찾을 수 없다. 첫 프레임(Core/Memory · Core/Container 프레임은 거르고)이 "누가 할당했나" 를 알려 준다. 심볼 변환은 전역 잠금을 잡는 느린 길이라 창이 닫힐 때 스택마다 한 번만 한다.

## 3. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| L1 | 측정 창: `AllocationRecord::_windowStamp` · 헤더 패딩 분할 · `recordFree` 가 `AllocationRecord` 를 받음, `MemoryProfiler::beginLeakWindow` · `beginLeakRound` · `endLeakWindow` · `printLeakWindowReport`, 기준선 배열을 창으로 합침, `TestMemoryProfiler` 창 테스트 다섯 개 | 구현함, 빌드 · 테스트 대기 |
| L2 | 측정으로 질문 셋에 답한다(4절). 답에 따라 `FullStacks` 의 상한(블록 수 · 스택 깊이)과 기준선 창을 `FullStacks` 로 올릴지를 정한다 | 대기 |
| L3 | 반복 로드 곡선 검사: 한 씬을 워밍업 1회 + 측정 8회 로드 · 언로드하며 회차마다 창을 닫아 `_totalBytes` · `_totalBlockCount` 곡선을 낸다. 8회가 평평하지 않으면(기울기 > 0) 실패하는 테스트나 시나리오 | 대기 |
| L4 | 보고 연결: 종료 보고와 에디터 프로파일러 패널이 `FullStacks` 보고서(줄 · 첫 프레임)를 보이게 한다. 켜는 방법(`gv_*`)과 줄 수 상한은 L2 로 정한다 | 대기 |

별도 단위: **소유 형식 강화** — 원시 `sw_new`(25곳)와 `Memory::allocate`(31곳)를 소유 형식(`make_unique` · 컨테이너)으로 바꿔 누수가 생기는 모양 자체를 줄인다(다른 작업자 몫). 창은 이 단위가 끝나도 남는다(컨테이너 증가 · 캐시 누적을 잡는다).

## 4. 측정으로 가릴 질문

Release 에서 혼자 돌린다([작업 방식](../11_Workflow.md)). 추정으로 정하지 않는다.
1. **씬 로드 한 번의 할당 수**(`getTotalAllocationCount()` 차이) — `FullStacks` 블록 테이블 메모리(블록 하나당 약 40 B + 해시 테이블 오버헤드)와 창이 닫힐 때 정렬 · 묶는 시간이 감당 가능한지.
2. **전수 캡처의 오버헤드** — 같은 씬 로드를 창 없이 / `TagTotals` / `FullStacks` 로 돌려 로드 시간을 비교하고 `CallStackCapture::capture` 의 할당당 비용, 스택 테이블 잠금(슬롯 16개)의 병렬 로드 대기를 본다. 결과로 기준선 창을 `FullStacks` 로 올릴 수 있는지, 전수가 안 되면 N번째 할당마다 표본을 잡는지를 정한다.
3. **워밍업 1 + 측정 8 회의 곡선** — 첫 회는 캐시 · 풀이 차는 증가라 버리고 이어지는 8회의 `_totalBytes` 가 평평한지 본다. 평평하다고 판정하는 기울기 임계값과 로더 · 로그 워커가 섞이는 정도를 이 곡선으로 정한다.

## 5. 읽을 곳 · 고칠 곳

코드: `Source/Core/Memory/Memory.{h,cpp}`(`AllocationRecord` · 헤더) · `Source/Core/Diagnostics/MemoryProfiler.{h,cpp}`(창) · `Source/Engine/Bootstrap/DiagnosticsSubsystem.cpp`(종료 보고) · `Test/CoreTest/Memory/TestMemoryProfiler.cpp`. 층: `Scripts/lint/rules/CheckCoreLayers.toml` 의 Memory 2 · Diagnostics 10(`Memory` 는 `recordFree` 에 `AllocationRecord` 를 넘기는 인터페이스만 알고 창을 모른다). 정본 위치: 결정 기록 5-3 "누수는 창 안에서 태어난 블록으로 잰다".

## 6. 추정 · 확인하지 못한 것

- L1 은 컴파일도 테스트도 돌리지 않았다(통합 단계에서 한 번에). 컴파일 확인이 필요한 곳: `ShardArray<LeakWindowShard>` 의 집합 초기화(`mutex` · `unordered_map` 멤버), `Ascending<&LeakBlockSample::_tag, …>` 의 열거형 비교, `unordered_map::emplace` 의 중복 키 동작.
- 첫 프레임 거르기는 심볼 글의 파일 경로(`Core/Memory/` · `Core/Container/`)와 심볼 이름(`sw::Memory::` · `MemoryInternal::`) 부분 문자열로 한다. PDB 줄 정보가 없는 구성(Linux 심볼 변환이 파일 경로를 주지 않는 경우)에서는 컨테이너 프레임이 걸러지지 않을 수 있다(추측, L2 에서 두 플랫폼으로 확인).
- 스탬프는 32비트이고 창 하나가 쓰는 회차는 수십 개라 돌아갈 일이 없다고 본다(추측). `TagTotals` 창이 Debug 종료 보고에서 예전과 같은 정보(태그 · 바이트 · 블록 수)를 내는지는 확인하지 않았다.
