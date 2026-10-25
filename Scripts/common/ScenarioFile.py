"""
시나리오 파일(`*.scenario.xml`)이 실행하는 쪽에게 하는 선언 읽기 — 어느 파일인지 찾고(이름 · 경로), 기동 인자로 풀 수 있는 선언(`<Launch>` · 루트 `editor` ·
`requireForeground` · `<Load>`)을 읽는다.

형식 규칙의 정본은 C++ `AutomationScenario`(`readLaunchVariable` · `parseRootAttribute`)다. 여기는 그 규칙 가운데 **인자를 만드는 데 필요한 부분만** 같이 지킨다:
`<Launch>` 는 `variable="gv_…"` 와 공백 · 따옴표 없는 `value` 를 갖고 같은 변수를 두 번 적지 않는다. 틀린 선언은 App 이 읽기 오류(11)로 서기 전에 여기서 이유와 함께 멈춘다.
시나리오를 찾는 자리는 `AppScenarioTest` 와 같다(`engine/automation` · `game/<팩>/automation`, 하위 폴더 포함).
"""

from __future__ import annotations

import xml.etree.ElementTree as ElementTree
from dataclasses import dataclass
from pathlib import Path

kScenarioSuffix = ".scenario.xml"
#: 시나리오가 놓이는 폴더 모양(리소스 루트 기준).
kScenarioGlobPatterns: tuple[str, ...] = ("engine/automation/**/*.scenario.xml", "game/*/automation/**/*.scenario.xml")
#: 이 폴더의 시나리오는 폴더가 이미 에디터 실행이라 루트에 `editor="true"` 를 적지 않는다(`AutomationTestOptions::_bRequiresEditor`).
kEditorFolderId = "engine/automation/editor/"
#: `<Launch variable>` 이 시작해야 하는 글(C++ `readLaunchVariable`).
kLaunchVariablePrefix = "gv_"
_kLaunchForbiddenCharacters = " \t\"'"
#: 게임 팩 시나리오의 리소스 경로 앞머리 — `game/<팩>/automation/…`.
kGamePackPrefix = "game/"


class ScenarioFileError(ValueError):
    """시나리오를 찾지 못했거나 선언이 틀려 기동 인자를 만들 수 없다 — 이유가 문장에 든다. `ValueError` 라서 선언을 읽는 다른 도구(`qa/Load.py`)도 같은 잡이로 받는다."""


@dataclass(frozen=True)
class LoadSpec:
    """부하 선언(`<Load>` 한 줄)의 값 — C++ `AutomationLoad` 와 같은 칸. 읽는 자리는 `readLoadSpec` 하나다(`scenario` 와 `load` 가 같은 규칙을 본다)."""

    durationSeconds: float
    rampUpSeconds: float
    clientCount: int
    meshCount: int
    moverCount: int
    network: str


#: `<Load>` 속성 → `LoadSpec` 칸. 모르는 속성은 오류다(C++ `AutomationScenario::parseLoad`).
_kLoadAttributeFields: dict[str, str] = {"clients": "clientCount", "meshes": "meshCount", "movers": "moverCount", "durationSeconds": "durationSeconds",
                                         "rampUpSeconds": "rampUpSeconds", "network": "network"}


@dataclass(frozen=True)
class ScenarioDeclaration:
    """시나리오 파일 하나의 실행 선언."""

    resourceId: str                              # `game/empty/automation/fog.scenario.xml`
    path: Path
    name: str                                    # 루트 `name` — 실행기가 `Saved/Automation/<name>/` 에 산출물을 쓴다
    listLaunch: tuple[tuple[str, str], ...]      # `<Launch>` 의 (변수, 값), 파일 순서
    bEditor: bool                                # 에디터를 켜야 한다(루트 `editor` 이거나 에디터 시나리오 폴더)
    bForeground: bool                            # 창이 전경이어야 한다(`requireForeground`)
    bLoad: bool                                  # 부하 시나리오(`<Load>`)

    @property
    def stem(self) -> str:
        """보고 · 로그 파일 이름 조각(`fog`) — `AppTestUtil` 과 같다(파일 이름의 첫 `.` 앞)."""
        return self.resourceId.rpartition("/")[2].split(".", 1)[0]

    @property
    def packFolder(self) -> str:
        """게임 팩 시나리오의 팩 폴더(`game/empty`). 엔진 시나리오면 빈 글."""
        if not self.resourceId.startswith(kGamePackPrefix):
            return ""
        return kGamePackPrefix + self.resourceId[len(kGamePackPrefix):].split("/", 1)[0]

    @property
    def bGroupMember(self) -> bool:
        """`<묶음>.<n>.scenario.xml` — 앞 실행이 남긴 저장 상태를 이어 읽는 묶음의 일원이다(`AppScenarioTest::isNumberedGroupMember`)."""
        return self.resourceId.rpartition("/")[2].count(".") > 2

    def makeLaunchArguments(self) -> list[str]:
        """`<Launch>` 를 명령줄 인자로(`-gv_firstScene=<씬>`) — `AutomationTestOptions::makeLaunchArguments` 와 같은 모양."""
        return [f"-{variable}={value}" for variable, value in self.listLaunch]


def findResourceRoot(workingFolder: Path) -> Path:
    """App 이 `Resource/` 를 찾는 규칙 그대로 — 작업 폴더에서 위로 올라가며 `Resource/` 를 가진 첫 폴더(`ResourcePaths::initialize`). 없으면 오류."""
    resolved = Path(workingFolder).resolve()
    for folder in [resolved, *resolved.parents]:
        if (folder / "Resource").is_dir():
            return folder / "Resource"
    raise ScenarioFileError(f"{resolved} 에서 위로 올라가도 Resource/ 폴더가 없다 — App 이 시나리오를 읽을 수 없다")


def listScenarioIds(resourceRoot: Path) -> list[str]:
    """리소스 루트 아래의 시나리오 경로(리소스 경로, 이름순)."""
    setId: set[str] = set()
    for pattern in kScenarioGlobPatterns:
        setId.update(path.relative_to(resourceRoot).as_posix() for path in resourceRoot.glob(pattern))
    return sorted(setId)


def readRootName(path: Path) -> str:
    """루트 `name` 만 읽는다(파일 전체를 다 읽지 않는다). 못 읽으면 빈 글 — 이름 찾기에서 빠질 뿐이다."""
    try:
        for _, element in ElementTree.iterparse(path, events=("start",)):
            return element.get("name", "")
    except (OSError, ElementTree.ParseError):
        return ""
    return ""


def findScenarioCandidates(resourceRoot: Path, query: str) -> list[str]:
    """
    이름 · 경로 조각 하나로 후보를 좁힌다. 더 좁은 단계에서 맞은 것이 있으면 거기서 멈춘다:
    ① 파일 경로(존재하는 파일, 리소스 루트 안) ② 리소스 경로 · 파일 이름 조각 · 루트 `name` 이 글 전체로 같음 ③ 리소스 경로나 `name` 에 조각이 들어 있음(대소문자 무시).
    """
    listId = listScenarioIds(resourceRoot)
    text = query.strip().replace("\\", "/")
    if not text:
        raise ScenarioFileError("시나리오 이름이 비었다")
    asFile = Path(query)
    if text.endswith(kScenarioSuffix) and asFile.is_file():
        try:
            return [asFile.resolve().relative_to(resourceRoot.resolve()).as_posix()]
        except ValueError as error:
            raise ScenarioFileError(f"{asFile} 은 App 이 읽는 Resource/({resourceRoot.as_posix()}) 밖에 있다 — 그 트리의 App 으로 돌린다(--app)") from error
    lowered = text.lower()
    mapName = {resourceId: readRootName(resourceRoot / resourceId).lower() for resourceId in listId}
    listExact = [resourceId for resourceId in listId
                 if lowered in (resourceId, resourceId.rpartition("/")[2], resourceId.rpartition("/")[2].split(".", 1)[0], mapName[resourceId])
                 or lowered + kScenarioSuffix in (resourceId, resourceId.rpartition("/")[2])]
    if listExact:
        return listExact
    return [resourceId for resourceId in listId if lowered in resourceId or lowered in mapName[resourceId]]


def readScenarioDeclaration(resourceRoot: Path, resourceId: str) -> ScenarioDeclaration:
    """시나리오 하나의 실행 선언을 읽는다. 읽을 수 없거나 선언이 틀리면 `ScenarioFileError`."""
    path = resourceRoot / resourceId
    try:
        root = ElementTree.parse(path).getroot()
    except (OSError, ElementTree.ParseError) as error:
        raise ScenarioFileError(f"{resourceId}: XML 을 읽지 못했다 — {error}") from error
    if root.tag != "Scenario":
        raise ScenarioFileError(f"{resourceId}: 루트가 <Scenario> 가 아니다(<{root.tag}>)")
    return ScenarioDeclaration(resourceId=resourceId, path=path, name=root.get("name", ""), listLaunch=readLaunchListInternal(resourceId, root),
                               bEditor=readBoolInternal(resourceId, root, "editor") or resourceId.startswith(kEditorFolderId),
                               bForeground=readBoolInternal(resourceId, root, "requireForeground"),
                               bLoad=any(child.tag == "Load" for child in root))


def readLoadSpec(label: str, root: ElementTree.Element) -> LoadSpec:
    """루트 아래 `<Load>` 하나의 값을 읽는다. 하나가 아니거나 모르는 속성이거나 `durationSeconds` 가 0 이하면 `ScenarioFileError`. `label` 은 오류 문장 머리(파일 경로)."""
    listLoad = root.findall("Load")
    if len(listLoad) != 1:
        raise ScenarioFileError(f"{label}: needs exactly one <Load>")
    mapValue: dict[str, object] = {"durationSeconds": 0.0, "rampUpSeconds": 0.0, "clientCount": 0, "meshCount": 0, "moverCount": 0, "network": ""}
    for key, value in listLoad[0].attrib.items():
        if key not in _kLoadAttributeFields:
            raise ScenarioFileError(f"{label}: <Load> unknown attribute '{key}'")
        field = _kLoadAttributeFields[key]
        try:
            mapValue[field] = value if isinstance(mapValue[field], str) else type(mapValue[field])(float(value))
        except ValueError as error:
            raise ScenarioFileError(f"{label}: <Load> {key} is not a number: '{value}'") from error
    spec = LoadSpec(**mapValue)  # type: ignore[arg-type]
    if spec.durationSeconds <= 0:
        raise ScenarioFileError(f"{label}: <Load> needs durationSeconds > 0")
    return spec


def readBoolInternal(resourceId: str, root: ElementTree.Element, attributeName: str) -> bool:
    """루트의 불 속성(`true` · `false`). 없으면 false, 다른 글이면 App 이 읽기 오류로 서므로 여기서 멈춘다."""
    text = root.get(attributeName)
    if text is None:
        return False
    if text not in ("true", "false"):
        raise ScenarioFileError(f"{resourceId}: {attributeName} 는 true 또는 false 여야 한다(받은 값 '{text}')")
    return text == "true"


def readLaunchListInternal(resourceId: str, root: ElementTree.Element) -> tuple[tuple[str, str], ...]:
    """루트 아래 `<Launch variable value/>` 들을 읽는다(C++ `readLaunchVariable` 의 규칙)."""
    listLaunch: list[tuple[str, str]] = []
    for child in root:
        if child.tag != "Launch":
            continue
        unknown = sorted(set(child.attrib) - {"variable", "value"})
        if unknown:
            raise ScenarioFileError(f"{resourceId}: <Launch> 의 모르는 속성 {', '.join(unknown)} (variable · value)")
        variable = child.get("variable", "")
        value = child.get("value", "")
        if not variable.startswith(kLaunchVariablePrefix) or not value or any(character in value for character in _kLaunchForbiddenCharacters):
            raise ScenarioFileError(f"{resourceId}: <Launch> 는 variable=\"gv_…\" 와 공백 · 따옴표 없는 value 가 필요하다(받은 값 variable='{variable}' value='{value}')")
        if any(variable == declared for declared, _ in listLaunch):
            raise ScenarioFileError(f"{resourceId}: <Launch> {variable} 를 두 번 적었다")
        listLaunch.append((variable, value))
    return tuple(listLaunch)
