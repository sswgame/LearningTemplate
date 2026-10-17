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
| ① | 빈 데크 pop · steal 이 seq_cst 펜스 전에 돌아옴 | `TaskManagerBenchTest.EmptyDequeProbe` · `.ForkJoinLatency` · `.SmallTaskThroughput` | | | | 두 줄 판정 + 근거 주석 |
| ② | 대기자 스핀이 `_workEpoch` 가 바뀔 때만 큐를 봄 | `TaskManagerBenchTest.ConcurrentForkJoinWaiters` · `.StageLikeRenderGraphLevel` | | | | 대기 상태 구조체 하나, 깨우지 않은 `submitWithoutWake` 묶음은 대기자도 못 봄(계약) |
| ③-1 | 태그 통계 `alignas( 64 )` | `MemoryTagBenchTest.ConcurrentAllocationContention`(태그마다 다른 줄) | | | | 없음(크기 태그당 128 B) |
| ③-2 | 태그 통계를 스레드 칸에 더함, 최고치는 읽을 때 | `MemoryTagBenchTest.ConcurrentAllocationContention` · `.AllocationOverheadWithTrackingOffAndOn` | | | | `getStats` 가 값 스냅샷, 최고치가 읽기 시점 값(순간 최고치 놓침), 프로파일러 약 13 KiB |
| ④ | 활성 태스크 수를 칸으로, 완료 쪽은 `waitAll` 대기자가 있을 때만 합을 봄 | `TaskManagerBenchTest.WorkerSpawnedTaskTree` · `.SmallTaskThroughput` | | | | 노드에 칸 번호 1 B, 0 확인이 칸 16 개 읽기 |
| ⑤ | 로그 생산자: 날짜 캐시 thread_local · 리스너 스냅샷 · 작업 스레드가 잘 때만 깨움 | `LogSinkBenchTest.ProducerCost` | | | | 리스너 목록 사본 관리, 깨우기 플래그의 Dekker 짝 |

## B 컨테이너 · 네트워크

| 단위 | 목적 | 벤치 | 전 p50 / p99 | 후 p50 / p99 | 판정 | 유지보수 비용 |
|---|---|---|---|---|---|---|
| ① | `SequenceBuffer` 칸을 다시 쓸 때 버퍼 용량 유지(크기 상수화는 feat18) | `NetCoreBenchTest.SequenceBufferSlotReuse` | | | | 원소에 `resetForReuse`(모든 필드를 `T{}` 로, 버퍼만 남김) |
| ② | `sw::deque` 바이트 기준 블록 + 고정 용량 큐 링 버퍼 | 미구현 | | | | 컨테이너 직접 구현 — 빌드 없이 넣기에 위험이 커서 다음 묶음 |
| ③ | 해시 표 노드 `uint32 next + 해시 태그` | 미구현 | | | | map · set 표 코어 통합 — 같은 이유로 다음 묶음 |
| ④ | `BitStream` 64비트 워드로 쓰기 · 읽기(와이어 바이트 고정 테스트 `BitStreamWireTest` 먼저) | `NetCoreBenchTest.BitStreamMixedFields` | | | | 읽기의 8 바이트 통째 읽기는 리틀 엔디언 전제 |
| ⑤ | `Memory::copy` · `move` · `set` · `compare` 헤더 inline | `MemoryCopyBenchTest.CopyAndCompareBySize` | | | | 헤더를 바꾸면 전체 재컴파일 |

B ② · ③ 은 이 묶음에서 하지 않았고(빌드 없이 컨테이너를 직접 구현하는 위험), 남은 단위는 [다음 세션 인계](NEXT.md) 2절 11번에 남는다.
