"""
스크립트가 여러 파일·작업을 동시에 처리할 때 쓰는 한 자리 — 워커 수 정책이 스크립트마다 갈리지 않게 한다.

기본은 **스레드**다: 일의 대부분이 파일 읽기와 정규식이라 프로세스 기동 · 피클 비용이 이득을 넘는다(린트 전체를 파일마다
`ProcessPoolExecutor` 로 넘기면 5.3 s 가 9.4 s). 예외는 파일마다 파이썬 정규식을 오래 돌리는 일 — GIL 때문에 스레드로는 겹치지 않아
`flat_map_in_processes` 가 **코어 수만큼의 덩어리로** 넘긴다(7.5 s → 2.25 s, 6 코어). 프로세스로 바꾸자고 하기 전에 같은 작업으로 재 볼 것.

워커 수 정책은 둘이다(합치지 말 것): 스캔은 IO 대기가 많아 코어의 두 배까지, 자식 프로세스를 띄우는 일(clang-format)은 코어 수까지.
"""
from __future__ import annotations

import concurrent.futures
import os
from typing import Callable, Iterable, Iterator, Sequence, TypeVar

from common.exit_code import ExitCode

TItem = TypeVar("TItem")
TResult = TypeVar("TResult")

# 스캔(파일 읽기·정규식) 워커 상한. IO 대기가 많아 코어의 두 배까지 띄운다.
MAX_SCAN_WORKERS = 32
# 자식 프로세스를 띄우는 작업의 워커 상한. 그쪽이 코어를 쓰므로 코어 수를 넘지 않는다.
MAX_PROCESS_WORKERS = 16


def get_cpu_count() -> int:
    """가용 코어 수 (알 수 없으면 4)."""
    return os.cpu_count() or 4


def get_scan_worker_count(item_count: int | None = None) -> int:
    """파일 스캔용 워커 수. 항목이 워커보다 적으면 그만큼만 띄운다."""
    workers = min(MAX_SCAN_WORKERS, get_cpu_count() * 2)
    if item_count is not None:
        workers = max(1, min(workers, item_count))
    return workers


def get_process_worker_count(item_count: int | None = None) -> int:
    """자식 프로세스를 띄우는 작업용 워커 수."""
    workers = min(MAX_PROCESS_WORKERS, get_cpu_count())
    if item_count is not None:
        workers = max(1, min(workers, item_count))
    return workers


def map_concurrent(
    worker: Callable[[TItem], TResult],
    items: Sequence[TItem],
    *,
    worker_count: int | None = None,
) -> Iterator[TResult]:
    """`items` 를 동시에 처리하고 **끝나는 순서대로** 결과를 흘려 줍니다. 항목이 하나뿐이면 풀 없이 부른다. 예외는 그대로 올라온다."""
    if not items:
        return
    if len(items) == 1:
        yield worker(items[0])
        return

    workers = worker_count if worker_count is not None else get_scan_worker_count(len(items))
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as executor:
        futures = [executor.submit(worker, item) for item in items]
        for future in concurrent.futures.as_completed(futures):
            yield future.result()


def flat_map_concurrent(
    worker: Callable[[TItem], Iterable[TResult]],
    items: Sequence[TItem],
    *,
    worker_count: int | None = None,
) -> list[TResult]:
    """
    `items` 를 동시에 처리하고 각 결과(목록)를 이어 붙여 돌려줍니다 — 위반 목록을 모으는 흔한 모양.

    @note 순서는 **완료 순**이라 결정적이지 않다. 출력이 안정적이어야 하면 호출부가 정렬한다.
    """
    collected: list[TResult] = []
    for result in map_concurrent(worker, items, worker_count=worker_count):
        if result:
            collected.extend(result)
    return collected


#: 프로세스 하나가 맡을 최소 항목 수. 이보다 적으면 그만큼 적게 띄우고, 한 개면 띄우지 않는다 — 윈도우에서 프로세스를 띄우고
#: 스크립트를 다시 import 하는 데 0.3 s 가량 든다(셀프테스트처럼 파일 몇 개짜리 훑기가 그 값을 치르지 않게).
MIN_ITEMS_PER_PROCESS = 64


def _run_chunk(worker: Callable[[TItem], Iterable[TResult]],
                     list_indexed_item: list[tuple[int, TItem]]) -> list[tuple[int, list[TResult]]]:
    """자식 프로세스에서 덩어리 하나를 돕니다. 순서를 되돌리려고 항목의 자리를 같이 돌려준다."""
    return [(index, list(worker(item) or ())) for index, item in list_indexed_item]


def flat_map_in_processes(
    worker: Callable[[TItem], Iterable[TResult]],
    items: Sequence[TItem],
    *,
    worker_count: int | None = None,
) -> list[TResult]:
    """
    `items` 를 **프로세스**로 나눠 처리하고 각 결과(목록)를 **항목 순서대로** 이어 붙여 돌려줍니다.

    파일마다 파이썬 정규식을 오래 돌리는 일(CPU)에 쓴다 — 스레드로는 GIL 에 막혀 겹치지 않는다. 항목을 코어 수만큼의 덩어리로
    나눠 넘기므로 프로세스 기동 · 피클은 그만큼만 든다(모듈 머리말 참고).

    - `worker` 와 항목 · 결과는 피클할 수 있어야 한다: 모듈 최상위 함수(인자를 묶으려면 `functools.partial`)와 그 모듈의 타입.
      스크립트로 직접 돌리는 파일의 함수라면 그 스크립트가 `if __name__ == "__main__":` 으로 진입점을 막아 두어야 한다(자식이
      같은 파일을 다시 import 한다).
    - 항목이 `MIN_ITEMS_PER_PROCESS` 의 두 배보다 적으면 프로세스를 띄우지 않고 이 프로세스에서 돈다.
    """
    list_item = list(items)
    if worker_count is None:
        worker_count = get_process_worker_count(max(1, len(list_item) // MIN_ITEMS_PER_PROCESS))
    if worker_count <= 1 or len(list_item) < 2 * MIN_ITEMS_PER_PROCESS:
        return [result for item in list_item for result in (worker(item) or ())]

    list_chunk = [[(index, list_item[index]) for index in range(start, len(list_item), worker_count)] for start in range(worker_count)]
    with concurrent.futures.ProcessPoolExecutor(max_workers=worker_count) as pool:
        list_indexed = [pair for chunk_result in pool.map(_run_chunk, [worker] * worker_count, list_chunk) for pair in chunk_result]
    list_indexed.sort(key=lambda pair: pair[0])
    return [result for _, list_result in list_indexed for result in list_result]


def run_until_non_zero(
    worker: Callable[[TItem], int],
    items: Sequence[TItem],
    *,
    worker_count: int | None = None,
) -> int:
    """
    `items` 를 동시에 처리하다 **0 이 아닌 결과가 처음 나오면** 그 값을 돌려줍니다 (전부 0 이면 0).

    자식 프로세스를 띄우는 배치용이라 워커 수 기본값이 `get_process_worker_count` 다.
    이미 제출된 나머지는 끝까지 돈다 — 중간에 끊지 않는다(자식 프로세스를 반만 죽이는 것보다 낫다).
    """
    if not items:
        return ExitCode.PASSED
    workers = worker_count if worker_count is not None else get_process_worker_count(len(items))
    for result_code in map_concurrent(worker, items, worker_count=workers):
        if result_code != 0:
            return result_code
    return ExitCode.PASSED
