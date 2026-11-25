"""
시나리오 하나가 앞서 무엇이 돌았든 같은 사진을 내는지 본다 — 혼자 · 앞 단계 하나 · 두 번 이어 돌려 찍은 사진을 바이트로 견준다.

  py -3 -m Scripts scenario-bisect <체인> <단계> --determinism [--backend dx12] [--preset Ninja-Release]

골든 비교는 상한(`Config/Dev/GoldenImage.json`) 안의 차이를 통과시키므로, 로딩 길이 · 핸들 번호 · 앞 단계에 달린 값은 우연히 상한을 넘는 날에야 보인다
(wildwest_mount 의 먼 캐릭터 갱신 시점이 그랬다). 같은 백엔드 · 같은 프로세스 모양의 사진은 바이트까지 같아야 한다. 묶음 검증의 changed 단계는 **새 시나리오**에 이것을 쓴다.
"""

from __future__ import annotations

import hashlib
import sys
from pathlib import Path
from typing import Callable, Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

#: 묶음 하나(단계 둘까지)의 시한 — 단일 시나리오 프로세스 시한(`AutomationTimeLimits::getChainKillSeconds( 1 )` 545 초)의 두 배에 기동 여유.
SEQUENCE_TIMEOUT_SECONDS = 1200
IMAGE_SUFFIXES = frozenset({".ppm", ".png"})

#: (돌릴 단계 경로들) → 실행. 진 실행이면 그 까닭, 통과면 빈 글.
SequenceRunner = Callable[[Sequence[str]], str]


def make_stage_runs(list_path: Sequence[str], target_index: int) -> list[tuple[str, list[str]]]:
    """(이름, 돌릴 단계들). 앞 단계는 체인에서 바로 앞, 첫 단계면 바로 뒤의 것이다. 체인에 하나뿐이면 앞 단계 실행이 없다."""
    target = list_path[target_index]
    list_sequence = [("혼자", [target])]
    list_neighbor = [path for path in (list_path[target_index - 1:target_index] or list_path[target_index + 1:target_index + 2]) if path != target]
    if list_neighbor:
        list_sequence.append(("앞 단계 하나", [list_neighbor[0], target]))
    list_sequence.append(("두 번 이어", [target, target]))
    return list_sequence


def list_output_folders(automation_dir: Path, scenario_name: str) -> set[Path]:
    """시나리오 산출물 폴더들(`Saved/Automation/<이름>/<프로세스>…`) — 실행 하나에 하나씩 생긴다(`AutomationRunner::makeOutputDirectory`)."""
    root = automation_dir / scenario_name
    return {path for path in root.iterdir() if path.is_dir() and not path.name.startswith("per-backend-")} if root.is_dir() else set()


def read_images(folder: Path) -> dict[str, str]:
    """폴더의 사진(상대 경로 → 내용 해시)."""
    return {path.relative_to(folder).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted(folder.rglob("*")) if path.suffix.lower() in IMAGE_SUFFIXES}


def describe_differences(list_run: Iterable[tuple[str, dict[str, str]]]) -> list[str]:
    """첫 실행의 사진과 다른 실행의 것이 다르면 한 줄씩."""
    list_difference: list[str] = []
    baseline: tuple[str, dict[str, str]] | None = None
    for run_name, images in list_run:
        if baseline is None:
            baseline = (run_name, images)
            continue
        for name in sorted(baseline[1].keys() | images.keys()):
            if baseline[1].get(name) == images.get(name):
                continue
            if name not in images:
                list_difference.append(f"{name}: '{run_name}' 실행에 없다")
            elif name not in baseline[1]:
                list_difference.append(f"{name}: '{baseline[0]}' 실행에 없다")
            else:
                list_difference.append(f"{name}: '{baseline[0]}' 와 '{run_name}' 의 바이트가 다르다")
    return list_difference


def make_sequence_command(chain_command: Sequence[str], list_stage: Sequence[str], backend: str, report: Path, log_file: Path) -> list[str]:
    """체인 명령을 단계 묶음 하나로 — 나누기를 빼고 백엔드 하나에서 돈다. `<Launch>` 값은 App 이 첫 시나리오에서 읽는다."""
    from dev.changed_tests import set_argument
    command = list(chain_command)
    for key in ("scenario-backend-split", "scenario-backend-split-offset", "scenario-keep-going", "shader-request-log"):
        command = set_argument(command, key, None)
    for key, value in (("scenario", ",".join(list_stage)), ("scenario-backends", backend), ("scenario-report", str(report)), ("log-file", str(log_file))):
        command = set_argument(command, key, value)
    return command


def make_runner(chain_command: Sequence[str], target_name: str, backend: str, build_dir: Path, working_directory: Path) -> SequenceRunner:
    """실제 앱으로 묶음을 돌리는 실행기 — 판정은 실행마다 비운 보고 폴더의 단계 보고다(`scenario_bisect` 와 같다)."""
    from common.run_process import run_process
    from dev.scenario_runs import describe_stage_failures, make_run_report_folder, read_run_reports, stage_failed_in_reports

    def run(list_stage: Sequence[str]) -> str:
        run_dir = make_run_report_folder(build_dir, len(list_stage))
        command = make_sequence_command(chain_command, list_stage, backend, run_dir / "report.json", run_dir / "parent.log")
        run_process(command, cwd=working_directory, timeout_seconds=SEQUENCE_TIMEOUT_SECONDS)
        list_report = read_run_reports(run_dir)
        return describe_stage_failures(list_report, target_name) if stage_failed_in_reports(list_report, target_name) else ""

    return run


def check_determinism(list_path: Sequence[str], target_index: int, scenario_name: str, automation_dir: Path, run: SequenceRunner) -> list[str]:
    """시나리오 하나의 문제 목록(없으면 통과). 진 실행 · 사진이 하나도 없는 시나리오도 문제다 — 견줄 것이 없는 통과는 통과가 아니다."""
    list_run: list[tuple[str, dict[str, str]]] = []
    for run_name, list_stage in make_stage_runs(list_path, target_index):
        before = list_output_folders(automation_dir, scenario_name)
        problem = run(list_stage)
        if problem:
            return [f"'{run_name}' 실행이 졌다: {problem}"]
        list_folder = sorted(list_output_folders(automation_dir, scenario_name) - before, key=lambda path: (path.stat().st_mtime_ns, len(path.name), path.name))
        if len(list_folder) != list_stage.count(list_path[target_index]):
            return [f"'{run_name}' 실행의 산출물 폴더가 {len(list_folder)} 개다({automation_dir / scenario_name})"]
        for index, folder in enumerate(list_folder):
            list_run.append((f"{run_name} {index + 1}" if len(list_folder) > 1 else run_name, read_images(folder)))
    if not any(images for _, images in list_run):
        return ["사진을 찍지 않는 시나리오다 — 견줄 것이 없다"]
    return describe_differences(list_run)
