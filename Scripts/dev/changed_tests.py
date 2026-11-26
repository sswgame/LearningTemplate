"""
새로 들어온 · 바뀐 테스트 · 시나리오를 먼저 도는 단계(`test-history changed`, 묶음 검증의 `changed`).

**`changed`.** push 는 검증한 커밋만 하므로 `origin/main` 에 있는 것은 돈 적이 있고, 그 뒤에 더해지거나 바뀐 것은 돈 적이 없다 — 상태 파일 없이
`git diff --name-status <base>...HEAD` 로 가른다(이력 DB 는 기계마다 달라 쓰지 않는다). 묶음 검증이 build 와 cpu 사이에 부른다(`preflight.sh` 의 `changed` 단계).
  (a) 바뀐 소스 · 테스트 파일의 기능 폴더(`…/Tests/…` 는 `Tests` 앞, 아니면 그 파일의 폴더 — 테스트가 없으면 한 단계 위)를 그 폴더를 싣는 단위 항목
      (`UnitTests` · `UnitTests.<게임>` · `UnitTests.OnlineKits` — ctest 의 명령을 `ctest --show-only=json-v1` 에서 읽는다)에 `--test-source=` 로 좁혀 돈다.
      에디터 쪽(`/Editor/` 소스 · 편집기 시나리오 · `Content/editor` 데이터)이나 리플렉션 선언(`REFLECT` 이 있는 헤더 — 메타 `Icon=` · 분류 · 표시 이름)이 바뀌었으면 에디터 단위 테스트(`UnitTests.Editor` — 창 · GPU 없는 `App --test-editor`)를 돈다. 단위라 시나리오보다 먼저다.
  (b) 더해지거나 바뀐 `*.scenario.xml`(골든이 바뀌면 그 시나리오)을 하나씩 제 프로세스로, 그 시나리오가 든 ctest 항목의 백엔드마다 돈다.
  돌 것이 있으면 먼저 프리셋을 증분 빌드한다 — HEAD 와 다른 옛 바이너리로 판정하지 않는다(지면 `CHANGED_FAILED`).
  둘 다 **없는 기준 파일만 쓴다**(`--record-missing-snapshots` · `-scenario-record-missing-golden`) — 쓴 케이스 · 시나리오는 `MissingBaseline` 으로 지고, 쓴 파일(추적 안 된
  `golden/` · `Snapshots/`)이 있으면 목록을 내고 멈춘다. 통합 담당이 보고 커밋한 뒤 `--from changed` 로 다시 돌려 기록 없이 통과해야 한다.
  출력은 표 하나(항목 · 새것/바뀐 것 · 실패 종류 · 까닭 · 그 파일을 더한 갈래)이고, 끝 줄은 `CHANGED_OK=<sha>` 또는 `CHANGED_FAILED` 다. 새 시나리오가 모든 백엔드에서
  같은 단언으로 지면 `전제`, 한 백엔드만 지면 `백엔드 <이름>` 으로 묶는다.
"""

from __future__ import annotations

import concurrent.futures
import datetime
import json
import re
import shutil
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Sequence

from common.exit_code import ExitCode
from common.git import run_git, run_git_or_raise
from common.paths import get_build_dir
from common.run_process import ProcessResult, build_preset_or_exit, run_process
from dev.run_history import PASSED_RESULTS, QuarantineEntry, read_quarantine, read_reports, read_verify_config
from dev.scenario_reach import read_reach, select_reached
from net.ctest_items import read_items as read_net_items

CHANGED_OUTPUT = Path("Testing") / "Changed"   # 빌드 폴더 아래 이 단계의 로그 · 보고 — 돌 때마다 비운다
# 에디터 단위 테스트(창 · GPU 없는 `App --test-editor`) — 시나리오 읽기 검사 · 아이콘 · 테마 · 현지화 검사가 모두 여기다.
EDITOR_UNIT_TEST_ITEM = "UnitTests.Editor"
REFLECT_DECLARATION = re.compile(r"^\s*(?:REFLECT|PROPERTY|ENUM)\(", re.MULTILINE)
UNIT_ITEM_PATTERN = re.compile(r"^UnitTests(\.[A-Za-z0-9_]+)?$")
SOURCE_SUFFIXES = (".cpp", ".h", ".hpp", ".inl")
SCENARIO_SUFFIX = ".scenario.xml"
PAIR_SUFFIX = re.compile(r"\.[12]\.scenario\.xml$")   # 두 번 기동 쌍(`sequence/` · `statedir/`)은 ctest 가 cmake -P 로 돈다(`-DPAIR=<앞머리>`)
MERGE_SUBJECT = re.compile(r"^Merge (?:remote-tracking )?branch '(?:origin/)?([^']+)'")
SOURCE_PATH = re.compile(r"(?:^|/)(Source/.+)$")
# 시한은 ctest 항목의 TIMEOUT 속성이다(없으면 ctest 의 기본 1500 초). 단계가 여럿인 체인에서 꺼낸 시나리오 하나만 체인 시한(단계 초 × 단계 수 × 물결)을 나눠 쓸 수 없어
# — 단계 몫에는 기동 · 종료가 없다 — App 의 단일 시나리오 프로세스 시한(`AutomationTimeLimits::getChainKillSeconds( 1 )` 545 초) 위에 기동 · 셰이더 여유를 둔 값을 쓴다.
CTEST_DEFAULT_TIMEOUT_SECONDS = 1500
# 새 시나리오의 결정성 검사를 도는 백엔드 — 앞 단계에 달린 값은 게임 쪽에서 생겨 하나면 드러난다.
DETERMINISM_BACKEND = "dx12"
SINGLE_SCENARIO_TIMEOUT_SECONDS = 900
# 단위 실행의 기준 파일 없음(`SnapshotMissing`)은 시나리오와 같은 이름으로 적는다 — 표의 한 칸이 "기록했으니 보고 커밋할 것" 을 뜻한다.
UNIT_KIND_ALIAS = {"SnapshotMissing": "MissingBaseline"}


@dataclass(frozen=True)
class ChangedFile:
    status: str   # git 의 A(새것) · M(바뀐 것)
    path: str     # 저장소 기준, `/` 구분


@dataclass(frozen=True)
class CtestItem:
    name: str
    command: tuple[str, ...]
    working_directory: str
    timeout_seconds: int = CTEST_DEFAULT_TIMEOUT_SECONDS   # ctest TIMEOUT 속성


@dataclass
class ChangedRun:
    item: str                       # 표의 항목 이름
    files: list[ChangedFile]        # 이 실행을 고른 바뀐 파일
    command: list[str]
    working_directory: str
    output: Path                    # 단위: 보고 JSON, 시나리오: 보고 폴더
    timeout_seconds: int
    is_scenario: bool = False
    result: ProcessResult | None = field(default=None)


@dataclass(frozen=True)
class ChangedRow:
    item: str
    change: str     # 새것 · 바뀐 것
    kind: str       # 실패 종류(통과는 빈 글)
    reason: str
    branch: str


def parse_name_status(text: str) -> list[ChangedFile]:
    """`git diff --name-status --no-renames` — 지운 것은 돌 것이 없어 뺀다. 형식만 바뀐 것(T)은 바뀐 것이다."""
    list_file: list[ChangedFile] = []
    for line in text.splitlines():
        status, _, path = line.partition("\t")
        if status in ("A", "M", "T") and path:
            list_file.append(ChangedFile("A" if status == "A" else "M", path.strip().replace("\\", "/")))
    return list_file


def describe_change(list_file: Sequence[ChangedFile]) -> str:
    return "새것" if any(changed.status == "A" for changed in list_file) else "바뀐 것"


def find_feature_folder(path: str, list_test_file: Sequence[str]) -> str | None:
    """바뀐 파일의 기능 폴더(`/` 로 끝남) — `…/Tests/…` 는 `Tests` 앞, 아니면 그 파일의 폴더. 그 아래 테스트가 없으면 한 단계 위, 그래도 없으면 없음."""
    parts = path.split("/")
    parts = parts[:parts.index("Tests")] if "Tests" in parts else parts[:-1]
    # 한 단계 위까지만, `Source/<층>/<기능>/` 보다 넓게는 올라가지 않는다(층 전체는 전체 실행과 같다) — 먼 테스트는 cpu · gpu-release 에서 처음 돈다(D4 남는 위험).
    for depth in (len(parts), len(parts) - 1):
        if depth < 3:
            break
        folder = "/".join(parts[:depth]) + "/"
        if any(test.startswith(folder) for test in list_test_file):
            return folder
    return None


def select_unit_folders(list_changed: Sequence[ChangedFile], list_test_file: Sequence[str]) -> dict[str, list[ChangedFile]]:
    """기능 폴더 → 그 폴더를 고른 바뀐 파일. 에디터 폴더(`/Editor/`)는 뺀다 — 그 케이스는 `App --test` 에 등록되지 않고 `UnitTests.Editor` 가 돈다."""
    map_folder: dict[str, list[ChangedFile]] = {}
    for changed in list_changed:
        if not changed.path.startswith("Source/") or "/Editor/" in changed.path:
            continue
        if not (changed.path.endswith(SOURCE_SUFFIXES) or "/Snapshots/" in changed.path):
            continue
        folder = find_feature_folder(changed.path, list_test_file)
        if folder is not None:
            map_folder.setdefault(folder, []).append(changed)
    return map_folder


def find_golden_scenario(golden_path: str, list_scenario_file: Sequence[str]) -> str | None:
    """`<폴더>/golden/<시나리오>.<이름>.<백엔드>.ppm`(· 메타 `.golden.json`)의 시나리오(`<폴더>/<시나리오>.scenario.xml`). 이름이 가장 긴 것이 맞다."""
    folder, _, file_name = golden_path.rpartition("/golden/")
    list_match = [scenario for scenario in list_scenario_file
                  if scenario.rpartition("/")[0] == folder and file_name.startswith(scenario.rpartition("/")[2][: -len(SCENARIO_SUFFIX)] + ".")]
    return max(list_match, key=len) if list_match else None


def select_scenarios(list_changed: Sequence[ChangedFile], list_scenario_file: Sequence[str]) -> dict[str, list[ChangedFile]]:
    """시나리오(Content 기준 리소스 경로) → 그 시나리오를 고른 바뀐 파일(시나리오 파일 · 그 골든)."""
    map_scenario: dict[str, list[ChangedFile]] = {}
    for changed in list_changed:
        if not changed.path.startswith("Content/"):
            continue
        if changed.path.endswith(SCENARIO_SUFFIX):
            scenario: str | None = changed.path
        elif "/golden/" in changed.path:
            scenario = find_golden_scenario(changed.path, list_scenario_file)
        else:
            scenario = None
        if scenario is not None:
            map_scenario.setdefault(scenario[len("Content/"):], []).append(changed)
    return map_scenario


def has_reflected_type(path: str, read_text: Callable[[str], str]) -> bool:
    """리플렉션 형식을 선언하는 헤더 — 에디터 단위 테스트가 그 메타(아이콘 · 색 · 분류)를 데이터 표와 대조한다."""
    return path.startswith("Source/") and path.endswith(".h") and REFLECT_DECLARATION.search(read_text(path)) is not None


def select_editor_files(list_changed: Sequence[ChangedFile], read_text: Callable[[str], str]) -> list[ChangedFile]:
    """에디터 단위 테스트(`UnitTests.Editor`)를 돌게 하는 바뀐 파일 — 에디터 모듈 · 편집기 확장의 소스, 편집기 시나리오, 에디터 데이터, 리플렉션 헤더(어느 층이든)."""
    return [changed for changed in list_changed
            if (changed.path.startswith("Source/") and "/Editor/" in changed.path)
            or has_reflected_type(changed.path, read_text)
            or (changed.path.startswith("Content/") and "/automation/editor/" in changed.path and changed.path.endswith(SCENARIO_SUFFIX))
            or changed.path.startswith("Content/editor/")]


def parse_ctest_items(json_text: str) -> list[CtestItem]:
    """`ctest --show-only=json-v1` 의 항목(이름 · 명령 · 작업 폴더 · 시한). 명령이 없는 항목(꺼 둔 것)은 뺀다."""
    list_item: list[CtestItem] = []
    for test in json.loads(json_text).get("tests", []):
        if not test.get("command"):
            continue
        properties = {prop.get("name"): prop.get("value") for prop in test.get("properties", [])}
        timeout = int(float(properties.get("TIMEOUT", CTEST_DEFAULT_TIMEOUT_SECONDS)))
        list_item.append(CtestItem(test["name"], tuple(test["command"]), properties.get("WORKING_DIRECTORY", ""), timeout))
    return list_item


def matches_argument(token: str, key: str) -> bool:
    """`-key` · `--key` · `-key=값` · `--key=값`."""
    if not token.startswith("-"):
        return False
    bare = token.lstrip("-")
    return bare == key or bare.startswith(key + "=")


def get_argument(command: Sequence[str], key: str) -> str | None:
    for token in command:
        if matches_argument(token, key):
            return token.partition("=")[2]
    return None


def set_argument(command: Sequence[str], key: str, value: str | None, dash: str = "-") -> list[str]:
    """@p key 를 모두 빼고, 값이 있으면 `<dash>key=값` 을 끝에 둔다."""
    list_token = [token for token in command if not matches_argument(token, key)]
    if value is not None:
        list_token.append(f"{dash}{key}={value}")
    return list_token


def find_dash(command: Sequence[str], key: str) -> str:
    """그 실행 파일이 쓰는 앞머리 — `App --test` 는 `--`, 봇의 엔진 인자(`-test`)는 `-`."""
    return "--" if any(token == f"--{key}" or token.startswith(f"--{key}=") for token in command) else "-"


def make_unit_runs(list_item: Sequence[CtestItem], map_folder: dict[str, list[ChangedFile]], output: Path) -> list[ChangedRun]:
    """
    기능 폴더를 그 폴더를 싣는 단위 항목에 나눈다 — `--test-source` 가 있는 항목(게임 · 온라인 키트)은 제 폴더 아래 것만, 나머지는 `--test-source` 가 없는 항목
    (엔진 · 활성 게임)이 돈다. 거르개 항목(`UnitTests.OtherWorkingFolder`)은 뺀다. 몫으로 나눈 항목(`UnitTests.<i>`)은 첫 몫 하나에서 몫 인자를 빼고 돈다.
    """
    list_unit: list[CtestItem] = []
    for item in list_item:
        if not UNIT_ITEM_PATTERN.match(item.name) or item.name == EDITOR_UNIT_TEST_ITEM or get_argument(item.command, "test-filter") is not None:
            continue
        shard = get_argument(item.command, "test-shard")
        if shard is not None and not shard.startswith("0/"):
            continue
        list_unit.append(item)
    list_sourced = [(item, [folder for folder in (get_argument(item.command, "test-source") or "").split(",") if folder]) for item in list_unit]
    list_general = [item for item, list_owned in list_sourced if not list_owned]
    map_item_folder: dict[str, list[str]] = {}
    for folder in map_folder:
        list_owner = [item for item, list_owned in list_sourced if list_owned and any(folder.startswith(owned) for owned in list_owned)]
        for item in list_owner or list_general:
            map_item_folder.setdefault(item.name, []).append(folder)
    list_run: list[ChangedRun] = []
    for item in list_unit:
        list_folder = map_item_folder.get(item.name)
        if not list_folder:
            continue
        name = "UnitTests" if get_argument(item.command, "test-shard") is not None else item.name
        dash = find_dash(item.command, "test")
        command = set_argument(item.command, "test-source", ",".join(list_folder), dash)
        command = set_argument(command, "test-shard", None)
        command = set_argument(command, "test-report", str(output / f"{name}.json"), dash)
        command = set_argument(command, "log-file", str(output / f"{name}.log"))
        command.append(f"{dash}record-missing-snapshots")
        list_changed = [changed for folder in list_folder for changed in map_folder[folder]]
        list_run.append(ChangedRun(name, list_changed, command, item.working_directory, output / f"{name}.json", item.timeout_seconds))
    return list_run


def make_scenario_command(item: CtestItem, scenario: str, output: Path) -> list[str]:
    """체인 항목의 명령을 시나리오 하나로 — 나누기 · 이어 가기를 빼 모든 백엔드에서 혼자 돈다. `<Launch>` 값은 App 이 시나리오에서 읽는다."""
    command = set_argument(item.command, "scenario", scenario)
    command = set_argument(command, "scenario-backend-split", None)
    command = set_argument(command, "scenario-backend-split-offset", None)
    command = set_argument(command, "scenario-keep-going", None)
    command = set_argument(command, "log-file", str(output / "parent.log"))
    # 셰이더 요청 기록은 체인 전체의 것이다 — 시나리오 하나로 덮으면 다음 검증의 데우기(preflight shader warm)가 그 몫만 컴파일한다.
    command = set_argument(command, "shader-request-log", None)
    command = set_argument(command, "scenario-report", str(output / (scenario.rpartition("/")[2][: -len(SCENARIO_SUFFIX)] + ".json")))
    command.append("-scenario-record-missing-golden")
    return command


def read_manual_scenarios(project_root: Path) -> dict[str, str]:
    """ctest 가 띄울 수 없는 시나리오 → 까닭과 명령(`Config/Rules/TestSelection/ManualScenarios.json`)."""
    table = json.loads((project_root / "Config" / "Rules" / "TestSelection" / "ManualScenarios.json").read_text(encoding="utf-8"))
    return {entry["path"]: f"손으로 돈다: {entry['reason']} — `{entry['command']}`" for entry in table["_scenarios"]}


def make_scenario_runs(list_item: Sequence[CtestItem], map_scenario: dict[str, list[ChangedFile]], build_dir: Path,
                       output: Path, find_branch: Callable[[str], str],
                       map_quarantine: dict[str, QuarantineEntry] | None = None, map_net: dict[str, str] | None = None,
                       map_manual: dict[str, str] | None = None) -> tuple[list[ChangedRun], list[ChangedRow]]:
    """
    시나리오마다 실행 하나. 두 번 기동 쌍 · 여러 프로세스 시나리오(@p map_net — 시나리오 → ctest 이름)는 ctest 항목 그대로(기록 없이) 돈다.
    항목이 띄울 수 없는 시나리오(@p map_manual — 시나리오 → 까닭과 명령)는 표에 그렇다고 적고, 어느 쪽도 아니면 실패다(아무도 돌리지 않는 시나리오다).
    """
    list_run: list[ChangedRun] = []
    list_row: list[ChangedRow] = []
    for scenario, list_changed in sorted(map_scenario.items()):
        folder = output / "scenario" / scenario.replace("/", ".")
        item = next((item for item in list_item if scenario in (get_argument(item.command, "scenario") or "").split(",")), None)
        quarantine = (map_quarantine or {}).get(item.name) if item is not None else None
        if quarantine is not None:
            # 격리한 ctest 의 시나리오는 묶음 검증이 빼고 돈다(gpu 단계의 `-E` 와 같은 표) — 표에 사유 · 만료를 남기고 돌리지 않는다.
            list_row.append(ChangedRow(scenario, describe_change(list_changed), "", f"격리 `{item.name}` — {quarantine.reason} (만료 {quarantine.expires.isoformat()})",
                                       find_branch(list_changed[0].path)))
            continue
        if item is not None:
            alone = len((get_argument(item.command, "scenario") or "").split(",")) == 1   # 시나리오 하나짜리 항목(부하 등)은 제 시한 그대로다
            list_run.append(ChangedRun(scenario, list_changed, make_scenario_command(item, scenario, folder), item.working_directory, folder,
                                       item.timeout_seconds if alone else SINGLE_SCENARIO_TIMEOUT_SECONDS, is_scenario=True))
            continue
        pair = PAIR_SUFFIX.sub("", scenario)
        pair_item = next((item for item in list_item if f"-DPAIR={pair}" in item.command), None) if pair != scenario else None
        if pair_item is not None:
            list_run.append(ChangedRun(scenario, list_changed, ["ctest", "-R", f"^{re.escape(pair_item.name)}$", "--output-on-failure"], str(build_dir),
                                       folder, pair_item.timeout_seconds))
            continue
        net_item = next((item for item in list_item if item.name == (map_net or {}).get(scenario)), None)
        if net_item is not None:
            if all(run.item != net_item.name for run in list_run):   # 한 항목이 여러 시나리오를 띄운다 — 한 번만 돈다
                list_run.append(ChangedRun(net_item.name, list_changed, ["ctest", "-R", f"^{re.escape(net_item.name)}$", "--output-on-failure"], str(build_dir),
                                           folder, net_item.timeout_seconds))
            continue
        manual = (map_manual or {}).get(scenario)
        list_row.append(ChangedRow(scenario, describe_change(list_changed), "" if manual else "NotRun", manual or "어느 ctest 항목도 띄우지 않는다 — 항목을 만들거나 Config/Rules/TestSelection/ManualScenarios.json 에 까닭을 적는다",
                                   find_branch(list_changed[0].path)))
    return list_run, list_row


def make_editor_unit_run(list_item: Sequence[CtestItem], list_editor_file: list[ChangedFile], output: Path) -> ChangedRun | None:
    """에디터 쪽이 바뀌었으면 에디터 단위 테스트(`UnitTests.Editor`)를 통째로 도는 실행(13 초 안팎이라 좁히지 않는다)."""
    if not list_editor_file:
        return None
    item = next((item for item in list_item if item.name == EDITOR_UNIT_TEST_ITEM), None)
    if item is None:
        raise RuntimeError(f"ctest 에 {EDITOR_UNIT_TEST_ITEM} 이 없다 - 에디터 단위 테스트를 돌 수 없다")
    dash = find_dash(item.command, "test-editor")
    command = set_argument(list(item.command), "test-report", str(output / f"{item.name}.json"), dash)
    command = set_argument(command, "log-file", str(output / f"{item.name}.log"))
    command.append(f"{dash}record-missing-snapshots")
    return ChangedRun(item.name, list_editor_file, command, item.working_directory, output / f"{item.name}.json", item.timeout_seconds)


def get_last_result_line(text: str, prefix: str) -> str:
    list_line = [line.strip() for line in text.splitlines() if line.strip().startswith(prefix)]
    return list_line[-1] if list_line else ""


def make_one_line(text: str, limit: int = 200) -> str:
    line = text.strip().splitlines()[0] if text.strip() else ""
    return line if len(line) <= limit else line[: limit - 1] + "…"


def read_unit_rows(run: ChangedRun, map_changed: dict[str, ChangedFile], find_branch: Callable[[str], str]) -> list[ChangedRow]:
    """단위 실행 하나의 줄 — 진 케이스마다 한 줄(케이스 파일이 바뀐 파일이면 그 파일의 새것/바뀐 것 · 갈래), 보고가 없으면 끝 줄로 한 줄, 다 통과면 한 줄."""
    result = run.result or ProcessResult(return_code=-1, launched=False)
    first_branch = find_branch(run.files[0].path)
    try:
        report = json.loads(run.output.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        report = None
    list_row: list[ChangedRow] = []
    for case in (report or {}).get("cases", []):
        if case.get("passed") or case.get("skipped"):
            continue
        match = SOURCE_PATH.search(str(case.get("file", "")).replace("\\", "/"))
        changed = map_changed.get(match.group(1)) if match else None
        kind = UNIT_KIND_ALIAS.get(case.get("failureKind", ""), case.get("failureKind", "")) or "Assertion"
        list_row.append(ChangedRow(f"{run.item}: {case['name']}", "새것" if changed and changed.status == "A" else "바뀐 것", kind,
                                   make_one_line((case.get("failures") or [""])[0]), find_branch(changed.path) if changed else first_branch))
    if list_row:
        return list_row
    end_line = get_last_result_line(result.stdout, "TEST_RESULT=")
    if result.succeeded and report is not None:
        return [ChangedRow(run.item, describe_change(run.files), "", f"통과 — {report.get('runCount', 0)} 케이스", first_branch)]
    if result.timed_out:
        kind = "Timeout"
    elif end_line:
        kind = end_line.partition("=")[2].partition(" ")[0]
    else:
        kind = "Crash"
    reason = end_line or make_one_line(result.stderr) or f"종료 코드 {result.return_code}, 끝 줄 없음 — {run.output.with_suffix('.log')}"
    return [ChangedRow(run.item, describe_change(run.files), kind, reason, first_branch)]


def fold_scenario_failures(list_report: Sequence[dict]) -> list[tuple[str, str, str]]:
    """(종류, 까닭, 묶음) — 같은 (종류 · 단계)로 진 백엔드를 접는다. 돈 백엔드가 둘 이상이고 모두 같이 졌으면 `전제`(시나리오의 가정이 틀렸다), 하나만 졌으면 `백엔드 <이름>`."""
    list_ran = [report for report in list_report if report.get("result") != "Skipped"]
    groups: dict[tuple[str, str], list[dict]] = {}
    for report in list_ran:
        if report.get("result") in PASSED_RESULTS:
            continue
        failure = report.get("failure") or {}
        groups.setdefault((failure.get("kind") or report.get("result", ""), failure.get("step", "")), []).append(report)
    list_fold: list[tuple[str, str, str]] = []
    for (kind, _), group in groups.items():
        list_backend = sorted(str(report.get("backend", "")) for report in group)
        if len(list_ran) > 1 and len(group) == len(list_ran):
            label = "전제"
        elif len(list_ran) > 1 and len(group) == 1:
            label = f"백엔드 {list_backend[0]}"
        else:
            label = ",".join(list_backend)
        failure = group[0].get("failure") or {}
        list_fold.append((kind, make_one_line(failure.get("reason") or group[0].get("reason", "")), label))
    return list_fold


def read_scenario_rows(run: ChangedRun, find_branch: Callable[[str], str]) -> list[ChangedRow]:
    """시나리오 실행 하나의 줄 — 백엔드 자식 보고(`<이름>.<백엔드>.json`)를 접어 낸다. 보고가 없으면 끝 줄로 한 줄."""
    result = run.result or ProcessResult(return_code=-1, launched=False)
    change, branch = describe_change(run.files), find_branch(run.files[0].path)
    if not run.is_scenario:   # 두 번 기동 쌍 — ctest 의 성패만
        if result.succeeded:
            return [ChangedRow(run.item, change, "", "통과(ctest 쌍)", branch)]
        return [ChangedRow(run.item, change, "Assertion", "ctest 쌍이 졌다 — Testing/Logs 의 그 항목 로그", branch)]
    by_backend: dict[str, dict] = {}
    for report in sorted(read_reports([run.output]) if run.output.is_dir() else [], key=lambda report: report["startedAtUnixMs"]):
        by_backend[str(report.get("backend", ""))] = report   # 같은 백엔드는 마지막 것
    list_report = list(by_backend.values())
    if not list_report:
        end_line = get_last_result_line(result.stdout, "SCENARIO_RESULT=")
        kind = "Hang" if result.timed_out else "LoadError"
        return [ChangedRow(run.item, change, kind, end_line or f"보고가 없다(종료 코드 {result.return_code}) — {run.output / 'parent.log'}", branch)]
    return fold_scenario_rows(run.item, change, branch, by_backend)


def fold_scenario_rows(item: str, change: str, branch: str, by_backend: dict[str, dict]) -> list[ChangedRow]:
    """백엔드별 보고를 표 줄로 — 모든 백엔드가 건너뛰었으면 `NotRun`(새 · 바뀐 시나리오가 한 번도 돌지 않았다 — 통과로 적으면 거짓 통과다), 일부만이면 통과 줄에 건너뛴 백엔드를 적는다."""
    list_report = list(by_backend.values())
    list_skipped = sorted(backend for backend, report in by_backend.items() if report.get("result") == "Skipped")
    if len(list_skipped) == len(list_report):
        return [ChangedRow(item, change, "NotRun", f"모든 백엔드가 건너뛰었다 — {make_one_line(list_report[0].get('reason', ''))}", branch)]
    list_fold = fold_scenario_failures(list_report)
    if not list_fold:
        list_ran = sorted(backend for backend in by_backend if backend not in list_skipped)
        skipped_note = f" (건너뜀: {','.join(list_skipped)})" if list_skipped else ""
        return [ChangedRow(item, change, "", "통과 — " + ",".join(list_ran) + skipped_note, branch)]
    return [ChangedRow(item, change, kind, f"[{label}] {reason}", branch) for kind, reason, label in list_fold]


def parse_branch(subject: str) -> str:
    """first-parent 커밋 제목 → 갈래 이름. 병합이 아니면 직접 커밋이다."""
    match = MERGE_SUBJECT.match(subject)
    if match:
        return match.group(1)
    return f"(직접) {make_one_line(subject, 40)}" if subject else "?"


def make_find_branch(project_root: Path, base: str) -> Callable[[str], str]:
    """파일 → 그 파일을 마지막으로 더한 갈래(`<base>..HEAD` 의 first-parent 커밋 — 병합이면 그 갈래 이름)."""
    cache: dict[str, str] = {}

    def find(path: str) -> str:
        if path not in cache:
            result = run_git(["log", "--first-parent", "--format=%s", "-1", f"{base}..HEAD", "--", path], cwd=project_root)
            cache[path] = parse_branch(result.stdout.strip()) if result.succeeded else "?"
        return cache[path]
    return find


def format_changed_table(list_row: Sequence[ChangedRow]) -> list[str]:
    list_line = ["| 항목 | 새것/바뀐 것 | 실패 종류 | 까닭 | 갈래 |", "| --- | --- | --- | --- | --- |"]
    for row in list_row:
        cells = (row.item, row.change, row.kind or "-", row.reason, row.branch)
        list_line.append("| " + " | ".join(cell.replace("|", "\\|") for cell in cells) + " |")
    return list_line


def list_new_references(status_text: str) -> list[str]:
    """`git status --porcelain --untracked-files=all` 의 추적 안 된 골든 · 스냅숏 — 이 단계가 처음 쓴 기준 파일이다(앞 실행이 남긴 것도 아직 사람이 보지 않았다)."""
    return sorted(line[3:].strip() for line in status_text.splitlines()
                  if line.startswith("?? ") and ("/golden/" in line or "/Snapshots/" in line))


def decide_changed(list_row: Sequence[ChangedRow], list_recorded: Sequence[str], sha: str) -> str:
    return "CHANGED_FAILED" if list_recorded or any(row.kind for row in list_row) else f"CHANGED_OK={sha}"


def run_new_scenario_determinism(list_item: Sequence[CtestItem], map_scenario: dict[str, list[ChangedFile]], content_root: Path, build_dir: Path,
                                 find_branch: Callable[[str], str], map_quarantine: dict[str, QuarantineEntry]) -> list[ChangedRow]:
    """새 시나리오마다 혼자 · 앞 단계 하나 · 두 번 이어 돌려 사진을 바이트로 견준 줄(사진을 찍지 않는 시나리오는 통과 줄).

    바뀐 시나리오는 빼고 **새것만**, 백엔드 하나에서 돈다 — 앞 단계에 달린 값은 게임 쪽에서 생겨 백엔드와 무관하고, 시나리오 하나에 기동 셋 · 단계 다섯이 든다.
    나머지 시나리오는 체인이 위치를 섞어 돌므로 골든이 본다."""
    from dev.scenario_determinism import check_determinism, make_runner
    from dev.scenario_runs import read_scenario_name
    list_row: list[ChangedRow] = []
    for scenario, list_changed in sorted(map_scenario.items()):
        is_new = any(changed.status == "A" and changed.path == f"Content/{scenario}" for changed in list_changed)
        item = next((item for item in list_item if scenario in (get_argument(item.command, "scenario") or "").split(",")), None)
        has_photo = "<Screenshot" in (content_root / scenario).read_text(encoding="utf-8") or "<ExpectImage" in (content_root / scenario).read_text(encoding="utf-8")
        if not is_new or item is None or not has_photo or item.name in map_quarantine:
            continue
        list_path = (get_argument(item.command, "scenario") or "").split(",")
        name = read_scenario_name(scenario)
        working = Path(item.working_directory) if item.working_directory else build_dir
        runner = make_runner(item.command, name, DETERMINISM_BACKEND, build_dir, working)
        print(f"[Changed] run {scenario}: determinism ({DETERMINISM_BACKEND})", flush=True)
        list_problem = check_determinism(list_path, list_path.index(scenario), name, working / "Saved" / "Automation", runner)
        list_row.append(ChangedRow(f"{scenario} (결정성)", describe_change(list_changed), "Determinism" if list_problem else "",
                                   "; ".join(list_problem) or "통과", find_branch(list_changed[0].path)))
    return list_row


def run_changed(project_root: Path, preset: str, base: str) -> int:
    build_dir = get_build_dir(preset, project_root)
    if not build_dir.is_dir():
        print(f"[Changed] {build_dir} 가 없다 — 빌드 뒤에 돈다")
        print("CHANGED_FAILED")
        return ExitCode.CHECK_INVALID
    sha = run_git_or_raise(["rev-parse", "HEAD"], cwd=project_root).strip()
    list_changed = parse_name_status(run_git_or_raise(["diff", "--name-status", "--no-renames", f"{base}...HEAD"], cwd=project_root))
    list_test_file = [path for path in run_git_or_raise(["ls-files", "--", "Source"], cwd=project_root).splitlines() if "/Tests/" in path and path.endswith(".cpp")]
    list_content_file = run_git_or_raise(["ls-files", "--", "Content"], cwd=project_root).splitlines()
    list_scenario_file = [path for path in list_content_file if path.endswith(SCENARIO_SUFFIX)]
    map_folder = select_unit_folders(list_changed, list_test_file)
    map_scenario = select_scenarios(list_changed, list_scenario_file)
    # 시나리오 · 골든이 그대로여도 그 시나리오가 여는 에셋 · 쓰는 감시 값이 바뀌었으면 돈다(scenario_reach).
    map_reached, list_reach_line = select_reached(read_reach(project_root, [changed.path for changed in list_changed], list_content_file), set(map_scenario),
                                                  int(read_verify_config(project_root, "_changedReachPerFile")), int(read_verify_config(project_root, "_changedReachTotal")))
    for line in list_reach_line:
        print(f"[Changed] {line}")
    map_changed_file = {changed.path: changed for changed in list_changed}
    for scenario, list_path in map_reached.items():
        map_scenario[scenario] = [map_changed_file.get(path) or map_changed_file[f"Content/{path}"] for path in list_path]
    list_editor_file = select_editor_files(list_changed, lambda path: (project_root / path).read_text(encoding="utf-8", errors="replace"))
    print(f"[Changed] {base}...HEAD: 바뀐 파일 {len(list_changed)} · 기능 폴더 {len(map_folder)} · 시나리오 {len(map_scenario)}(닿음 {len(map_reached)}) · 에디터 파일 {len(list_editor_file)}")
    if not map_folder and not map_scenario and not list_editor_file:
        print(f"CHANGED_OK={sha}")
        return ExitCode.PASSED
    # ctest 항목도 이 빌드가 정한다(구성이 CMake 파일의 바뀜을 다시 읽는다). 묶음 검증은 앞 단계가 이미 지어 몇 초다.
    try:
        build_preset_or_exit(preset, cwd=project_root)
    except SystemExit as problem:
        print(f"[Changed] {problem}")
        print("CHANGED_FAILED")
        return ExitCode.VIOLATION
    shown = run_process(["ctest", "--show-only=json-v1"], cwd=build_dir)
    if not shown.succeeded:
        print(f"[Changed] ctest 항목을 읽지 못했다: {make_one_line(shown.stderr or shown.stdout)}")
        print("CHANGED_FAILED")
        return ExitCode.VIOLATION
    list_item = parse_ctest_items(shown.stdout)
    output = build_dir / CHANGED_OUTPUT
    shutil.rmtree(output, ignore_errors=True)
    output.mkdir(parents=True)
    find_branch = make_find_branch(project_root, base)
    list_unit_run = make_unit_runs(list_item, map_folder, output)
    list_quarantine, list_quarantine_problem = read_quarantine(project_root, datetime.date.today())
    if list_quarantine_problem:
        for problem in list_quarantine_problem:
            print(f"[Changed] {problem}")
        print("CHANGED_FAILED")
        return ExitCode.VIOLATION
    map_quarantine = {entry.test: entry for entry in list_quarantine}
    map_net = {scenario: item.test_name for item in read_net_items() for scenario in item.scenarios}
    list_scenario_run, list_row = make_scenario_runs(list_item, map_scenario, build_dir, output, find_branch, map_quarantine, map_net,
                                                     read_manual_scenarios(project_root))
    editor_unit_run = make_editor_unit_run(list_item, list_editor_file, output)
    if editor_unit_run is not None:
        list_unit_run.append(editor_unit_run)   # 단위와 함께 — 시나리오 읽기 오류 · 없는 이름을 시나리오보다 먼저 알린다

    def execute(run: ChangedRun) -> None:
        print(f"[Changed] run {run.item}: {' '.join(run.command)}", flush=True)   # `run ` 머리 — preflight.sh 가 이 줄을 빼고 표만 찍는다
        working = Path(run.working_directory) if run.working_directory else build_dir
        run.result = run_process(run.command, cwd=working, timeout_seconds=run.timeout_seconds)

    # 단위는 CPU 일이라 함께, 시나리오는 시간 · 그림을 판정하므로 하나씩(묶음 검증의 gpu 단계를 혼자 도는 것과 같은 까닭).
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, len(list_unit_run))) as pool:
        list(pool.map(execute, list_unit_run))
    for run in list_scenario_run:
        execute(run)
    list_row += run_new_scenario_determinism(list_item, map_scenario, project_root / "Content", build_dir, find_branch, map_quarantine)
    map_changed = {changed.path: changed for changed in list_changed}
    for run in list_unit_run:
        list_row += read_unit_rows(run, map_changed, find_branch)
    for run in list_scenario_run:
        list_row += read_scenario_rows(run, find_branch)
    for line in format_changed_table(list_row):
        print(line)
    list_recorded = list_new_references(run_git_or_raise(["status", "--porcelain", "--untracked-files=all", "--", "Content", "Source"], cwd=project_root))
    if list_recorded:
        print(f"[Changed] 처음 쓴 기준 파일 {len(list_recorded)} 개 — 그림 · 글을 보고 커밋한 뒤 `preflight.sh --from changed` 로 기록 없이 통과시킨다:")
        for path in list_recorded:
            print(f"  {path}")
    final = decide_changed(list_row, list_recorded, sha)
    print(final)
    return ExitCode.PASSED if final.startswith("CHANGED_OK=") else ExitCode.VIOLATION
