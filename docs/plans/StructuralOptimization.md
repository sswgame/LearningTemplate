# 구조 최적화 측정 표 (NEXT 2절 11번 A · B)

구조 최적화 묶음 A(동시성)와 B(컨테이너 · 네트워크)의 단위마다 목적, 전/후를 재는 벤치, 결과 자리를 둔다. 단위는 브랜치 `wt/feat19` 에서
**벤치 커밋을 먼저**, 최적화를 단위마다 따로 커밋했다. 벤치 커밋을 지은 결과가 "전", 단위 커밋을 지은 결과가 "후"다. 이득이 없으면 그 단위 커밋만
`git revert` 하고 벤치는 남긴다(사용자 선호: 측정 없이 최적화하지 않는다).

## 재는 법

- Release(`build/Ninja-Release`), VSync 와 상관없는 CoreTest 벤치다. 다른 빌드 · 테스트와 겹치지 않게 혼자 돌린다.
- `cd build/Ninja-Release/Bin && ../TestBin/CoreTest.exe --test_filter=<스위트>.<케이스> --load_suites=only` 를 3~5 회. `[Bench]` 줄의 p50 · p99(ns/op 줄은 가장 짧은 판)를 적는다.
- 전/후는 같은 PC 에서 번갈아 잰다(전 → 후 → 전 → 후). 차이가 판 사이 흔들림보다 작으면 "차이 없음" 으로 적고 되돌릴지 정한다.

## A 동시성

| 단위 | 목적 | 벤치(CoreTest) | 전 p50 / p99 | 후 p50 / p99 | 판정 | 유지보수 비용 |
|---|---|---|---|---|---|---|
| 형식 | `ShardedCounter` · `ShardArray`(스레드 칸 16, 읽는 쪽이 합침) | `ShardedCounterTest.*`(정합성) | — | — | — | 헤더 하나 + 칸 번호 함수, 짝 규칙을 헤더 계약으로 |
| ① | 빈 데크 pop · steal 이 seq_cst 펜스 전에 돌아옴 | `TaskManagerBenchTest.EmptyDequeProbe` · `.ForkJoinLatency` · `.SmallTaskThroughput` | pop 1.6 ns · steal 1.4 ns | pop 0.2 ns · steal 0.2 ns | 유지 | 두 줄 판정 + 근거 주석 |
| ② | 대기자 스핀이 `_workEpoch` 가 바뀔 때만 큐를 봄 | `TaskManagerBenchTest.ConcurrentForkJoinWaiters` · `.StageLikeRenderGraphLevel` | 스테이지 cold 111 / 212 us, 대기자 둘 150 / 238 us | 스테이지 cold 110 / 179 us, 대기자 둘 150 / 236 us | 유지(p99 이득, p50 같음) | 대기 상태 구조체 하나, 깨우지 않은 `submitWithoutWake` 묶음은 대기자도 못 봄(계약) |
| ③-1 | 태그 통계 `alignas( 64 )` | `MemoryTagBenchTest.ConcurrentAllocationContention`(태그마다 다른 줄) | (③-2 와 함께 잼) | (③-2 와 함께 잼) | 유지 | 없음(크기 태그당 128 B) |
| ③-2 | 태그 통계를 스레드 칸에 더함, 최고치는 읽을 때 | `MemoryTagBenchTest.ConcurrentAllocationContention` · `.AllocationOverheadWithTrackingOffAndOn` | 같은 태그 14 스레드 36110 / 36675 us, 태그마다 2722 / 2844 us, 1 스레드 37.9 ns | 같은 태그 1923 / 2178 us, 태그마다 1968 / 2128 us, 1 스레드 38.9 ns | 유지(경합 -95 %) | `getStats` 가 값 스냅샷, 최고치가 읽기 시점 값(순간 최고치 놓침), 프로파일러 약 13 KiB |
| ④ | 활성 태스크 수를 칸으로, 완료 쪽은 `waitAll` 대기자가 있을 때만 합을 봄 | `TaskManagerBenchTest.WorkerSpawnedTaskTree` · `.SmallTaskThroughput` | task tree 492 / 622 us, tiny 4096 557 us | task tree 631 / 704 us, tiny 4096 620 us | 되돌림(196cc7733) | 노드에 칸 번호 1 B, 0 확인이 칸 16 개 읽기 |
| ⑤ | 로그 생산자: 날짜 캐시 thread_local · 리스너 스냅샷 · 작업 스레드가 잘 때만 깨움 | `LogSinkBenchTest.ProducerCost` | 4 스레드 · 리스너 하나 441 / 494 us, 무리스너 354 / 406 us | 316 / 344 us, 267 / 316 us | 유지 | 리스너 목록 사본 관리, 깨우기 플래그의 Dekker 짝 |

## B 컨테이너 · 네트워크

| 단위 | 목적 | 벤치 | 전 p50 / p99 | 후 p50 / p99 | 판정 | 유지보수 비용 |
|---|---|---|---|---|---|---|
| ① | `SequenceBuffer` 칸을 다시 쓸 때 버퍼 용량 유지(크기 상수화는 feat18) | `NetCoreBenchTest.SequenceBufferSlotReuse` | 233.5 ns/op | 191.6 ns/op | 유지 | 원소에 `resetForReuse`(모든 필드를 `T{}` 로, 버퍼만 남김) |
| ② | `sw::deque` 바이트 기준 블록 + 고정 용량 큐 링 버퍼 | 미구현 | | | | 컨테이너 직접 구현 — 빌드 없이 넣기에 위험이 커서 다음 묶음 |
| ③ | 해시 표 노드 `uint32 next + 해시 태그` | 미구현 | | | | map · set 표 코어 통합 — 같은 이유로 다음 묶음 |
| ④ | `BitStream` 64비트 워드로 쓰기 · 읽기(와이어 바이트 고정 테스트 `BitStreamWireTest` 먼저) | `NetCoreBenchTest.BitStreamMixedFields` | 쓰기 4.9 · 읽기 3.8 ns/op | 쓰기 4.3 · 읽기 2.0 ns/op | 유지 | 읽기의 8 바이트 통째 읽기는 리틀 엔디언 전제 |
| ⑤ | `Memory::copy` · `move` · `set` · `compare` 헤더 inline | `MemoryCopyBenchTest.CopyAndCompareBySize` | 8 B 5.8 ns ~ 4 KiB 35.5 ns | 5.7 ~ 35.3 ns(1 ~ 3 %, 잡음 안) | 되돌림(9c1bff0f4) | 헤더를 바꾸면 전체 재컴파일 |

측정(2026-10-18 통합 `integ6`, Release, CoreTest 를 따로 지은 워크트리, 다른 빌드 없이 5 회의 중앙값): A 는 벤치 커밋(`bd8c8e79d`)과 A 끝(`2b4fc46aa`)을, ① · ② · ④ 는 그 단위 앞뒤 커밋을 견줬다.
B 는 B 벤치 커밋(`2b4fc46aa`)과 feat19 끝(`1bb8c33c6`)을 견줬다. A ⑤ 이후 커밋은 그대로는 컴파일되지 않아(리스너 스냅샷이 비 const `broadcast` 를 부름) 통합의 `broadcastSnapshot` 수정만 얹어 쟀다.
`forkJoin hot` p50 은 2 us 와 17 ~ 19 us 사이를 실행마다 오가 단위 판정에 쓰지 않았다. ③-1 과 ③-2 는 따로 재지 않았다.

B ② · ③ 은 이 묶음에서 하지 않았고(빌드 없이 컨테이너를 직접 구현하는 위험), 남은 단위는 [다음 세션 인계](NEXT.md) 2절 11번에 남는다.
