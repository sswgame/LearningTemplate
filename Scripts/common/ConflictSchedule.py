"""
서로 겹치면 안 되는 일(충돌)은 순서대로, 겹쳐도 되는 일은 동시에 돌리는 작은 실행기.

`Parallel.mapConcurrent` 는 일이 모두 독립일 때의 풀이고 이쪽은 일 사이에 "같이 못 도는" 관계가 있을 때의 풀이다(검증 묶음의 잠금).
규칙은 하나다: **앞선 일 중 아직 안 끝난(돌고 있거나 기다리는) 충돌 일이 있으면 시작하지 않는다.** 그래서 충돌하는 일끼리는 항상 목록 순서로
돌고, `jobs == 1` 이면 목록 순서 그대로다(가장 앞의 안 끝난 일은 기다릴 앞선 일이 없다). 충돌 관계는 부르는 쪽이 정하고 여기서는 새로 만들지 않는다.

일이 예외를 던지면 새 일을 더 시작하지 않고, 돌고 있던 일을 끝까지 기다린 뒤 첫 예외를 다시 던진다(자식 프로세스를 반만 죽이지 않는다).
"""

from __future__ import annotations

import concurrent.futures
from typing import Callable, Sequence, TypeVar

TUnit = TypeVar("TUnit")


def runConflictSchedule(listUnit: Sequence[TUnit], hasConflict: Callable[[TUnit, TUnit], bool], runUnit: Callable[[TUnit], None], jobs: int) -> None:
    """`listUnit` 을 최대 `jobs` 개까지 동시에 `runUnit` 으로 돌린다. `hasConflict(앞, 뒤)` 가 참이면 뒤는 앞이 끝난 뒤에 시작한다."""
    if jobs < 1:
        raise ValueError(f"jobs 는 1 이상입니다: {jobs}")
    listPending = list(range(len(listUnit)))
    mapRunning: dict[concurrent.futures.Future[None], int] = {}
    firstError: BaseException | None = None
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as executor:
        while (listPending and firstError is None) or mapRunning:
            if firstError is None:
                for position, index in enumerate(listPending):
                    if len(mapRunning) >= jobs:
                        break
                    listUnfinished = [listPending[earlier] for earlier in range(position)] + list(mapRunning.values())
                    if any(other < index and hasConflict(listUnit[other], listUnit[index]) for other in listUnfinished):
                        continue
                    mapRunning[executor.submit(runUnit, listUnit[index])] = index
                listPending = [index for index in listPending if index not in mapRunning.values()]
            listDone, _ = concurrent.futures.wait(mapRunning, return_when=concurrent.futures.FIRST_COMPLETED)
            for future in listDone:
                del mapRunning[future]
                if firstError is None and future.exception() is not None:
                    firstError = future.exception()
    if firstError is not None:
        raise firstError
