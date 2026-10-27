"""
검증 묶음 매니페스트 — 통합에서 돌릴 모든 확인(빌드 · ctest · 테스트 필터 · 쿠킹 · 골든 · 측정)의 형식과 읽기.

**항목의 정본은 데이터(`Test/QA/VerifyBatch.json`)이고 이 모듈은 형식만 가진다.** 확인 대기를 문서마다 글로 적어 두면 통합이 그 글을 찾아
다시 모으고 항목마다 따로 돌리게 된다(측정 후보가 쌓이는 원인). 그래서 대기는 항목 id 로만 적는다 — 계획 문서의 대기 줄은 `매니페스트 id 'xxx'`
한 줄이고, 그 id 가 매니페스트에 없거나(오타 · 지운 항목) 일회성 항목(`pending`)을 가리키는 줄이 하나도 없으면 `plans --check` 와 린트가 막는다.
실행은 `dev/RunVerifyBatch.py`(`py -3 -m Scripts verify-batch`)가 하고, 여기는 읽기 · 검증 · 실행 순서 · 명령 만들기 · 결과 표만 안다.

항목 종류(`kind`)는 여덟이고 실행 순서도 이 순서다(빌드 → 쿠킹 → 검사 → ctest · 테스트 → 골든 → 측정 — 쿠킹한 셰이더를 쓰는 실행이 쿠킹 뒤에 돈다):

- `build`   : `full-build --preset <구성의 프리셋>`. 실패하면 같은 구성의 나머지는 막힌다.
- `ctest`   : `ctest --test-dir build/<프리셋> -L <label>`.
- `test`    : `Scripts test <filter> --preset <프리셋> --no-build <extra_args>`(호스트 스위트 · 벤치는 `--host_suites=all` · `--load_suites=only` 를 extra_args 에).
- `covered` : 다른 항목(`covered_by`)이 도는 동안 같이 도는 시나리오 파일 · 테스트 케이스. 따로 돌리지 않고 덮개 항목의 결과를 따른다.
- `cook` · `check` · `golden` · `measure` : 기존 도구를 `tool`(`scripts` = `py -3 -m Scripts <args>`, `app` = 그 구성의 App 실행 파일)과 `args` 로 부른다.
  `check` 는 빌드한 App 의 헤드리스 검사(`--check-text` 처럼 종료 코드로 판정하고 산출물이 없는 것)다. 측정이 아니라 한계 · 판정 칸이 없다.

명령이 없는 항목은 이유가 둘 중 하나로 갈린다 — 실행기는 어느 쪽이든 돌리지 않고 이유를 표에 적는다(없는 명령을 지어내지 않는다).
`manual` 은 사람 눈 · 귀 · 장비 · 외부 에셋이 있어야 하는 확인이라 명령이 영영 없을 수 있는 것이고, `need_command` 는 사람이 필요 없는데 아직 명령이 없는 것(케이스 · 시나리오를 더하면 `kind` 의 칸으로 도는 것)이다.
**일회성(`pending`) 항목은 명령이 있거나 `manual` 이어야 한다** — 명령 없는 `need_command` 가 일회성이면 통합이 영영 돌릴 수 없는 확인을 기다리게 되므로 읽을 때 막는다.
명령 없는 항목(`need_command` · `manual`)은 어느 문서의 `매니페스트 id` 줄이 가리켜야 한다 — 누가 명령을 만들 것인지, 누가 사람 확인을 할 것인지의 주인 문서(후보 문서 포함)가 있어야 한다.
`platform` 이 `wsl` 인 항목은 사용자가 요청할 때만 하므로 기본으로 돌리지 않는다. 필수 칸이 빠지거나 모르는 칸이 있으면 `VerifyManifestError` 로 멈춘다(기본값 없음).
`gpu` 는 GPU 잠금 방식이다: `none` · `shared`(공유 칸) · `exclusive`(GPU 와 빌드 칸을 혼자 — 측정이 다른 빌드에 오염되지 않게).

선택 칸 셋은 느린 검증을 구조로 줄인다(근거: 한 번에 약 80 분 걸리던 전체 실행의 로그):
- `requires` : 선행 항목 id 목록. 같은 구성에서 **앞서 도는** 돌 수 있는 항목이어야 한다(읽을 때 막는다). 선행이 이번 실행에서 통과하지 못했으면(실패 · 막힘 · 건너뜀)
  이 항목은 돌지 않고 `건너뜀 — 선행 …` 으로 표에 오른다(선행이 남긴 보고를 읽는 항목이 연쇄로 지는 것을 막는다). 선행을 이번에 고르지 않았으면 이전 실행의 결과를 쓴다고 보고 돈다.
- `shards` : 2 이상의 정수, `test` 항목만. 같은 필터를 `--test_shard=<k>/<n>` 으로 n 개 프로세스에 나눠 돌리고(비용표는 정책의 케이스 비용) 결과 하나로 모은다. 겹쳐 돌지는
  그 실행 파일의 정책(`Test/TestPolicy.json` 의 `hostResourceLock`)이 정한다 — 잠금이 있으면 같은 `Bin` 산출물을 나누는 것이라 샤드끼리도 순서대로 돈다.
- `functional` : `measure` 종류인데 병합 검증의 통과 · 실패 판정인 항목(기본 실행에 든다). 칸이 없으면 측정이라 `--measure` 가 있을 때만 돈다.
"""

from __future__ import annotations

import json
import re
import sys
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path

from .AppRun import kBenchRowRe
from .BuildTree import BuildTree
from .ExitCode import ExitCode
from .Plans import listPlanFiles

#: 매니페스트 파일(저장소 기준).
kManifestRelPath = "Test/QA/VerifyBatch.json"

#: 항목 종류 — 실행 순서도 이 순서다.
kListKind: tuple[str, ...] = ("build", "cook", "check", "ctest", "test", "covered", "golden", "measure")
#: GPU 잠금 방식 — 실행 순서에서 이 순서로 모은다(없음 → 공유 → 독점).
kListGpuMode: tuple[str, ...] = ("none", "shared", "exclusive")
#: 병합 검증의 단계 — `--fail-fast` 가 단계 사이에서 멈춘다. 종류 순서와 같고 싼 것이 앞이다: 짓고(빌드) → 쿠킹 → ctest(린트 · 파이썬 · nogpu · race) → 테스트 필터 → 골든 → 측정.
#: 린트(`ctest -L lint`)를 빌드 앞에 두지 않는다 — 린트의 글롭 대조(`CheckSourceGlob` · `CheckModuleTargets`)가 **구성된 빌드 폴더**를 읽어, 빌드(재구성)가 끝나기 전에 돌리면 낡은 구성으로 판정한다.
kListStageKinds: tuple[tuple[str, ...], ...] = (("build",), ("cook",), ("ctest",), ("test", "covered"), ("golden",), ("measure",))
kListPlatform: tuple[str, ...] = ("windows", "wsl")
kListTool: tuple[str, ...] = ("scripts", "app")
kListJudge: tuple[str, ...] = ("exit", "metrics")
#: `args` 안에서 풀어 주는 자리 표시: App 실행 파일 · App 폴더 · 구성의 프리셋 이름 · 이번 실행의 작업 폴더.
kListPlaceholder: tuple[str, ...] = ("app", "bin", "preset", "work")
#: GPU 잠금 방식 → 잡을 자원들 `(자원, 공유 여부)`. 독점은 측정이므로 빌드 칸도 막는다(측정 중 다른 빌드가 CPU 를 쓰면 값이 오염된다).
kMapLockByGpuMode: dict[str, tuple[tuple[str, bool], ...]] = {
    "none": (),
    "shared": (("gpu", True),),
    "exclusive": (("gpu", False), ("build", False)),
}

_kIdRe = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")
_kPlaceholderRe = re.compile(r"\{([a-z]+)\}")
_kPendingMarkerRe = re.compile(r"확인 대기|실행 대기|통합에서 확인|통합에서 실행|\*\*남은 확인")
_kBacktickIdRe = re.compile(r"`([a-z0-9][a-z0-9-]*)`")
#: `매니페스트 id` 뒤에 이어진 백틱 id 목록(쉼표 · 가운뎃점으로 이음). 목록이 끝나는 곳에서 멈춘다.
_kIdListRe = re.compile(r"매니페스트 id\s*((?:`[a-z0-9][a-z0-9-]*`\s*[,·]?\s*)+)")
_kFenceRe = re.compile(r"^\s*(```|~~~)")
_kSuiteCaseRe = re.compile(r"^([A-Za-z]\w*)\.([\w*]+)$")
_kWordRe = re.compile(r"\w+")
#: 다른 항목이 남긴 시나리오 보고 폴더(ctest 샤드의 실행 쓰기 루트, 보고는 `AppRuns/shard<k>of<n>/<케이스>/Saved/Automation` 아래)를 읽는 args — 이런 항목은 그 보고를 만드는 항목을 `requires` 로 적어야 한다.
kReportFolderText = "build/{preset}/AppRuns"
_kReportFolderRe = re.compile(re.escape(kReportFolderText))
# 뒤따르는 낱말은 먼저 보기(lookahead)로만 읽어, `a, b, c` 에서 (a,b) 와 (b,c) 가 둘 다 잡힌다.
_kWordCommaRe = re.compile(r"\b(\w+)\s*,\s*(?=(\w+))")

_kCommonKeys: tuple[str, ...] = ("id", "title", "kind", "config", "platform", "gpu", "source", "result_doc", "pending")
_kMapKindKeys: dict[str, tuple[str, ...]] = {
    "build": (),
    "ctest": ("label",),
    "test": ("filter",),
    "covered": ("covered", "covered_by"),
    "cook": ("tool", "args"),
    "check": ("tool", "args"),
    "golden": ("tool", "args"),
    "measure": ("tool", "args", "limit", "judge"),
}
_kOptionalKeys: tuple[str, ...] = ("need_command", "manual", "note", "extra_args", "metrics", "timeout_minutes", "requires", "shards", "functional")
#: `shards` 의 상한 — 샤드마다 App 을 띄우는 프로세스라 코어 · GPU 칸(`gpuSlots`)을 넘게 쪼개는 것은 시작 비용만 더한다.
kMaxShardCount = 16
_kMetricKeys: tuple[str, ...] = ("name", "pattern", "unit", "max", "min")


class VerifyManifestError(ValueError):
    """매니페스트가 모양이 틀렸거나 한 명령으로 만들 수 없다. 메시지는 `위치: 내용` 이다."""


@dataclass(frozen=True)
class VerifyMetric:
    """측정 지표 하나 — `pattern` 의 첫 묶음 숫자(출력에서 마지막으로 맞는 줄). `max` · `min` 이 있으면 판정에 쓰고 없으면 보고만 한다."""

    name: str
    pattern: str
    unit: str
    maxValue: float | None
    minValue: float | None


@dataclass(frozen=True)
class VerifyConfig:
    """빌드 구성 — 이름(Debug · Shipping · Release)과 프리셋. 목록 순서가 실행 순서다."""

    name: str
    preset: str


@dataclass(frozen=True)
class VerifyItem:
    """항목 하나. 종류에 없는 칸은 빈 글 · 빈 튜플이다."""

    itemId: str
    title: str
    kind: str
    config: str
    platform: str
    gpu: str
    source: str
    resultDoc: str
    bPending: bool
    index: int
    needCommand: str
    manual: str
    note: str
    timeoutMinutes: int
    label: str
    testFilter: str
    listExtraArg: tuple[str, ...]
    tool: str
    listArg: tuple[str, ...]
    limit: str
    judge: str
    listMetric: tuple[VerifyMetric, ...]
    listCovered: tuple[str, ...]
    coveredBy: str
    listRequires: tuple[str, ...]
    shardCount: int
    bFunctional: bool

    @property
    def bIncludedByDefault(self) -> bool:
        """병합 검증의 기본 실행에 드는가 — 측정(`measure`)은 `functional` 이 아니면 `--measure` 가 있을 때만 돈다(성능에 닿는 변경에서만 부른다)."""
        return self.kind != "measure" or self.bFunctional

    @property
    def bCovered(self) -> bool:
        """따로 돌지 않고 덮개 항목의 결과를 따르는가."""
        return self.kind == "covered"

    @property
    def bUnrunnable(self) -> bool:
        """명령이 없는 항목인가 — 사람이 해야 하거나(`manual`) 아직 명령을 못 만든(`need_command`) 것."""
        return bool(self.needCommand or self.manual)

    @property
    def bRunnable(self) -> bool:
        """이번 실행기가 돌릴 수 있는가 — 한 명령으로 도는 항목이고 Windows 쪽이다."""
        return not self.bCovered and not self.bUnrunnable and self.platform == "windows"


@dataclass(frozen=True)
class VerifyManifest:
    """읽은 매니페스트 — 구성 목록(실행 순서)과 항목 목록(파일 순서)."""

    listConfig: tuple[VerifyConfig, ...]
    listItem: tuple[VerifyItem, ...]

    def findItem(self, itemId: str) -> VerifyItem | None:
        return next((item for item in self.listItem if item.itemId == itemId), None)

    def findConfig(self, name: str) -> VerifyConfig:
        for config in self.listConfig:
            if config.name == name:
                return config
        raise VerifyManifestError(f"구성 `{name}` 이 없습니다")


# ------------------------------------------------------------------------------
# 읽기
# ------------------------------------------------------------------------------
def requireTextInternal(entry: dict, key: str, where: str) -> str:
    value = entry.get(key)
    if not isinstance(value, str) or not value.strip():
        raise VerifyManifestError(f"{where}: `{key}` 가 없거나 빈 글입니다")
    return value


def requireChoiceInternal(entry: dict, key: str, where: str, listChoice: tuple[str, ...]) -> str:
    value = requireTextInternal(entry, key, where)
    if value not in listChoice:
        raise VerifyManifestError(f"{where}: `{key}` 의 `{value}` 는 허용값({' · '.join(listChoice)})이 아닙니다")
    return value


def requireTextListInternal(entry: dict, key: str, where: str, *, bAllowEmpty: bool) -> tuple[str, ...]:
    value = entry.get(key)
    if not isinstance(value, list) or not all(isinstance(part, str) and part for part in value) or (not value and not bAllowEmpty):
        raise VerifyManifestError(f"{where}: `{key}` 가 {'글' if bAllowEmpty else '빈 글이 아닌 글'}의 목록이 아닙니다")
    return tuple(value)


def readMetricInternal(entry: object, where: str) -> VerifyMetric:
    if not isinstance(entry, dict) or set(entry) - set(_kMetricKeys):
        raise VerifyManifestError(f"{where}: 지표는 `{'` · `'.join(_kMetricKeys)}` 만 가진 사전이어야 합니다")
    pattern = requireTextInternal(entry, "pattern", where)
    try:
        if re.compile(pattern).groups < 1:
            raise VerifyManifestError(f"{where}: `pattern` 에 숫자를 뽑을 묶음 `( )` 이 없습니다")
    except re.error as error:
        raise VerifyManifestError(f"{where}: `pattern` 이 정규식이 아닙니다: {error}") from error
    listBound = [entry.get("max"), entry.get("min")]
    if any(bound is not None and (isinstance(bound, bool) or not isinstance(bound, (int, float))) for bound in listBound):
        raise VerifyManifestError(f"{where}: `max` · `min` 은 숫자입니다")
    return VerifyMetric(requireTextInternal(entry, "name", where), pattern, requireTextInternal(entry, "unit", where), listBound[0], listBound[1])


def readItemInternal(entry: object, index: int, setConfig: set[str]) -> VerifyItem:
    if not isinstance(entry, dict):
        raise VerifyManifestError(f"{kManifestRelPath}: items[{index}] 가 사전이 아닙니다")
    itemId = requireTextInternal(entry, "id", f"items[{index}]")
    where = f"{kManifestRelPath}: 항목 `{itemId}`"
    if not _kIdRe.match(itemId):
        raise VerifyManifestError(f"{where}: id 는 소문자 · 숫자 · '-' 로 된 이름입니다")
    kind = requireChoiceInternal(entry, "kind", where, kListKind)
    setAllowed = set(_kCommonKeys) | set(_kOptionalKeys) | {key for keys in _kMapKindKeys.values() for key in keys}
    if set(entry) - setAllowed:
        raise VerifyManifestError(f"{where}: 모르는 칸 {sorted(set(entry) - setAllowed)} (오타인지 본다)")
    config = requireTextInternal(entry, "config", where)
    if config not in setConfig:
        raise VerifyManifestError(f"{where}: 구성 `{config}` 이 `configs` 에 없습니다")
    bPending = entry.get("pending")
    if not isinstance(bPending, bool):
        raise VerifyManifestError(f"{where}: `pending` 은 true · false 입니다(문서의 대기 줄이 가리키는 일회성 확인이면 true)")
    needCommand = str(entry["need_command"]).strip() if "need_command" in entry else ""
    if "need_command" in entry and not needCommand:
        raise VerifyManifestError(f"{where}: `need_command` 는 이유가 든 글입니다")
    if bPending and needCommand:
        raise VerifyManifestError(f"{where}: 일회성(`pending`) 항목은 명령이 있거나 `manual` 이어야 합니다 — 명령이 아직 없으면 `pending` 을 false 로 두고 명령을 만드는 일을 후보 문서가 맡게 합니다")
    manual = str(entry["manual"]).strip() if "manual" in entry else ""
    if "manual" in entry and not manual:
        raise VerifyManifestError(f"{where}: `manual` 은 사람이 해야 하는 까닭이 든 글입니다")
    if needCommand and manual:
        raise VerifyManifestError(f"{where}: `need_command`(사람이 필요 없는데 명령이 아직 없다)와 `manual`(사람이 해야 한다)은 함께 쓰지 않는다")
    bUnrunnable = bool(needCommand or manual)
    listMissing = [key for key in _kMapKindKeys[kind] if key not in entry]
    if listMissing and not bUnrunnable:
        raise VerifyManifestError(f"{where}: 종류 `{kind}` 에 필요한 칸 {listMissing} 가 없습니다(명령이 없는 확인이면 사람이 해야 할 때 `manual`, 아직 명령을 못 만들었으면 `need_command` 에 이유를 적는다)")
    timeoutMinutes = entry.get("timeout_minutes", 0)
    bNeedsTimeout = kind != "covered" and not bUnrunnable
    if isinstance(timeoutMinutes, bool) or not isinstance(timeoutMinutes, int) or (bNeedsTimeout and timeoutMinutes <= 0):
        raise VerifyManifestError(f"{where}: 도는 항목에는 양의 정수 `timeout_minutes` 가 필요합니다")
    judge = requireChoiceInternal(entry, "judge", where, kListJudge) if "judge" in entry else ""
    listMetric = tuple(readMetricInternal(metric, f"{where} 지표") for metric in entry.get("metrics", []))
    if judge == "metrics" and not all(metric.maxValue is not None or metric.minValue is not None for metric in listMetric):
        raise VerifyManifestError(f"{where}: `judge` 가 metrics 면 모든 지표에 `max` 나 `min` 이 있어야 합니다")
    if judge == "metrics" and not listMetric:
        raise VerifyManifestError(f"{where}: `judge` 가 metrics 면 `metrics` 가 비어 있을 수 없습니다")
    shardCount = entry.get("shards", 1)
    if "shards" in entry and (isinstance(shardCount, bool) or not isinstance(shardCount, int) or not 2 <= shardCount <= kMaxShardCount or kind != "test" or bUnrunnable):
        raise VerifyManifestError(f"{where}: `shards` 는 2 이상 {kMaxShardCount} 이하의 정수이고 한 명령으로 도는 `test` 항목에만 둡니다")
    bFunctional = entry.get("functional", False)
    if "functional" in entry and (not isinstance(bFunctional, bool) or kind != "measure"):
        raise VerifyManifestError(f"{where}: `functional` 은 `measure` 항목의 true · false 입니다(통과 · 실패를 판정하는 측정 종류 항목이면 true)")
    listRequires = requireTextListInternal(entry, "requires", where, bAllowEmpty=False) if "requires" in entry else ()
    if listRequires and (kind == "covered" or bUnrunnable or itemId in listRequires or len(set(listRequires)) != len(listRequires)):
        raise VerifyManifestError(f"{where}: `requires` 는 겹치지 않는 다른 항목 id 의 목록이고 따로 도는 항목에만 둡니다(덮개 항목은 덮개 대상의 결과를 따른다)")
    return VerifyItem(
        itemId=itemId, title=requireTextInternal(entry, "title", where), kind=kind, config=config,
        platform=requireChoiceInternal(entry, "platform", where, kListPlatform), gpu=requireChoiceInternal(entry, "gpu", where, kListGpuMode),
        source=requireTextInternal(entry, "source", where), resultDoc=requireTextInternal(entry, "result_doc", where), bPending=bPending,
        index=index, needCommand=needCommand, manual=manual, note=str(entry.get("note", "")), timeoutMinutes=timeoutMinutes,
        label=requireTextInternal(entry, "label", where) if "label" in entry else "",
        testFilter=requireTextInternal(entry, "filter", where) if "filter" in entry else "",
        listExtraArg=requireTextListInternal(entry, "extra_args", where, bAllowEmpty=True) if "extra_args" in entry else (),
        tool=requireChoiceInternal(entry, "tool", where, kListTool) if "tool" in entry else "",
        listArg=requireTextListInternal(entry, "args", where, bAllowEmpty=False) if "args" in entry else (),
        limit=requireTextInternal(entry, "limit", where) if "limit" in entry else "", judge=judge, listMetric=listMetric,
        listCovered=requireTextListInternal(entry, "covered", where, bAllowEmpty=False) if "covered" in entry else (),
        coveredBy=requireTextInternal(entry, "covered_by", where) if "covered_by" in entry else "",
        listRequires=listRequires, shardCount=shardCount, bFunctional=bFunctional)


def getRunOrderKey(item: VerifyItem) -> tuple[int, int, int]:
    """한 구성 안의 돌 순서 키 — `orderItems` 가 정렬하는 (종류, GPU 방식, 파일 순서). 선행은 이 키가 더 작아야 한다."""
    return (kListKind.index(item.kind), kListGpuMode.index(item.gpu), item.index)


def checkRequiresInternal(item: VerifyItem, mapById: dict[str, VerifyItem], where: str) -> None:
    """`requires` 가 같은 구성에서 앞서 도는, 돌 수 있는 항목만 가리키는지 — 뒤에 도는 항목을 선행으로 적으면 선행 결과가 정해지기 전에 판정하게 된다."""
    for requiredId in item.listRequires:
        required = mapById.get(requiredId)
        if required is None:
            raise VerifyManifestError(f"{where}: `requires` 의 `{requiredId}` 가 매니페스트에 없습니다")
        if required.config != item.config or not required.bRunnable:
            raise VerifyManifestError(f"{where}: `requires` 의 `{requiredId}` 는 같은 구성({item.config})에서 도는 항목이어야 합니다(덮개 · 명령 없음 · wsl 항목은 선행이 될 수 없다)")
        if getRunOrderKey(required) >= getRunOrderKey(item):
            raise VerifyManifestError(f"{where}: 선행 `{requiredId}` 가 이 항목보다 앞서 돌지 않습니다(종류 · GPU 방식 · 파일 순서) — 선행을 더 앞에 두거나 종류를 맞춘다")


def checkCrossReferencesInternal(listItem: list[VerifyItem], listConfig: list[VerifyConfig]) -> None:
    """id 가 겹치지 않는지, 덮개 항목이 같은 구성 · 같은 GPU 방식의 `ctest` · `test` 항목을 가리키는지, args 의 자리 표시가 아는 것인지."""
    mapById: dict[str, VerifyItem] = {}
    for item in listItem:
        if item.itemId in mapById:
            raise VerifyManifestError(f"{kManifestRelPath}: id `{item.itemId}` 가 두 번 나옵니다")
        mapById[item.itemId] = item
    for item in listItem:
        where = f"{kManifestRelPath}: 항목 `{item.itemId}`"
        if item.bCovered and not item.bUnrunnable:
            cover = mapById.get(item.coveredBy)
            if cover is None or cover.kind not in ("ctest", "test"):
                raise VerifyManifestError(f"{where}: `covered_by` `{item.coveredBy}` 는 `ctest` · `test` 항목의 id 여야 합니다")
            if (cover.config, cover.gpu) != (item.config, item.gpu):
                raise VerifyManifestError(f"{where}: 구성 · gpu 가 덮개 항목 `{cover.itemId}` 와 같아야 합니다({cover.config} · {cover.gpu})")
        checkRequiresInternal(item, mapById, where)
        for argument in item.listArg:
            for placeholder in _kPlaceholderRe.findall(argument):
                if placeholder not in kListPlaceholder:
                    raise VerifyManifestError(f"{where}: args 의 `{{{placeholder}}}` 는 모르는 자리 표시입니다({' · '.join(kListPlaceholder)})")
    if len({config.preset for config in listConfig}) != len(listConfig):
        raise VerifyManifestError(f"{kManifestRelPath}: 구성의 프리셋이 겹칩니다")


def parseManifest(data: object) -> VerifyManifest:
    """JSON 에서 읽은 값을 매니페스트로 — 모양이 틀리면 `VerifyManifestError`."""
    if not isinstance(data, dict) or not isinstance(data.get("configs"), list) or not isinstance(data.get("items"), list):
        raise VerifyManifestError(f"{kManifestRelPath}: 최상위는 `configs`(목록)와 `items`(목록)를 가진 사전이어야 합니다")
    listConfig: list[VerifyConfig] = []
    for index, entry in enumerate(data["configs"]):
        if not isinstance(entry, dict) or set(entry) != {"name", "preset"}:
            raise VerifyManifestError(f"{kManifestRelPath}: configs[{index}] 는 `name` · `preset` 만 가진 사전이어야 합니다")
        listConfig.append(VerifyConfig(requireTextInternal(entry, "name", f"configs[{index}]"), requireTextInternal(entry, "preset", f"configs[{index}]")))
    if not listConfig or len({config.name for config in listConfig}) != len(listConfig):
        raise VerifyManifestError(f"{kManifestRelPath}: `configs` 가 비었거나 이름이 겹칩니다")
    setConfig = {config.name for config in listConfig}
    listItem = [readItemInternal(entry, index, setConfig) for index, entry in enumerate(data["items"])]
    checkCrossReferencesInternal(listItem, listConfig)
    return VerifyManifest(tuple(listConfig), tuple(listItem))


def readManifest(repositoryRoot: Path) -> VerifyManifest:
    """저장소의 매니페스트를 읽는다. 파일이 없거나 JSON 이 깨졌거나 모양이 틀리면 `VerifyManifestError`."""
    path = repositoryRoot / kManifestRelPath
    if not path.is_file():
        raise VerifyManifestError(f"{kManifestRelPath}: 파일이 없습니다")
    try:
        return parseManifest(json.loads(path.read_text(encoding="utf-8")))
    except ValueError as error:
        if isinstance(error, VerifyManifestError):
            raise
        raise VerifyManifestError(f"{kManifestRelPath}: JSON 을 읽지 못했습니다: {error}") from error


# ------------------------------------------------------------------------------
# 실행 순서 · 명령
# ------------------------------------------------------------------------------
def orderItems(manifest: VerifyManifest, listItem: list[VerifyItem]) -> list[tuple[str, list[VerifyItem]]]:
    """`listItem` 을 구성 순서로 묶고, 구성 안에서는 (종류, GPU 방식, 파일 순서)로 늘어놓는다. 항목이 없는 구성은 뺀다."""
    listGroup: list[tuple[str, list[VerifyItem]]] = []
    for config in manifest.listConfig:
        listInConfig = sorted((item for item in listItem if item.config == config.name), key=getRunOrderKey)
        if listInConfig:
            listGroup.append((config.name, listInConfig))
    return listGroup


def getStageIndex(item: VerifyItem) -> int:
    """항목의 단계 번호(`kListStageKinds`) — `--fail-fast` 가 단계 사이에서 멈춘다."""
    return next(index for index, listKind in enumerate(kListStageKinds) if item.kind in listKind)


def getStageName(stageIndex: int) -> str:
    return "·".join(kListStageKinds[stageIndex])


def groupByGpuMode(listItem: list[VerifyItem]) -> list[tuple[str, list[VerifyItem]]]:
    """이어진 항목 중 GPU 방식이 같은 것끼리 묶는다 — 묶음 하나가 잠금 한 번이다(항목마다 놓았다 잡으면 그 사이에 다른 쪽이 끼어든다)."""
    listGroup: list[tuple[str, list[VerifyItem]]] = []
    for item in listItem:
        if listGroup and listGroup[-1][0] == item.gpu:
            listGroup[-1][1].append(item)
        else:
            listGroup.append((item.gpu, [item]))
    return listGroup


def expandArguments(listArg: tuple[str, ...], mapValue: dict[str, str]) -> list[str]:
    """`{app}` · `{bin}` · `{preset}` · `{work}` 를 풀어 준다(모르는 자리 표시는 읽을 때 걸러졌다)."""
    return [_kPlaceholderRe.sub(lambda match: mapValue[match.group(1)], argument) for argument in listArg]


def buildItemCommand(item: VerifyItem, preset: str, repositoryRoot: Path, owner: str, workDir: Path, listShardArg: tuple[str, ...] = ()) -> list[str]:
    """항목을 돌리는 명령. App 이 필요한데 없으면(빌드 전) `VerifyManifestError`. `scenario` 같은 덮개 항목은 명령이 없다.

    `listShardArg` 는 `test` 항목의 샤드 한 조각이 실행 파일에 더 넘기는 인자(`--test_shard=k/n` · `--test_costs=…`)다 — `makeShardArguments` 가 만든다.
    """
    scripts = [sys.executable, "-m", "Scripts"]
    if item.kind == "build":
        return [*scripts, "full-build", "--owner", owner, "--preset", preset]
    if item.kind == "ctest":
        return ["ctest", "--test-dir", str(repositoryRoot / "build" / preset), "-L", item.label, "-j", "--output-on-failure"]
    if item.kind == "test":
        return [*scripts, "test", item.testFilter, "--preset", preset, "--no-build", *item.listExtraArg, *listShardArg]
    if item.bCovered:
        raise VerifyManifestError(f"항목 `{item.itemId}`: 덮개 항목은 따로 돌지 않습니다")
    tree = BuildTree.fromPreset(preset, repositoryRoot)
    app = tree.appExecutable
    bNeedsApp = item.tool == "app" or any("{app}" in argument for argument in item.listArg)
    if bNeedsApp and app is None:
        raise VerifyManifestError(f"항목 `{item.itemId}`: {tree.path} 에 App 이 없습니다 — 같은 구성의 build 항목이 먼저 지어야 합니다")
    listArg = expandArguments(item.listArg, {"app": str(app), "bin": str(tree.binDir), "preset": preset, "work": str(workDir)})
    return [*scripts, *listArg] if item.tool == "scripts" else [str(app), *listArg]


# ------------------------------------------------------------------------------
# 지표 · 결과 표
# ------------------------------------------------------------------------------
def extractMetricValues(listMetric: tuple[VerifyMetric, ...], outputText: str) -> dict[str, float | None]:
    """출력에서 지표마다 마지막으로 맞는 줄의 숫자. 어느 줄도 안 맞으면 None."""
    mapValue: dict[str, float | None] = {}
    for metric in listMetric:
        listMatch = re.findall(metric.pattern, outputText, flags=re.MULTILINE)
        mapValue[metric.name] = float(listMatch[-1] if isinstance(listMatch[-1], str) else listMatch[-1][0]) if listMatch else None
    return mapValue


def judgeMetrics(listMetric: tuple[VerifyMetric, ...], mapValue: dict[str, float | None]) -> list[str]:
    """한계를 어긴(또는 값이 없는) 지표의 설명 목록 — 비면 통과. 한계가 없는 지표는 보고만 한다."""
    listProblem: list[str] = []
    for metric in listMetric:
        if metric.maxValue is None and metric.minValue is None:
            continue
        value = mapValue.get(metric.name)
        if value is None:
            listProblem.append(f"{metric.name}: 출력에서 값을 찾지 못했다")
        elif metric.maxValue is not None and value > metric.maxValue:
            listProblem.append(f"{metric.name}: {value:g} {metric.unit} > 한계 {metric.maxValue:g}")
        elif metric.minValue is not None and value < metric.minValue:
            listProblem.append(f"{metric.name}: {value:g} {metric.unit} < 한계 {metric.minValue:g}")
    return listProblem


def formatMetricValues(listMetric: tuple[VerifyMetric, ...], mapValue: dict[str, float | None]) -> str:
    """표의 `값` 칸 — `이름 값 단위` 를 ` / ` 로 잇는다. 지표가 없으면 `-`."""
    listText = [f"{metric.name} {mapValue[metric.name]:g} {metric.unit}" if mapValue.get(metric.name) is not None else f"{metric.name} 없음" for metric in listMetric]
    return " / ".join(listText) or "-"


def summarizeBenchRows(outputText: str, maxRowCount: int) -> str:
    """출력의 `[Bench]` 줄(`test::logBenchSamples`)을 `이름 p50 N us` 로 줄여 앞의 `maxRowCount` 개만 ` / ` 로 잇는다. 줄이 없으면 빈 글 — 전체는 로그에 있다."""
    listRow = [f"{match.group(1).strip()} p50 {match.group(3)} us" for match in kBenchRowRe.finditer(outputText)]
    return " / ".join(listRow[:maxRowCount]) + (f" … 외 {len(listRow) - maxRowCount} 줄" if len(listRow) > maxRowCount else "")


def formatLimitText(item: VerifyItem) -> str:
    """표의 `한계` 칸 — 측정 항목은 적어 둔 한계, 지표 한계가 있으면 그것, 그 밖은 `-`."""
    listBound = [f"{metric.name} ≤ {metric.maxValue:g} {metric.unit}" if metric.maxValue is not None else f"{metric.name} ≥ {metric.minValue:g} {metric.unit}"
                 for metric in item.listMetric if metric.maxValue is not None or metric.minValue is not None]
    return item.limit or " / ".join(listBound) or "-"


@dataclass
class ItemResult:
    """항목 하나의 결과 — 상태(통과 · 실패 · 건너뜀 · 막힘), 이유, 값, 걸린 초, 로그 경로, 끝난 시각."""

    itemId: str
    status: str
    detail: str
    valueText: str
    seconds: float
    logPath: str
    finishedAt: str
    startedAt: str = ""   # 초 단위 ISO(`2026-10-26T00:50:13`). 돌지 않은 항목(건너뜀 · 막힘 · 명령을 못 만든 실패)은 빈 글, 덮개 항목은 덮개 대상의 시각


#: 결과 상태 — 통과 · 실패 · 건너뜀(돌리지 않았다: 명령이 없다 · wsl · 도구가 건너뜀) · 막힘(돌 수 없었다: 빌드 실패 · 잠금 시한 넘김).
kStatusPassed = "통과"
kStatusFailed = "실패"
kStatusSkipped = "건너뜀"
kStatusBlocked = "막힘"
#: 표의 `시작` 칸 — 시도한 시각이 없는 결과(건너뜀 · 막힘 · 안 돈 덮개). 값이 없는 `-` 와 구분한다.
kNotStartedText = "—(안 돎)"
#: 표의 `값` 칸에 줄여 싣는 `[Bench]` 줄 수.
kBenchSummaryRowCount = 3


def makeResult(itemId: str, status: str, detail: str, finishedAt: str, *, valueText: str = "-", seconds: float = 0.0, logPath: str = "") -> ItemResult:
    return ItemResult(itemId, status, detail, valueText, seconds, logPath, finishedAt)


def interpretOutcome(item: VerifyItem, returnCode: int, bLaunched: bool, bTimedOut: bool, outputText: str, seconds: float, logPath: str, finishedAt: str) -> ItemResult:
    """프로세스 하나의 끝을 항목 결과로 읽는다: 띄우지 못함 · 시간 초과 · 도구가 건너뜀(77) · 종료 코드 · 지표 한계. 통과는 종료 코드 0 이고 지표가 한계 안일 때뿐이다."""
    if not bLaunched:
        return makeResult(item.itemId, kStatusFailed, f"띄우지 못함: {outputText.strip()[:200]}", finishedAt, seconds=seconds, logPath=logPath)
    if bTimedOut:
        return makeResult(item.itemId, kStatusFailed, f"{item.timeoutMinutes} 분 안에 끝나지 않음", finishedAt, seconds=seconds, logPath=logPath)
    if returnCode == ExitCode.Skipped:
        return makeResult(item.itemId, kStatusSkipped, "도구가 판정을 건너뜀(77) — 기준 없음 · 백엔드 없음", finishedAt, seconds=seconds, logPath=logPath)
    mapValue = extractMetricValues(item.listMetric, outputText)
    listProblem = judgeMetrics(item.listMetric, mapValue) if item.judge == "metrics" else []
    valueText = formatMetricValues(item.listMetric, mapValue) if item.listMetric else (summarizeBenchRows(outputText, kBenchSummaryRowCount) or "-")
    if returnCode != 0:
        return makeResult(item.itemId, kStatusFailed, f"종료 코드 {returnCode}", finishedAt, valueText=valueText, seconds=seconds, logPath=logPath)
    if listProblem:
        return makeResult(item.itemId, kStatusFailed, " · ".join(listProblem), finishedAt, valueText=valueText, seconds=seconds, logPath=logPath)
    return makeResult(item.itemId, kStatusPassed, "", finishedAt, valueText=valueText, seconds=seconds, logPath=logPath)


#: 돌리지 않고 건너뛴 이유의 머리말 — 실행기의 한 줄(`끝 <id>: 건너뜀 선행 …`)과 `--failed-from` 이 같은 글로 이 항목을 다시 고른다.
kSkipRequiresMarker = "선행"
kSkipFailFastMarker = "앞 단계"


def makeRequiresSkipResult(item: VerifyItem, requiredId: str, requiredStatus: str, finishedAt: str) -> ItemResult:
    """선행(`requires`)이 통과하지 못해 돌리지 않은 항목의 결과 — 선행이 남겼어야 할 보고를 읽다가 연쇄로 지는 것을 막고 이유를 표에 적는다."""
    return makeResult(item.itemId, kStatusSkipped, f"{kSkipRequiresMarker} `{requiredId}` 가 {requiredStatus} — 이 항목은 그 결과를 읽는다", finishedAt)


def makeFailFastSkipResult(item: VerifyItem, stageName: str, failedId: str, finishedAt: str) -> ItemResult:
    """`--fail-fast` 가 앞 단계의 실패 때문에 돌리지 않은 항목의 결과."""
    return makeResult(item.itemId, kStatusSkipped, f"{kSkipFailFastMarker} {stageName} 의 `{failedId}` 가 졌다 — --fail-fast 로 뒤를 돌리지 않음", finishedAt)


def combineShardResults(item: VerifyItem, listShard: list[ItemResult], wallSeconds: float, finishedAt: str) -> ItemResult:
    """샤드 조각들의 결과를 항목 결과 하나로 — 하나라도 졌으면 실패(조각 번호와 이유), 안 졌고 막힌 조각이 있으면 막힘, 전부 건너뛰었으면 건너뜀, 아니면 통과.

    걸린 초는 조각들이 겹쳐 돈 벽시계 시간이다. 로그는 처음 진 조각의 것(없으면 첫 조각)이다.
    """
    count = len(listShard)
    listFailed = [(index, result) for index, result in enumerate(listShard) if result.status == kStatusFailed]
    listBlocked = [(index, result) for index, result in enumerate(listShard) if result.status == kStatusBlocked]
    listSkipped = [result for result in listShard if result.status == kStatusSkipped]
    listBad = listFailed or listBlocked
    if listBad:
        status = kStatusFailed if listFailed else kStatusBlocked
        detail = " · ".join(f"샤드 {index + 1}/{count}: {result.detail}" for index, result in listBad)
    elif len(listSkipped) == count:
        status, detail = kStatusSkipped, listSkipped[0].detail
    else:
        status, detail = kStatusPassed, f"{count} 샤드" + (f"(건너뜀 {len(listSkipped)})" if listSkipped else "")
    logPath = (listBad[0][1] if listBad else listShard[0]).logPath
    result = makeResult(item.itemId, status, detail, finishedAt, seconds=wallSeconds, logPath=logPath)
    result.startedAt = min((shard.startedAt for shard in listShard if shard.startedAt), default="")
    return result


def makeNotRunResult(item: VerifyItem, finishedAt: str) -> ItemResult:
    """돌리지 않는 항목(명령이 없다 · 사람이 해야 한다 · wsl)의 결과 — 이유를 표에 적는다."""
    if item.manual:
        return makeResult(item.itemId, kStatusSkipped, f"사람 확인: {item.manual}", finishedAt)
    if item.needCommand:
        return makeResult(item.itemId, kStatusSkipped, f"명령 없음: {item.needCommand}", finishedAt)
    return makeResult(item.itemId, kStatusSkipped, "wsl — 사용자가 요청할 때만", finishedAt)


def resolveCoveredResult(item: VerifyItem, mapResult: dict[str, ItemResult], finishedAt: str) -> ItemResult:
    """덮개 항목의 결과 — 덮개 항목(`covered_by`)이 통과하면 통과, 졌으면 실패, 안 돌았으면 그 상태를 이어받는다(어느 쪽이든 이유에 덮개 id 를 적는다)."""
    if item.bUnrunnable:
        return makeNotRunResult(item, finishedAt)
    cover = mapResult.get(item.coveredBy)
    if cover is None:
        return makeResult(item.itemId, kStatusSkipped, f"덮개 항목 `{item.coveredBy}` 를 고르지 않음", finishedAt)
    status = cover.status
    detail = {kStatusPassed: f"`{item.coveredBy}` 에 포함", kStatusFailed: f"`{item.coveredBy}` 가 졌다 — 이 항목이 졌는지는 그 로그로 본다"}.get(status, f"`{item.coveredBy}` 가 {status}")
    result = makeResult(item.itemId, status, detail, finishedAt, logPath=cover.logPath)
    result.startedAt = cover.startedAt   # 덮개 항목이 돈 시각이 이 항목이 포함된 판의 시각이다(안 돌았으면 빈 글 그대로)
    return result


def countStatus(listResult: list[ItemResult]) -> dict[str, int]:
    return {status: sum(result.status == status for result in listResult) for status in (kStatusPassed, kStatusFailed, kStatusBlocked, kStatusSkipped)}


def formatReport(manifest: VerifyManifest, listResult: list[ItemResult], title: str, environment: str) -> str:
    """결과 파일(마크다운) — 머리, 요약, 표 하나, 지거나 막힌 항목, 명령이 없어 돌지 않은 항목."""
    mapCount = countStatus(listResult)
    mapResult = {result.itemId: result for result in listResult}
    listLine = [f"# {title}", "", f"환경: {environment}", "", "요약: " + " · ".join(f"{status} {count}" for status, count in mapCount.items()), "",
                formatResultTable(manifest, listResult, environment), ""]
    listBad = [(item, mapResult[item.itemId]) for item in manifest.listItem if item.itemId in mapResult and mapResult[item.itemId].status in (kStatusFailed, kStatusBlocked)]
    if listBad:
        listLine += ["## 지거나 막힌 항목", *(f"- `{item.itemId}` {result.status}: {result.detail} (로그 `{result.logPath or '-'}`)" for item, result in listBad), ""]
    listNeed = [item for item in manifest.listItem if item.itemId in mapResult and item.needCommand]
    if listNeed:
        listLine += ["## 명령이 없어 돌지 않은 항목(need_command)", *(f"- `{item.itemId}` {item.title}: {item.needCommand}" for item in listNeed), ""]
    listManual = [item for item in manifest.listItem if item.itemId in mapResult and item.manual]
    if listManual:
        listLine += ["## 사람이 확인할 항목(manual)", *(f"- `{item.itemId}` {item.title}: {item.manual}" for item in listManual), ""]
    return "\n".join(listLine)


def escapeCellInternal(text: str) -> str:
    return text.replace("|", "\\|").replace("\n", " ")


def formatResultTable(manifest: VerifyManifest, listResult: list[ItemResult], environment: str) -> str:
    """결과 표 하나(항목 · 값 · 한계 · 통과/실패 · 측정 환경 · 시작(HH:MM:SS) · 날짜 · 옮길 곳). 파일 순서대로, 실행하지 않은 항목도 이유와 함께 든다."""
    mapResult = {result.itemId: result for result in listResult}
    listLine = ["| 항목 | 값 | 한계 | 통과/실패 | 측정 환경 | 시작 | 날짜 | 옮길 곳 |", "|---|---|---|---|---|---|---|---|"]
    for item in manifest.listItem:
        result = mapResult.get(item.itemId)
        if result is None:
            continue
        status = f"{result.status}({result.detail})" if result.detail else result.status
        listLine.append("| " + " | ".join(escapeCellInternal(cell) for cell in (
            f"`{item.itemId}` {item.title}", result.valueText, formatLimitText(item), status, f"{environment} · {item.config} · gpu {item.gpu}",
            result.startedAt[11:] or kNotStartedText, result.finishedAt, item.resultDoc)) + " |")
    return "\n".join(listLine)


# ------------------------------------------------------------------------------
# 디스크 대조 — 매니페스트가 가리키는 파일 · 프리셋 · 테스트 케이스가 실제로 있는가
# ------------------------------------------------------------------------------
def findManifestProblems(repositoryRoot: Path, manifest: VerifyManifest) -> list[str]:
    """매니페스트가 가리키는 것이 저장소에 있는지: 구성의 프리셋, `source` · `result_doc` 의 파일(첫 낱말), 덮개 항목의 시나리오 파일 · 테스트 케이스, `test` 의 필터."""
    listProblem: list[str] = []
    presetPath = repositoryRoot / "CMakePresets.json"
    if presetPath.is_file():
        setPreset = {preset["name"] for preset in json.loads(presetPath.read_text(encoding="utf-8")).get("configurePresets", [])}
        listProblem += [f"{kManifestRelPath}: 구성 `{config.name}` 의 프리셋 `{config.preset}` 이 CMakePresets.json 에 없습니다"
                        for config in manifest.listConfig if config.preset not in setPreset]
    for item in manifest.listItem:
        where = f"{kManifestRelPath}: 항목 `{item.itemId}`"
        for key, text in (("source", item.source), ("result_doc", item.resultDoc)):
            if not (repositoryRoot / text.split()[0]).is_file():
                listProblem.append(f"{where}: `{key}` 의 첫 낱말 `{text.split()[0]}` 이 저장소 파일이 아닙니다(`경로 절` 모양으로 적는다)")
        if item.shardCount > 1:
            try:
                planShards(repositoryRoot, item)
            except VerifyManifestError as error:
                listProblem.append(str(error))
        if any(_kReportFolderRe.search(argument) for argument in item.listArg) and not item.listRequires:
            listProblem.append(f"{where}: args 가 시나리오 보고 폴더({kReportFolderText})를 읽는데 `requires` 가 없습니다 — 그 보고를 만드는 항목을 선행으로 적는다"
                               f"(안 적으면 만든 항목이 지거나 안 돌 때 이 항목이 연쇄로 진다)")
        listPattern = [name for name in (item.testFilter.split(",") if item.testFilter else []) if name]
        for name in (*item.listCovered, *listPattern):
            if name.endswith(".scenario.xml"):
                if not (repositoryRoot / "Resource" / name).is_file():
                    listProblem.append(f"{where}: 시나리오 `{name}` 이 Resource/ 에 없습니다")
            elif not findTestCaseInternal(repositoryRoot, name):
                listProblem.append(f"{where}: 테스트 `{name}` 에 맞는 케이스가 Test/ 에 없습니다")
    return listProblem


def findTestCaseInternal(repositoryRoot: Path, name: str) -> bool:
    r"""`Suite.Case`(`*` 허용) 에 맞는 테스트 케이스 선언이 Test/ 의 .cpp 에 있는가. 모양이 아니면(전체를 뜻하는 `*` 등) 있다고 본다.

    뜻은 옛 정규식 `\bSuite\s*,\s*Case\b`(`Case=*` 이면 `\bSuite\b`)와 같다. 본문은 `readTestIndexInternal` 이 한 번만 훑어 색인한다.
    """
    match = _kSuiteCaseRe.match(name)
    if match is None:
        return True
    suite, case = match.groups()
    setWord, mapSuiteCase = readTestIndexInternal(repositoryRoot)
    if case == "*":
        return suite in setWord
    setCase = mapSuiteCase.get(suite, ())
    if "*" not in case:
        return case in setCase
    caseRe = re.compile(re.escape(case).replace(r"\*", r"\w*"))
    return any(caseRe.fullmatch(word) for word in setCase)


@lru_cache(maxsize=4)
def readTestSourcesInternal(repositoryRoot: Path) -> tuple[tuple[str, str], ...]:
    """Test/ 의 모든 .cpp 를 한 번만 읽어 (실행 파일 폴더 `Test/<이름>`, 본문) 목록으로 — 케이스 색인과 스위트 → 실행 파일 색인이 같이 쓴다. 프로세스 수명 동안만 유효하다."""
    testRoot = repositoryRoot / "Test"
    return tuple((path.relative_to(testRoot).parts[0], path.read_text(encoding="utf-8", errors="replace")) for path in sorted(testRoot.rglob("*.cpp")))


@lru_cache(maxsize=4)
def readTestIndexInternal(repositoryRoot: Path) -> tuple[frozenset[str], dict[str, frozenset[str]]]:
    """Test/ 의 모든 .cpp 를 한 글로 이어 한 번만 훑어 (낱말 집합, 낱말 -> 그 뒤 `,` 로 이어진 낱말 집합) 을 만든다.

    프로세스 수명 동안만 유효하다(게이트 · 일회성 도구 전용 — 파일이 바뀌는 상주 호출자가 생기면 `cache_clear()`).
    """
    text = "\n".join(source for _, source in readTestSourcesInternal(repositoryRoot))
    mapSuiteCase: dict[str, set[str]] = {}
    for found in _kWordCommaRe.finditer(text):
        mapSuiteCase.setdefault(found.group(1), set()).add(found.group(2))
    return frozenset(_kWordRe.findall(text)), {suite: frozenset(setCase) for suite, setCase in mapSuiteCase.items()}


# ------------------------------------------------------------------------------
# 샤드 — `shards` 가 있는 test 항목을 실행 파일의 `--test_shard=k/n` 조각 n 개로 나눈다
# ------------------------------------------------------------------------------
#: 테스트 정책 파일(저장소 기준) — 실행 파일의 비용표 · 호스트 잠금 · GPU 칸 수가 여기 있다.
kPolicyRelPath = "Test/TestPolicy.json"
#: 스위트를 선언하는 매크로(`SW_TEST_CASE( Suite,` · `SW_TEST_CASE_GENERATOR( Suite,` · `SW_TEST_CASE_PER_PARAMETER( Suite,` · `SW_TEST_REQUIRES_HOST( Suite,`).
_kSuiteDeclarationRe = re.compile(r"SW_TEST_\w+\(\s*([A-Za-z]\w*)\s*,")


@dataclass(frozen=True)
class ShardPlan:
    """샤드 나누기 계획 — 어느 실행 파일의 케이스인지, 샤드끼리 겹쳐 돌아도 되는지, 비용표 인자."""

    executable: str
    bMayOverlap: bool
    costArgument: str


@lru_cache(maxsize=4)
def readSuiteExecutableIndex(repositoryRoot: Path) -> dict[str, frozenset[str]]:
    """스위트 이름 -> 그 스위트가 선언된 실행 파일(폴더 `Test/<이름>`) 집합."""
    mapSuite: dict[str, set[str]] = {}
    for folder, source in readTestSourcesInternal(repositoryRoot):
        for suite in set(_kSuiteDeclarationRe.findall(source)):
            mapSuite.setdefault(suite, set()).add(folder)
    return {suite: frozenset(setFolder) for suite, setFolder in mapSuite.items()}


def readTestPolicy(repositoryRoot: Path) -> dict:
    path = repositoryRoot / kPolicyRelPath
    if not path.is_file():
        raise VerifyManifestError(f"{kPolicyRelPath}: 파일이 없습니다")
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except ValueError as error:
        raise VerifyManifestError(f"{kPolicyRelPath}: JSON 을 읽지 못했습니다: {error}") from error


def readGpuSlots(repositoryRoot: Path) -> int:
    """정책의 `gpuSlots` — GPU 를 동시에 받는 프로세스 수의 상한(샤드를 겹쳐 돌리는 수도 이를 넘지 않는다)."""
    slots = readTestPolicy(repositoryRoot).get("gpuSlots")
    if isinstance(slots, bool) or not isinstance(slots, int) or slots < 1:
        raise VerifyManifestError(f"{kPolicyRelPath}: `gpuSlots` 가 1 이상의 정수가 아닙니다")
    return slots


def planShards(repositoryRoot: Path, item: VerifyItem) -> ShardPlan:
    """`shards` 항목의 필터가 가리키는 스위트가 한 실행 파일에만 있는지 보고 그 실행 파일의 정책으로 계획을 세운다. 아니면 `VerifyManifestError`."""
    where = f"{kManifestRelPath}: 항목 `{item.itemId}`"
    listSuite = sorted({match.group(1) for name in item.testFilter.split(",") if (match := _kSuiteCaseRe.match(name.strip()))})
    if not listSuite:
        raise VerifyManifestError(f"{where}: `shards` 는 `Suite.Case` 모양의 필터가 있어야 어느 실행 파일을 나눌지 압니다")
    mapIndex = readSuiteExecutableIndex(repositoryRoot)
    setExecutable = {folder for suite in listSuite for folder in mapIndex.get(suite, frozenset())}
    if len(setExecutable) != 1:
        raise VerifyManifestError(f"{where}: `shards` 의 필터 스위트 {listSuite} 가 실행 파일 하나에 있어야 합니다(찾은 곳: {sorted(setExecutable) or '없음'})")
    executable = next(iter(setExecutable))
    entry = readTestPolicy(repositoryRoot).get("executables", {}).get(executable)
    if not isinstance(entry, dict):
        raise VerifyManifestError(f"{kPolicyRelPath}: executables 에 `{executable}` 줄이 없습니다")
    mapCost: dict[str, object] = {**entry.get("caseCosts", {}), **entry.get("hostCaseCosts", {})}
    costArgument = "--test_costs=" + ",".join(f"{name}={cost}" for name, cost in mapCost.items()) if mapCost else ""
    return ShardPlan(executable, "hostResourceLock" not in entry, costArgument)


def makeShardArguments(plan: ShardPlan, shardIndex: int, shardCount: int) -> tuple[str, ...]:
    """샤드 한 조각의 실행 파일 인자 — `--test_shard=<k>/<n>`(0 부터)과 비용표. 같은 표 · 같은 비용이라 모든 조각이 같은 배정을 계산한다."""
    return (f"--test_shard={shardIndex}/{shardCount}", *((plan.costArgument,) if plan.costArgument else ()))


# ------------------------------------------------------------------------------
# 문서의 대기 줄 — 글로 쌓지 못하게 id 로만 적는다
# ------------------------------------------------------------------------------
def findReferencedIds(line: str) -> list[str]:
    """한 줄에서 `매니페스트 id` 뒤에 이어진 백틱 id 들(쉼표 · 가운뎃점으로 이은 목록). 표지가 없으면 빈 목록."""
    return [itemId for match in _kIdListRe.finditer(line) for itemId in _kBacktickIdRe.findall(match.group(1))]


def findPendingLineViolations(repositoryRoot: Path, manifest: VerifyManifest | None) -> list[str]:
    """계획 문서 · 후보 문서(`docs/plans`, 안내 문서 제외)의 대기 줄을 점검한다.

    (a) `확인 대기` · `실행 대기` · `통합에서 확인` · `통합에서 실행` · `**남은 확인` 이 든 줄(제목 줄 제외)에는 `매니페스트 id` 와 id 가 있어야 하고 그 id 가 매니페스트에 있어야 한다.
    (b) `pending` 인 항목은 어느 문서의 `매니페스트 id` 줄이 가리켜야 한다 — 문서의 대기 줄을 지운 뒤 항목을 남기면 낡은 확인이 쌓인다.
    (c) 명령 없는 항목(`need_command` · `manual`)도 어느 문서의 `매니페스트 id` 줄이 가리켜야 한다 — 명령을 만들 일 · 사람 확인의 주인 문서(후보 문서 포함)가 없으면 아무도 안 하는 확인이 남는다.
    `manifest` 가 None(파일 없음)이면 대기 줄이 하나라도 있을 때 그 사실을 알린다.
    """
    setReferenced: set[str] = set()
    listViolation: list[str] = []
    for path in listPlanFiles(repositoryRoot):
        bFence = False
        for lineNumber, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
            if _kFenceRe.match(line):
                bFence = not bFence
            if bFence or line.lstrip().startswith("#"):
                continue
            listId = findReferencedIds(line)
            setReferenced.update(listId)
            if not _kPendingMarkerRe.search(line):
                continue
            where = f"docs/plans/{path.name}:{lineNumber}"
            if manifest is None:
                listViolation.append(f"{where}: 대기 줄이 있는데 {kManifestRelPath} 가 없습니다")
            elif not listId:
                listViolation.append(f"{where}: 대기 줄에는 글 대신 `매니페스트 id` 와 항목 id 를 적습니다(항목은 {kManifestRelPath} 에 더한다)")
            else:
                listViolation += [f"{where}: 매니페스트 id `{itemId}` 가 {kManifestRelPath} 에 없습니다" for itemId in listId if manifest.findItem(itemId) is None]
    if manifest is not None:
        listViolation += [f"{kManifestRelPath}: 일회성 항목 `{item.itemId}`(pending)를 가리키는 문서 줄이 없습니다 — 확인이 끝나 줄을 지웠으면 항목도 지웁니다"
                          for item in manifest.listItem if item.bPending and item.itemId not in setReferenced]
        listViolation += [f"{kManifestRelPath}: 명령 없는 항목 `{item.itemId}`({'manual' if item.manual else 'need_command'})를 가리키는 문서 줄이 없습니다 — "
                          "명령을 만들 일 · 사람 확인의 주인 문서(후보 문서)에 `매니페스트 id` 줄을 두거나 일이 사라졌으면 항목을 지웁니다"
                          for item in manifest.listItem if item.bUnrunnable and not item.bPending and item.itemId not in setReferenced]
    return listViolation


def checkVerifyPending(repositoryRoot: Path) -> list[str]:
    """`plans --check` 와 린트 게이트가 부르는 한 입구 — 매니페스트가 읽히고, 디스크와 맞고, 문서의 대기 줄과 맞는지. 위반 줄 목록."""
    path = repositoryRoot / kManifestRelPath
    if not path.is_file():
        return findPendingLineViolations(repositoryRoot, None)
    try:
        manifest = readManifest(repositoryRoot)
    except VerifyManifestError as error:
        return [str(error)]
    return findManifestProblems(repositoryRoot, manifest) + findPendingLineViolations(repositoryRoot, manifest)
