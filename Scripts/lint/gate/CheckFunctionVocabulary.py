#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
함수 이름 어휘 검사 — 한 개념에 이름 하나.

**같은 일을 하는 함수가 두 이름을 갖는 것은 규칙이 없어서가 아니라 아무도 세지 않아서다.**
세지 않으면 `queryAABB` 와 `queryAabb`, `alloc*` 과 `allocate*`, `setup*` 과 `initialize*` 가 나란히
생긴다. 읽는 사람은 둘 중 어느 쪽이 맞는지 알 수 없고, 다음 사람은 방금 본 쪽을 따라 쓴다.

규칙은 AGENTS.md "Function names" 에 적혀 있고 여기서 강제한다.

  1) AcronymRun  — 함수 이름의 대문자 묶음은 등록부(`Scripts/lint/AcronymRegistry.py`)의 강제 약어 하나여야 한다
                   (`updateUI` · `queryAABB` — 약어는 대문자, docs/plans/AcronymSpelling.md). 등록부에 없거나 아직 강제하지 않은
                   약어의 대문자 묶음(`bindComputeUAV`)은 잡는다. 마지막 대문자가 다음 낱말의 첫 글자면 묶음에서 뺀다
                   (`getHUDScale` 의 묶음은 `HUD`, `bindVector2DCallback` 의 `DC` 는 묶음이 아니다). 강제 약어를 Pascal 로 쓴 이름
                   (`updateHud`)과 이어 붙은 약어(`RHIUI`)는 `CheckAcronymSpelling.py` 가 본다.
  2) BannedVerb  — 한 개념에 동사 하나. `setup`/`startup`/`cleanup` → `initialize`/`shutdown`,
                   `alloc` → `allocate`, `fetch`/`retrieve`/`lookup`/`obtain` → `get`/`find`,
                   `build`/`generate`/`construct` → `make`/`create`/`compute` (다시 채우면 `rebuild`/`populate`).
     Abbreviation — 함수 이름에 줄임말을 쓰지 않는다. `appendBoolAttr` → `appendBoolAttribute`.
                   BannedVerb · Abbreviation 은 `mapExemption` 에 오른 기존 선언을 건너뛴다(새 위반만 막는다).
                   맨이름 `out` 매개변수는 `CheckOutParameterNames.py` 가 본다.
  3) CheckVerb   — `check*` 는 술어가 아니다. bool 이면 `is*`/`has*`, void 면 `assert*` 다.
  4) NamePair    — 같은 이름에 `string_view` 판과 `const hashed_string&` 판을 둘 다 두지 않는다.
                   `hashed_string` 은 리터럴에서 암묵 변환되므로 `f( "Jump" )` 가 **모호**해진다. 이름은
                   `hashed_string` 하나로 받고, 동적 텍스트는 호출부가 `hashed_string( x )` 로 올린다.
  5) BareGetter  — `setX()` 와 짝인 게터는 `getX()` / `isX()` 다. 맨이름 `x()` 는 안 된다
                   (`setName`/`name()` 이 한 클래스에 있으면 잡는다).

**헤더만 본다.** 호출부까지 보면 우리 것이 아닌 이름(`vkGetPhysicalDeviceSurfaceCapabilitiesKHR`)
을 잡는다. 선언은 어차피 헤더에 있고, 규칙이 말하는 것도 그 표면이다.

  python Scripts/lint/gate/CheckFunctionVocabulary.py [--root <repo>] [--files <path>...]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

import AcronymRegistry as registry  # noqa: E402
from common import kLintTargetRelDirs, mapConcurrent  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402

# 훑을 곳 — 우리가 이름을 정하는 코드만.
_kScanRoots = kLintTargetRelDirs
_kHeaderSuffix = (".h", ".hpp", ".inl")

# 선언 한 줄: [지정자]* 반환형 이름( ... — 대입(`=`)이 앞에 오면 호출부이므로 뺀다.
_kDeclRe = re.compile(
    r"^[\t ]*(?:(?:SW_\w*API|static|virtual|inline|constexpr|explicit|friend|\[\[nodiscard\]\])[\t ]+)*"
    r"(?!return\b|else\b|delete\b|new\b|case\b)"
    r"[A-Za-z_][\w:<>,\t &*]*?[\t &*]([a-z]\w*)[\t ]*\("
)

# camelCase 이름 안의 대문자 묶음. 묶음 뒤가 소문자면 마지막 대문자는 다음 낱말의 첫 글자다(복수 `s` 는 낱말이 아니다).
#   - `getGLTextureName`("GL") · `bindComputeUAV`("UAV") → 강제 약어가 아니라 위반
#   - `updateHUD` · `getHUDScale`("HUD") → 강제 약어면 위반 아님
#   - `bindVector2DCallback`("DC" = D + Callback) · `isVSyncEnabled`("VS" = V + Sync) → 묶음이 아님
_kUpperRunRe = re.compile(r"[A-Z]{2,}")


def findAcronymRunInternal(name: str) -> str | None:
    """강제 약어 하나가 아닌 대문자 묶음(없으면 None)."""
    for match in _kUpperRunRe.finditer(name):
        run = match.group(0)
        after = name[match.end():match.end() + 2]
        if after[:1].islower() and not (after[:1] == "s" and not after[1:2].islower()):
            run = run[:-1]
        if len(run) >= 2 and run not in registry.kEnforced:
            return run
    return None

# 금지 동사 → 써야 할 동사. 접두사 뒤에 대문자가 오거나 이름이 거기서 끝날 때만 본다
# (`allocate` 는 `alloc` + 소문자라 걸리지 않고, `fetch_add` 는 `_` 라 걸리지 않는다).
_kBannedVerb: dict[str, str] = {
    "setup": "initialize",
    "startup": "initialize",
    "teardown": "shutdown",
    "cleanup": "shutdown",
    "alloc": "allocate",
    "dealloc": "free (STL 할당자 계약인 deallocate 는 예외)",
    "dispose": "release / free",
    "fetch": "get / find",
    "retrieve": "get / find",
    "lookup": "find",
    "obtain": "get / acquire",
    "calculate": "compute",
    "calc": "compute",
    "build": "make (값) / create (소유) / compute (계산) — 변경 이력·단계면 rebuild / populate",
    "generate": "make / create / compute",
    "construct": "make / create",
}
_kBannedVerbRe = re.compile(r"^(" + "|".join(sorted(_kBannedVerb, key=len, reverse=True)) + r")(?=[A-Z0-9]|$)")

# 함수 이름 안의 줄임말 → 풀어 쓴 낱말. 낱말 경계(뒤가 소문자가 아님)일 때만 본다(`Attribute` 는 걸리지 않는다).
_kAbbreviation: dict[str, str] = {
    "Attr": "Attribute",
}
_kAbbreviationRe = re.compile(r"(" + "|".join(_kAbbreviation) + r")(?![a-z])")

_kCheckVerbRe = re.compile(r"^check(?=[A-Z])")

# 첫 매개변수 타입 — 4) NamePair 용. `f( string_view a` / `f( const hashed_string& a`.
_kFirstParamRe = re.compile(r"\(\s*(?:const\s+)?(string_view|hashed_string)\b")

# 5) BareGetter 가 건너뛰는 것 — 술어 접두사는 규칙이 허용하는 게터 모양이다.
_kPredicatePrefixRe = re.compile(r"^(is|has|was|can|should)[A-Z]")

def isExemptInternal(rule: str, stem: str, name: str) -> bool:
    """`규칙:파일 이름::함수 이름` 에 맞는 예외 줄(fnmatch)이 있으면 쓴 것으로 적고 True 를 돌려줍니다."""
    key = CheckFunctionVocabularyGate.findExemptionKey(f"{rule}:{stem}::{name}")
    if key is None:
        return False
    CheckFunctionVocabularyGate.useExemption(key)
    return True


def scanFileInternal(filePath: Path, repositoryRoot: Path) -> list[str]:
    relativePath = filePath.relative_to(repositoryRoot).as_posix()
    try:
        text = filePath.read_text(encoding="utf-8", errors="replace")
    except OSError as exception:
        return [f"{relativePath}: 읽기 실패: {exception}"]
    stem = filePath.stem

    # 주석의 줄바꿈은 남긴다 — 지우면 보고하는 줄 번호가 주석 줄 수만큼 앞당겨진다.
    text = re.sub(r"/\*.*?\*/", lambda comment: "\n" * comment.group(0).count("\n"), text, flags=re.S)
    violations: list[str] = []
    seenName: set[str] = set()
    mapNameToFirstParam: dict[str, set[str]] = {}
    mapNameToLine: dict[str, int] = {}

    for lineIndex, rawLine in enumerate(text.split("\n"), 1):
        line = re.sub(r"//.*$", "", rawLine)
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        if "=" in line.split("(", 1)[0]:
            continue
        match = _kDeclRe.match(line)
        if match is None:
            continue

        name = match.group(1)
        firstParam = _kFirstParamRe.search(line[match.end() - 1 :])
        if firstParam is not None:
            # 매개변수 수가 같은 쌍만 모호하다 — `f( string_view )` 와 `f( const hashed_string&, int )` 는 아니다.
            paramCount = line[match.end() - 1 :].count(",")
            mapNameToFirstParam.setdefault(name, set()).add(f"{firstParam.group(1)}/{paramCount}")
        mapNameToLine.setdefault(name, lineIndex)
        if name in seenName:
            continue
        seenName.add(name)

        acronymRun = findAcronymRunInternal(name)
        if acronymRun is not None:
            violations.append(
                f"{relativePath}:{lineIndex}: [AcronymRun] '{name}' — 대문자 묶음 '{acronymRun}' 은 강제 약어가 아닙니다."
                f" 등록부(Scripts/lint/AcronymRegistry.py)의 kEnforced 에 오르기 전에는 camelCase 낱말 하나로 씁니다(UAV→Uav)."
            )

        verbMatch = _kBannedVerbRe.match(name)
        if verbMatch is not None and not isExemptInternal("BannedVerb", stem, name):
            verb = verbMatch.group(1)
            violations.append(
                f"{relativePath}:{lineIndex}: [BannedVerb] '{name}' — '{verb}' 대신 '{_kBannedVerb[verb]}' 를 씁니다."
            )

        abbreviationMatch = _kAbbreviationRe.search(name)
        if abbreviationMatch is not None and not isExemptInternal("Abbreviation", stem, name):
            word = abbreviationMatch.group(1)
            violations.append(
                f"{relativePath}:{lineIndex}: [Abbreviation] '{name}' — '{word}' 대신 '{_kAbbreviation[word]}' 로 풀어 씁니다."
            )

        if _kCheckVerbRe.match(name):
            violations.append(
                f"{relativePath}:{lineIndex}: [CheckVerb] '{name}' — check 는 술어가 아닙니다."
                f" bool 이면 is*/has*, void 로 단언하면 assert* 입니다."
            )

    for name, kinds in sorted(mapNameToFirstParam.items()):
        ambiguous = any(f"string_view/{count}" in kinds and f"hashed_string/{count}" in kinds for count in range(0, 8))
        if ambiguous:
            violations.append(
                f"{relativePath}:{mapNameToLine[name]}: [NamePair] '{name}' — string_view 판과 hashed_string 판이 같이 있습니다."
                f" 리터럴 호출이 모호해지니 hashed_string 하나만 두고, 동적 텍스트는 호출부에서 hashed_string( x ) 로 올립니다."
            )

    for name in sorted(seenName):
        if len(name) > 3 and name.startswith("set") and name[3].isupper():
            bare = name[3].lower() + name[4:]
            # 술어 모양(`canEverTick`)은 허용. `isX`/`getX` 가 따로 있으면 맨이름은 게터가 아니라 동작(`open()`)이다.
            if bare not in seenName or bare == "bool" or _kPredicatePrefixRe.match(bare):
                continue
            if ("get" + name[3:]) in seenName or ("is" + name[3:]) in seenName:
                continue
            violations.append(
                f"{relativePath}:{mapNameToLine[bare]}: [BareGetter] '{bare}()' — '{name}()' 과 짝인 게터는"
                f" 'get{name[3:]}()' (bool 이면 'is{name[3:]}()', 없을 수 있으면 'find{name[3:]}()') 입니다. 맨이름은 쓰지 않습니다."
            )

    return violations


class CheckFunctionVocabularyGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있어 어긋날 수 없다."""

    description = "함수 이름 어휘 검사"
    buildComment = "Checking function-name vocabulary (acronym casing, one verb per concept, predicate form)..."
    timeoutSeconds = 30
    preCommitPattern = ("*.cpp", "*.cc", "*.cxx", "*.c", "*.h", "*.hpp", "*.inl")
    preCommitFileArgument = "--files"
    violationHeader = "이름 규칙 위반"
    hint = "\n규칙은 AGENTS.md 의 'Function names' 절에 있습니다."
    # 예외 표 — **새 위반만 막는다.** 규칙 전부터 있던 선언만 올리고, 고치면 지운다(전체 훑기가 낡은 줄을 잡는다).
    # 키는 `규칙:헤더 파일 이름(확장자 없음)::함수 이름` — 폴더를 옮겨도 그대로다. 이유가 "개명 예정 X" 이면 기계적 치환에서 X 로 바꾸고,
    # "도메인 용어" 면 그 분야에서 그 동사가 곧 용어라 그대로 둔다.
    mapExemption = {
        # BannedVerb
        "BannedVerb:FrameArenaAllocator::construct": "도메인 용어 — STL 할당자 계약(allocator_traits::construct)과 같은 이름",
        "BannedVerb:ReflectionContainers::constructEmpty": "도메인 용어 — 주어진 메모리에 빈 컨테이너를 배치 생성(C++ 객체 수명 시작)",
        "BannedVerb:Uuid::generate": "도메인 용어 — UUID 생성(RFC 9562)",
        "BannedVerb:RunMap::generate": "도메인 용어 — 절차적 생성(PCG)",
        "BannedVerb:SurfaceBvh::build": "도메인 용어 — BVH 구축(Embree · Jolt 와 같은 용어)",
        "BannedVerb:ThemePark::buildRide": "도메인 용어 — 게임 안에서 놀이기구를 짓는 행동",
        "BannedVerb:ParkDirectorComponent::build*": "도메인 용어 — 게임 안에서 놀이기구를 짓는 행동",
        "BannedVerb:CrashContext::buildCrashReportPath": "개명 예정 makeCrashReportPath — 경로 문자열을 만든다",
        "BannedVerb:SkeletonBoneLod::buildMasks": "개명 예정 computeMasks — 마스크를 계산한다",
        "BannedVerb:SpriteMeshBuilder::buildSlicedVertices": "개명 예정 makeSlicedVertices — 정점 값을 만든다",
        "BannedVerb:TerrainMeshBuilder::buildChunkVertices": "개명 예정 makeChunkVertices — 정점 값을 만든다",
        "BannedVerb:WaterBodyComponent::build*Vertices": "개명 예정 make*Vertices — 정점 값을 만든다",
        "BannedVerb:FrameRenderer::buildLightViewProj": "개명 예정 computeLightViewProj — 행렬을 계산한다",
        "BannedVerb:FrameRenderer::buildViewProj": "개명 예정 computeViewProj — 행렬을 계산한다",
        "BannedVerb:RenderGraph::buildResourceLifetimes": "개명 예정 computeResourceLifetimes — 수명 구간을 계산한다",
        "BannedVerb:GPUSceneBuilder::buildViewTransparentOrders": "개명 예정 computeViewTransparentOrders — 정렬 순서를 계산한다",
        "BannedVerb:ShaderBindingLayout::build": "개명 예정 make — 레이아웃 값을 돌려준다",
        "BannedVerb:ShaderBindingLayout::buildBindPlan": "개명 예정 computeBindPlan — 바인딩 계획을 계산한다",
        "BannedVerb:DirectionalLightComponent::buildShadow*": "개명 예정 computeShadow* — 행렬 · 투영을 계산한다",
        "BannedVerb:ReflectionTypes::buildAncestorDisplay": "개명 예정 computeAncestorDisplay — 표시 문자열을 계산한다",
        "BannedVerb:OpsHttpEndpoint::buildResponse": "개명 예정 makeResponse — 응답 문자열을 만든다",
        "BannedVerb:ScheduleSystem::buildPlan": "개명 예정 makePlan — 일정 구간을 out 으로 만든다",
        "BannedVerb:ChatWordFilter::buildFailLinks": "개명 예정 computeFailLinks — 실패 링크를 계산한다(Aho-Corasick)",
        "BannedVerb:TestFramework::buildRunOrder": "개명 예정 makeRunOrder — 실행 순서 값을 돌려준다",
        "BannedVerb:ParserConfig::buildArgs": "개명 예정 makeArgs — 인자 목록 값을 돌려준다",
        "BannedVerb:DevConsoleController::buildVisibleLines": "개명 예정 collectVisibleLines — 보이는 줄을 out 으로 모은다",
        "BannedVerb:GameObjectStore::generateNewId": "개명 예정 allocateId — 다음 id 를 내준다",
        "BannedVerb:FrameRenderer::buildOutputPsoVariants": "개명 예정 createOutputPsoVariants — PSO 를 만들어 소유한다",
        "BannedVerb:JoltPhysicsScene::buildShape": "개명 예정 createShape — Jolt 셰이프를 만든다(참조 소유)",
        "BannedVerb:JoltPhysicsScene::buildSingleShape": "개명 예정 createSingleShape — Jolt 셰이프를 만든다(참조 소유)",
        "BannedVerb:OptionsMenuScreen::buildRow": "개명 예정 createRow — 행 위젯을 만든다",
        "BannedVerb:X11SplashWindow::buildScaledImage": "개명 예정 createScaledImage — X11 이미지를 만든다",
        "BannedVerb:ObjectiveMarkerComponent::buildContent": "개명 예정 createContent — 위젯을 만들어 돌려준다",
        "BannedVerb:GPU*Pool::build": "개명 예정 rebuild — 메시 목록으로 풀을 다시 채운다(GPUMeshVertexPool · GPUMeshMorphPool · GPUVertexAnimationPool)",
        "BannedVerb:TickRegistry::buildStages": "개명 예정 rebuildStages — 단계 표를 다시 채운다",
        "BannedVerb:NavMeshGeometry::buildSpatialIndex": "개명 예정 rebuildSpatialIndex — 공간 색인을 다시 채운다",
        "BannedVerb:GPUSceneBuilder::buildFromScene": "개명 예정 populateFromScene — 씬에서 프레임 데이터를 채운다",
        "BannedVerb:GPUSceneBuilder::buildBatches": "개명 예정 populateBatches — 배치 표를 채운다",
        "BannedVerb:FractureGraph::buildHierarchy": "개명 예정 populateHierarchy — 그래프 계층을 채운다",
        "BannedVerb:PoseRetargetComponent::buildBasePose": "개명 예정 populateBasePose — 기준 포즈를 채운다",
        "BannedVerb:SkeletalAnimatorComponent::buildBasePose": "개명 예정 populateBasePose — 기준 포즈를 채운다",
        "BannedVerb:TypeRegistry::buildLookupCaches": "개명 예정 populateLookupCaches — 조회 캐시를 채운다",
        "BannedVerb:ReflectionTypes::buildLookupCache": "개명 예정 populateLookupCache — 조회 캐시를 채운다",
        "BannedVerb:OptionsMenuScreen::buildRows": "개명 예정 populateRows — 행 목록을 채운다",
        "BannedVerb:AdventureElementGrid::buildRuleTable": "개명 예정 populateRuleTable — 규칙 표를 채운다",
        "BannedVerb:GimmickCircuit::build": "개명 예정 populate — 정의로 회로를 채운다",
        # Abbreviation
        "Abbreviation:MaterialUtil::appendBoolAttr": "개명 예정 appendBoolAttribute — XML 특성을 붙인다",
    }
    selfTestCases = [
        {
            "name": "강제 약어가 아닌 대문자 묶음",
            "files": {"Source/Engine/Probe.h": "namespace sw\n{\n    struct Probe\n    {\n        void bindComputeUAV( int32 a );\n    };\n}\n"},
        },
        {
            "name": "initialize 대신 setup",
            "files": {"Source/Engine/Probe.h": "namespace sw\n{\n    struct Probe\n    {\n        bool setupLocalization( int32 a );\n    };\n}\n"},
        },
        {
            "name": "allocate 대신 alloc",
            "files": {"Source/Engine/Probe.h": "namespace sw\n{\n    struct Probe\n    {\n        void* allocSrvDescriptor( int32 a );\n    };\n}\n"},
        },
        {
            "name": "술어가 check 로 시작한다",
            "files": {"Source/Engine/Probe.h": "namespace sw\n{\n    struct Probe\n    {\n        bool checkCollision( int32 a ) const;\n    };\n}\n"},
        },
        {
            "name": "같은 이름에 string_view 판과 hashed_string 판",
            "files": {"Source/Engine/Probe.h": "namespace sw\n{\n    struct Probe\n    {\n        bool hasAction( string_view action ) const;\n        bool hasAction( const hashed_string& action ) const;\n    };\n}\n"},
        },
        {
            "name": "setName 과 짝인 게터가 맨이름 name()",
            "files": {"Source/Engine/Probe.h": "namespace sw\n{\n    struct Probe\n    {\n        const utf8* name() const;\n        void setName( const utf8* pName );\n    };\n}\n"},
        },
        {
            "name": "make / create 대신 build (예외 표의 같은 함수 이름도 다른 파일에서는 잡는다)",
            "files": {"Source/Engine/Probe.h": "namespace sw\n{\n    struct Probe\n    {\n        void build( int32 a );\n    };\n}\n"},
        },
        {
            "name": "make / create 대신 generate",
            "files": {"Source/Engine/Probe.h": "namespace sw\n{\n    struct Probe\n    {\n        static uint64 generateNewId();\n    };\n}\n"},
        },
        {
            "name": "make / create 대신 construct",
            "files": {"Source/Engine/Probe.h": "namespace sw\n{\n    struct Probe\n    {\n        void constructEmpty( void* pMemory ) const;\n    };\n}\n"},
        },
        {
            "name": "함수 이름에 줄임말 Attr",
            "files": {"Source/Engine/Probe.h": "namespace sw\n{\n    struct Probe\n    {\n        static void appendIntAttr( int32 value );\n    };\n}\n"},
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--files", nargs="*", default=None, help="이 파일들만 검사 (pre-commit 용)")

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        # `--files` 를 빈 목록으로 준 것(= staged 헤더 없음)과 아예 주지 않은 것(= 전수 검사)은 다르다.
        headers = [] if args.files == [] else self.selectTargetFiles(repositoryRoot, args.files, listScanRoot=_kScanRoots,
                                                                     suffixes=_kHeaderSuffix)
        if not headers:
            return GateResult(summary="검사할 헤더가 없습니다")

        violations: list[str] = []
        for fileViolations in mapConcurrent(lambda path: scanFileInternal(path, repositoryRoot), headers):
            violations.extend(fileViolations)
        return GateResult(listViolation=violations, summary=f"{len(headers)} headers scanned")


main = CheckFunctionVocabularyGate.run


if __name__ == "__main__":
    sys.exit(main())
