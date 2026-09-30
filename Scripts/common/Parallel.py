#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
스크립트가 여러 파일·작업을 동시에 처리할 때 쓰는 한 자리.

왜 필요한가:
  린트·포맷·쿠킹 스크립트 일곱 곳이 **같은 열 줄**을 각자 복사해 두고 있었다 —
  워커 수를 정하고, `ThreadPoolExecutor` 를 열고, `as_completed` 로 결과를 모으는 그 패턴이다.
  복사본마다 워커 수 정책이 조금씩 달라서(`min(32, cpu*2)` · `min(16, cpu)` · `min(8, len, cpu)`)
  "이 저장소는 동시성을 어떻게 정하는가" 라는 질문에 답할 곳이 없었다. 여기가 그 자리다.

**왜 스레드인가 (프로세스가 아니라) — 그리고 언제 프로세스인가.**
  2026-09-08 에 린트를 `ProcessPoolExecutor` 로 바꿔 보고 **5.3s → 9.4s** 로 더 느려져 되돌렸다
  (docs/06_Backlog.md). 이 스크립트들의 일은 대부분 파일 읽기와 정규식이라, 프로세스를 띄우는 비용과
  경로 목록을 피클로 넘기는 비용이 이득을 넘는다. 다시 프로세스로 바꾸자고 제안하기 전에 그 숫자를 볼 것.

  **예외 — 파일마다 파이썬 정규식을 한참 돌리는 일은 `flatMapInProcesses`.** 스레드는 GIL 때문에 그런 일을
  겹치지 못한다(`CheckCodeConventions` 의 파일별 검사가 스레드 풀에서도 사실상 한 코어였다). 2026-09-30 에 다시 쟀다:
  **항목마다가 아니라 코어 수만큼의 덩어리로** 넘기면 프로세스 기동 · 피클 비용이 코어 수만큼만 들어 7.5 s → 2.25 s 다
  (6 코어, 결과 동일). 09-08 의 실험이 느렸던 것은 파일마다 넘겼기 때문으로 본다. 파일 읽기가 대부분인 일은 여전히 스레드다.

**정책이 둘인 이유.**
  - `getScanWorkerCount` — 파일을 읽고 훑는 일. IO 대기가 많아 코어보다 많이 띄우는 편이 낫다.
  - `getProcessWorkerCount` — 자식 프로세스를 띄우는 일(clang-format·컴파일러). 그쪽이 이미 코어를
    쓰므로 더 띄우면 서로 경합한다. 두 정책을 하나로 합치지 말 것 — 일의 성질이 다르다.
"""
from __future__ import annotations

import concurrent.futures
import os
from typing import Callable, Iterable, Iterator, Sequence, TypeVar

TItem = TypeVar("TItem")
TResult = TypeVar("TResult")

# 스캔(파일 읽기·정규식) 워커 상한. IO 대기가 많아 코어의 두 배까지 띄운다.
kMaxScanWorkers = 32
# 자식 프로세스를 띄우는 작업의 워커 상한. 그쪽이 코어를 쓰므로 코어 수를 넘지 않는다.
kMaxProcessWorkers = 16


def getCpuCount() -> int:
    """가용 코어 수 (알 수 없으면 4)."""
    return os.cpu_count() or 4


def getScanWorkerCount(itemCount: int | None = None) -> int:
    """파일 스캔용 워커 수. 항목이 워커보다 적으면 그만큼만 띄운다."""
    workers = min(kMaxScanWorkers, getCpuCount() * 2)
    if itemCount is not None:
        workers = max(1, min(workers, itemCount))
    return workers


def getProcessWorkerCount(itemCount: int | None = None) -> int:
    """자식 프로세스를 띄우는 작업용 워커 수."""
    workers = min(kMaxProcessWorkers, getCpuCount())
    if itemCount is not None:
        workers = max(1, min(workers, itemCount))
    return workers


def mapConcurrent(
    worker: Callable[[TItem], TResult],
    items: Sequence[TItem],
    *,
    workerCount: int | None = None,
    onProgress: Callable[[int, int], None] | None = None,
) -> Iterator[TResult]:
    """
    `items` 를 동시에 처리하고 **끝나는 순서대로** 결과를 흘려 줍니다.

    항목이 하나뿐이면 풀을 만들지 않고 그냥 부른다 — 풀 생성 비용이 일보다 큰 흔한 경우다.
    예외는 그대로 올라온다(호출부가 처리한다).

    `onProgress(끝난 수, 전체 수)` 는 몇 분씩 걸리는 훑기가 살아 있음을 보이려고 있다. 이것이
    없어서 `RunClangTidy` 와 `RunBuildWarnings` 가 **각자 풀을 열고 있었다** — 동시 처리를 한
    자리에 모은 뒤에도 둘만 남아 있던 이유가 그것이다.
    """
    if not items:
        return
    if len(items) == 1:
        yield worker(items[0])
        if onProgress is not None:
            onProgress(1, 1)
        return

    workers = workerCount if workerCount is not None else getScanWorkerCount(len(items))
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as executor:
        futures = [executor.submit(worker, item) for item in items]
        for doneCount, future in enumerate(concurrent.futures.as_completed(futures), start=1):
            yield future.result()
            if onProgress is not None:
                onProgress(doneCount, len(items))


def flatMapConcurrent(
    worker: Callable[[TItem], Iterable[TResult]],
    items: Sequence[TItem],
    *,
    workerCount: int | None = None,
) -> list[TResult]:
    """
    `items` 를 동시에 처리하고 각 결과(목록)를 이어 붙여 돌려줍니다 — 위반 목록을 모으는 흔한 모양.

    @note 순서는 **완료 순**이라 결정적이지 않다. 출력이 안정적이어야 하면 호출부가 정렬한다.
    """
    collected: list[TResult] = []
    for result in mapConcurrent(worker, items, workerCount=workerCount):
        if result:
            collected.extend(result)
    return collected


#: 프로세스 하나가 맡을 최소 항목 수. 이보다 적으면 그만큼 적게 띄우고, 한 개면 띄우지 않는다 — 윈도우에서 프로세스를 띄우고
#: 스크립트를 다시 import 하는 데 0.3 s 가량 든다(셀프테스트처럼 파일 몇 개짜리 훑기가 그 값을 치르지 않게).
kMinItemsPerProcess = 64


def runChunkInternal(worker: Callable[[TItem], Iterable[TResult]],
                     listIndexedItem: list[tuple[int, TItem]]) -> list[tuple[int, list[TResult]]]:
    """자식 프로세스에서 덩어리 하나를 돕니다. 순서를 되돌리려고 항목의 자리를 같이 돌려준다."""
    return [(index, list(worker(item) or ())) for index, item in listIndexedItem]


def flatMapInProcesses(
    worker: Callable[[TItem], Iterable[TResult]],
    items: Sequence[TItem],
    *,
    workerCount: int | None = None,
) -> list[TResult]:
    """
    `items` 를 **프로세스**로 나눠 처리하고 각 결과(목록)를 **항목 순서대로** 이어 붙여 돌려줍니다.

    파일마다 파이썬 정규식을 오래 돌리는 일(CPU)에 쓴다 — 스레드로는 GIL 에 막혀 겹치지 않는다. 항목을 코어 수만큼의 덩어리로
    나눠 넘기므로 프로세스 기동 · 피클은 그만큼만 든다(모듈 머리말 참고).

    - `worker` 와 항목 · 결과는 피클할 수 있어야 한다: 모듈 최상위 함수(인자를 묶으려면 `functools.partial`)와 그 모듈의 타입.
      스크립트로 직접 돌리는 파일의 함수라면 그 스크립트가 `if __name__ == "__main__":` 으로 진입점을 막아 두어야 한다(자식이
      같은 파일을 다시 import 한다).
    - 항목이 `kMinItemsPerProcess` 의 두 배보다 적으면 프로세스를 띄우지 않고 이 프로세스에서 돈다.
    """
    listItem = list(items)
    if workerCount is None:
        workerCount = getProcessWorkerCount(max(1, len(listItem) // kMinItemsPerProcess))
    if workerCount <= 1 or len(listItem) < 2 * kMinItemsPerProcess:
        return [result for item in listItem for result in (worker(item) or ())]

    listChunk = [[(index, listItem[index]) for index in range(start, len(listItem), workerCount)] for start in range(workerCount)]
    with concurrent.futures.ProcessPoolExecutor(max_workers=workerCount) as pool:
        listIndexed = [pair for chunkResult in pool.map(runChunkInternal, [worker] * workerCount, listChunk) for pair in chunkResult]
    listIndexed.sort(key=lambda pair: pair[0])
    return [result for _, listResult in listIndexed for result in listResult]


def runUntilNonZero(
    worker: Callable[[TItem], int],
    items: Sequence[TItem],
    *,
    workerCount: int | None = None,
) -> int:
    """
    `items` 를 동시에 처리하다 **0 이 아닌 결과가 처음 나오면** 그 값을 돌려줍니다 (전부 0 이면 0).

    자식 프로세스를 띄우는 배치용이라 워커 수 기본값이 `getProcessWorkerCount` 다.
    이미 제출된 나머지는 끝까지 돈다 — 중간에 끊지 않는다(자식 프로세스를 반만 죽이는 것보다 낫다).
    """
    if not items:
        return 0
    workers = workerCount if workerCount is not None else getProcessWorkerCount(len(items))
    for resultCode in mapConcurrent(worker, items, workerCount=workers):
        if resultCode != 0:
            return resultCode
    return 0
