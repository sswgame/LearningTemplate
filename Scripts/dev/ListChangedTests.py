#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file ListChangedTests.py
@brief 바뀐 파일에서 돌릴 테스트를 고른다 — `py -3 -m Scripts test --changed` 가 쓴다.

Bazel · Buck 의 영향 기반 선택과 같은 자리다. 소스 → 테스트 의존은 빌드 그래프에서 계산한다.

- 소스 파일이 어느 타깃의 것인지는 CMake File API(타깃의 소스 목록)와 ninja 의존 기록(`ninja -t deps` — 오브젝트마다 실제로 읽은 헤더)으로,
  타깃 → 테스트 실행 파일은 File API 의 의존(링크 · `add_dependencies`)을 거꾸로 따라 찾는다. 단위는 **실행 파일**이다.
  엔진 구현 파일 하나가 어느 케이스를 지나는지는 include 로 알 수 없다(테스트가 부르는 함수가 그 파일을 부를 수 있다).
- 테스트 파일만 바뀌었으면 그 파일의 스위트만 돈다. 케이스(`SW_TEST_CASE`)만 든 `.cpp` 일 때만이고, 짝 헤더가 있거나 리플렉션 ·
  등록을 하는 파일(다른 스위트가 훑는 전역 표에 들어간다)은 그 실행 파일 전체다.
- 전체 집합(`lint|nogpu`)은 경로 표(`kListPathRule`)가 `full` 이라고 적은 곳뿐이다(빌드 규칙 · 테스트 틀 · 실행 중에 올리는 게임 모듈 등).
- **해석하지 못하면 오류다**(`ChangedTestsError`). 표에 없는 경로 · 어느 타깃에도 없는 소스 · 어느 테스트도 닿지 않는 타깃 · 읽지 못한
  빌드 그래프는 전체나 일부로 물러서지 않고 까닭을 알리며 멈춘다 — 표에 줄을 더하거나 빌드 폴더를 짓고 다시 돌린다.
- 린트(`lint`)는 늘 돈다 — 문서 · 스크립트 · 코드 전부를 보고 1 분 안쪽이다.

계약: 빌드 폴더가 지어져 있어야 한다(`ninja -t deps` 는 지은 오브젝트만 안다). 새 파일은 configure 뒤에 File API 에 들어오므로
`RunTests` 가 먼저 짓는다.
"""

from __future__ import annotations

import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common import BuildTree, runProcess  # noqa: E402

#: 경로 머리 → 뜻. 위에서 아래로 처음 맞는 줄을 쓴다. `full` 은 전체, `lint` 는 린트만, `python` 은 린트 + 파이썬 테스트,
#: `source` 는 빌드 그래프로, `test` 는 테스트 폴더 규칙(`Test/<실행 파일>/`)으로 고른다. 표에 없는 경로는 오류다.
kListPathRule: list[tuple[str, str]] = [
    ("Test/TestPolicy.json", "full"),
    ("Test/CMakeLists.txt", "full"),
    ("Test/TestFramework/", "full"),
    ("Test/FuzzTest/", "full"),
    ("Test/Load/", "full"),
    ("Test/QA/", "full"),
    ("Test/PythonTest/", "python"),
    ("Test/", "test"),
    # 게임 모듈은 App 이 실행 중에 올린다 — 테스트 실행 파일의 링크 그래프로는 닿지 않으므로 전체다.
    ("Source/Games/", "full"),
    ("Source/sw.natvis", "lint"),
    ("Source/", "source"),
    ("Tools/", "full"),
    ("ThirdParty/", "full"),
    ("cmake/", "full"),
    ("Config/", "full"),
    ("Resource/", "full"),
    ("CMakeLists.txt", "full"),
    ("CMakePresets.json", "full"),
    ("vcpkg.json", "full"),
    ("Scripts/generate/", "full"),
    ("Scripts/lint/", "lint"),
    ("Scripts/", "python"),
    ("docs/", "lint"),
    (".claude/", "lint"),
    (".github/", "lint"),
    (".vscode/", "lint"),
    (".clang-format", "lint"),
    (".clang-tidy", "lint"),
    (".clangd", "lint"),
    (".gitattributes", "lint"),
    (".gitignore", "lint"),
    (".mcp.json", "lint"),
]

#: 경로 머리 규칙보다 먼저 보는 확장자 — 문서는 어디 있든 린트만 본다(`Source/**/README.md` 의 코드 조각은 린트가 대조한다).
kSetLintOnlySuffix = {".md"}

#: 테스트 파일이 그 실행 파일 전체를 돌리게 하는 낱말 — 리플렉션 · 전역 등록은 다른 스위트가 훑는 표에 들어간다.
_kWholeExecutableMarkerRe = re.compile(r"\b(REFLECT|REFLECT_ENUM|SW_TEST_REQUIRES_HOST|SW_TEST_LOAD_SUITE|SW_TEST_KNOWN_LOG|SW_REGISTER_\w+)\b")
_kTestCaseRe = re.compile(r"SW_TEST_CASE\s*\(\s*(\w+)\s*,")
#: ninja 오브젝트 경로에서 타깃 이름(`…/CMakeFiles/<타깃>.dir/…`).
_kObjectTargetRe = re.compile(r"CMakeFiles/([^/]+)\.dir/")
#: 이 도구의 File API 질의(공유 질의 — 다른 클라이언트의 질의와 겹치지 않는다).
_kQueryRelPath = Path(".cmake/api/v1/query/codemodel-v2")


class ChangedTestsError(ValueError):
    """바뀐 파일을 테스트로 옮기지 못했다 — 까닭(경로마다 한 줄)을 담는다. 부르는 쪽은 고르지 않고 멈춘다."""

    def __init__(self, listReason: list[str]) -> None:
        super().__init__("\n".join(listReason))
        self.listReason = listReason


@dataclass
class TestSelection:
    """고른 결과. `bFull` 이면 나머지는 보지 않는다."""

    bFull: bool = False
    bPython: bool = False
    setExecutable: set[str] = field(default_factory=set)
    mapExecutableSuite: dict[str, set[str]] = field(default_factory=dict)
    listReason: list[str] = field(default_factory=list)

    def markFull(self, reason: str) -> None:
        self.bFull = True
        self.listReason.append(reason)

    def isEmpty(self) -> bool:
        return not (self.bFull or self.bPython or self.setExecutable or self.mapExecutableSuite)


@dataclass
class BuildGraph:
    """빌드 폴더에서 읽은 것: 파일 → 타깃, 타깃 → 그것에 기대는 타깃."""

    mapFileTarget: dict[str, set[str]] = field(default_factory=dict)
    mapDependent: dict[str, set[str]] = field(default_factory=dict)

    def findTargets(self, relativePath: str) -> set[str]:
        return self.mapFileTarget.get(normalizePath(relativePath), set())

    def collectDependents(self, listTarget: set[str]) -> set[str]:
        """타깃들과, 그것에 (간접으로라도) 기대는 타깃 전부."""
        setSeen: set[str] = set()
        listPending = list(listTarget)
        while listPending:
            target = listPending.pop()
            if target in setSeen:
                continue
            setSeen.add(target)
            listPending.extend(self.mapDependent.get(target, ()))
        return setSeen


def normalizePath(path: str) -> str:
    """비교용 경로 — 슬래시 · 소문자(Windows 는 대소문자를 가리지 않는다)."""
    return path.replace("\\", "/").lower()


def classifyPath(relativePath: str) -> str | None:
    """저장소 기준 경로의 규칙(`kListPathRule`). 표에 없으면 None(부르는 쪽이 오류로 알린다)."""
    path = relativePath.replace("\\", "/")
    if Path(path).suffix.lower() in kSetLintOnlySuffix:
        return "lint"
    for prefix, rule in kListPathRule:
        if path.startswith(prefix):
            return rule
    return None


def findSuitesOfTestFile(text: str) -> set[str] | None:
    """케이스만 든 테스트 파일이면 그 스위트들, 실행 파일 전체를 돌려야 하는 파일이면 None."""
    if _kWholeExecutableMarkerRe.search(text):
        return None
    setSuite = set(_kTestCaseRe.findall(text))
    return setSuite or None


def findMakeProgram(buildDir: Path) -> str:
    """그 빌드 폴더를 지은 ninja(`CMAKE_MAKE_PROGRAM`)."""
    makeProgram = BuildTree(buildDir).readCacheValue("CMAKE_MAKE_PROGRAM")
    if not makeProgram:
        raise ChangedTestsError([f"{buildDir}/CMakeCache.txt 에 CMAKE_MAKE_PROGRAM 이 없다 — 빌드 폴더를 구성하고 다시"])
    return makeProgram


def findCodemodelReply(replyDir: Path) -> str | None:
    """최신 File API 색인이 가리키는 codemodel 답 파일 이름(어느 클라이언트의 질의로 생겼든). 답이 아직 없으면 None."""
    listIndex = sorted(replyDir.glob("index-*.json")) if replyDir.is_dir() else []
    if not listIndex:
        return None
    index = json.loads(listIndex[-1].read_text(encoding="utf-8"))
    return next((item["jsonFile"] for item in index.get("objects", []) if item.get("kind") == "codemodel"), None)


def readBuildGraph(buildDir: Path, sourceRoot: Path) -> BuildGraph:
    """File API 응답 + ninja 의존 기록. 읽지 못하면 `ChangedTestsError`."""
    ninjaExecutable = findMakeProgram(buildDir)
    replyDir = buildDir / ".cmake" / "api" / "v1" / "reply"
    codemodelFile = findCodemodelReply(replyDir)
    if codemodelFile is None:
        # 질의가 없던 빌드 폴더 — 질의를 두고 다시 구성하면(캐시가 있어 몇 초) 답이 생긴다.
        queryFile = buildDir / _kQueryRelPath
        queryFile.parent.mkdir(parents=True, exist_ok=True)
        queryFile.write_text("", encoding="utf-8")
        configureResult = runProcess(["cmake", str(buildDir)])
        if not configureResult.bSucceeded:
            raise ChangedTestsError([f"File API 답을 만들려던 `cmake {buildDir}` 가 졌다:\n{configureResult.output[-2000:]}"])
        codemodelFile = findCodemodelReply(replyDir)
    if codemodelFile is None:
        raise ChangedTestsError([f"{replyDir} 에 codemodel 답이 없다 — 구성을 다시 하고 돌린다"])
    codemodel = json.loads((replyDir / codemodelFile).read_text(encoding="utf-8"))
    graph = BuildGraph()
    mapIdName: dict[str, str] = {}
    listTargetData: list[dict] = []
    for configuration in codemodel.get("configurations", []):
        for targetRef in configuration.get("targets", []):
            data = json.loads((replyDir / targetRef["jsonFile"]).read_text(encoding="utf-8"))
            mapIdName[data["id"]] = data["name"]
            listTargetData.append(data)
    for data in listTargetData:
        for source in data.get("sources", []):
            graph.mapFileTarget.setdefault(normalizePath(source["path"]), set()).add(data["name"])
        for dependency in data.get("dependencies", []):
            dependencyName = mapIdName.get(dependency["id"])
            if dependencyName:
                graph.mapDependent.setdefault(dependencyName, set()).add(data["name"])

    # 헤더는 타깃 소스 목록에 없는 것이 많다 — 그 헤더를 실제로 읽은 오브젝트의 타깃으로 잇는다.
    completed = runProcess([ninjaExecutable, "-C", str(buildDir), "-t", "deps"])
    if not completed.bSucceeded:
        raise ChangedTestsError([f"`{ninjaExecutable} -t deps` 가 졌다(종료 코드 {completed.returnCode}) — 빌드 폴더를 짓고 다시"])
    rootText = normalizePath(str(sourceRoot.resolve())) + "/"
    currentTarget: str | None = None
    for line in completed.stdout.splitlines():
        if not line:
            continue
        if not line.startswith(" "):
            match = _kObjectTargetRe.search(line.replace("\\", "/"))
            currentTarget = match.group(1) if match else None
            continue
        if currentTarget is None:
            continue
        dependencyPath = normalizePath(line.strip())
        if dependencyPath.startswith(rootText):
            graph.mapFileTarget.setdefault(dependencyPath[len(rootText):], set()).add(currentTarget)
    return graph


def selectTests(listChangedPath: list[str], sourceRoot: Path, setTestExecutable: set[str], graph: BuildGraph | None) -> TestSelection:
    """바뀐 파일(저장소 기준 경로)에서 고른다. 해석하지 못한 경로가 하나라도 있으면 그 까닭을 모아 `ChangedTestsError`.
    `graph` 는 `source` 경로가 있을 때만 필요하다(None 이면 그 경로들이 오류다)."""
    selection = TestSelection()
    listUnresolved: list[str] = []
    for relativePath in listChangedPath:
        path = relativePath.replace("\\", "/")
        rule = classifyPath(path)
        if rule is None:
            listUnresolved.append(f"{path}: 테스트 영향 표(ListChangedTests.kListPathRule)에 없는 경로 — 표에 줄을 더한다")
            continue
        if rule == "lint":
            continue
        if rule == "full":
            selection.markFull(f"{path}: 표가 전체로 정한 경로")
            continue
        if rule == "python":
            selection.bPython = True
            continue
        if rule == "test":
            executable = path.split("/")[1] if path.count("/") >= 2 else ""
            if executable not in setTestExecutable:
                listUnresolved.append(f"{path}: 테스트 실행 파일 폴더(Test/<실행 파일>/)가 아니고 표에도 없다 — 표에 줄을 더한다")
                continue
            absolutePath = sourceRoot / path
            bCaseOnly = absolutePath.suffix == ".cpp" and absolutePath.is_file() and not absolutePath.with_suffix(".h").is_file()
            setSuite = findSuitesOfTestFile(absolutePath.read_text(encoding="utf-8", errors="replace")) if bCaseOnly else None
            if setSuite is None:
                selection.setExecutable.add(executable)
            else:
                selection.mapExecutableSuite.setdefault(executable, set()).update(setSuite)
            continue
        # source — 빌드 그래프로
        if graph is None:
            listUnresolved.append(f"{path}: 빌드 그래프가 없다(빌드 폴더를 먼저 짓는다)")
            continue
        setOwner = graph.findTargets(path)
        if not setOwner:
            listUnresolved.append(f"{path}: 어느 타깃의 오브젝트도 읽지 않는 파일(새 파일이면 configure · 빌드 뒤에 다시)")
            continue
        setHit = graph.collectDependents(setOwner) & setTestExecutable
        if not setHit:
            listUnresolved.append(f"{path}: 타깃 {sorted(setOwner)} 에 기대는 테스트 실행 파일이 없다 — 실행 중에 올리는 모듈이면 표에 `full` 줄을 더한다")
            continue
        selection.setExecutable.update(setHit)

    if listUnresolved:
        raise ChangedTestsError(listUnresolved)
    # 실행 파일 전체를 도는 곳의 스위트 선택은 겹친다.
    for executable in selection.setExecutable:
        selection.mapExecutableSuite.pop(executable, None)
    return selection
