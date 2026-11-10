#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/Rules.py

커밋 린트 규칙 표 — **린트 규칙은 여기 한 곳에만 있다.** `py -3 -m Scripts lint --list` 가 이 표를 찍는다.

린트는 형식 · 빌드로 막지 못하는 틈에만 둔다(CLAUDE.md 원칙 1). 규칙을 더하기 전에 형식 · API · 빌드 경계(`Config/Rules/*.toml` 의 층 ·
타깃 게이트 `#error` 껍데기)로 막을 수 없는지 먼저 본다 — 표의 `whyNotType` 칸을 한 줄로 채우지 못하면 린트가 아니다.
언리얼 UBT 의 include 규칙 · clang-tidy 의 `.clang-tidy` 처럼 "무엇을 검사하는가" 를 한 목록으로 둔다.

칸:
- `id`         : `py -3 -m Scripts lint <id> [인자…]` — 그 규칙 모듈의 단독 실행(`--all` · `--self-check` 등은 모듈마다).
- `group`      : 갈래(형식 · 경계 · 모양 · 이름 · 셰이더 · 참조 · 래칫).
- `blocks`     : 무엇을 막는가.
- `whyNotType` : 왜 형식 · 빌드로 막지 못하는가.
- `trigger`    : 스테이지된 파일 중 이 접미사가 하나라도 있으면 커밋 훅이 돈다(`*` 는 늘).
- `check`      : `모듈:함수`. `perFile` 이면 `(projectRoot, 스테이지된 파일 목록)`, 아니면 `(projectRoot)` 를 받는다. 위반 줄 목록을 돌려준다.
- `exceptions` : 예외 · 기준선 파일(없으면 빈 글). 예외에는 까닭을 함께 적는다.

규칙 데이터는 `Config/Rules/` 한 폴더다 — 빌드가 막는 층 · 타깃 경계 표는 `Config/Rules/Layers/`(CMake · 생성기 · 린트가 함께 읽는다),
린트의 예외 · 기준선 · 낱말 표는 `Config/Rules/Lint/`.
"""

from __future__ import annotations

from dataclasses import dataclass

kCpp = (".h", ".hpp", ".cpp", ".c", ".inl", ".xxx")
kShader = (".hlsl", ".hlsli")
kCMake = ("CMakeLists.txt", ".cmake")
kAny = ("*",)


@dataclass(frozen=True)
class Rule:
    id: str
    group: str
    blocks: str
    whyNotType: str
    trigger: tuple[str, ...]
    check: str
    perFile: bool = False
    exceptions: str = ""

    def getModuleName(self) -> str:
        return self.check.partition(":")[0]


kRules: tuple[Rule, ...] = (
    # --- 형식(고치는 것은 `format` · `format-includes`) -------------------------------------------------------------------------
    Rule("include-order", "형식", "include 순서 · 묶음", "include 줄의 순서는 컴파일러가 보지 않는다", kCpp, "lint.IncludeOrder:checkStaged", perFile=True),
    # --- 경계(빌드 게이트가 닿지 않는 자리) ------------------------------------------------------------------------------------
    Rule("game-gpu-includes", "경계", "게임 코드가 GPU 칸 배치 헤더(Engine/Graphics/RHI · Shader)를 직접 include", "게이트는 전이 include 도 막는데 게임이 쓰는 엔진 공개 헤더가 RHI 헤더를 include 한다 — 직접 include 만 가르지 못한다", kCpp, "lint.GameGPUIncludes:checkGameGPUIncludes"),
    Rule("layer-cycles", "경계", "같은 엔진 층의 폴더끼리 서로 include(층 안 순환)", "같은 층은 한 부분이라 #error 껍데기가 없다 — 폴더마다 부분을 나누면 층 번호가 폴더 순서를 흉내 낼 뿐이다", kCpp + (".toml",), "lint.LayerCycles:checkLayerCycles", exceptions="Config/Rules/Layers/EngineLayers.toml [sameTierCycle]"),
    Rule("editor-view-includes", "경계", "MVVM 으로 옮긴 에디터 View 가 모델 · 씬 헤더를 include", "옮기는 동안의 목록 — 다 옮기면 View 부분의 #error 껍데기로 바꾼다", kCpp, "lint.EditorViewIncludes:checkEditorViewIncludes", exceptions="Config/Rules/Lint/EditorViewIncludes.json"),
    Rule("thread-owners", "경계", "TaskManager 밖에서 스레드를 만드는 파일", "스레드 형식은 합법이라 만드는 자리를 형식으로 막지 못한다", kCpp, "lint.ThreadOwners:checkThreadOwners", exceptions="Config/Rules/Lint/ThreadOwners.json"),
    Rule("cmake-path-case", "경계", "대소문자만 다른 CMake include · add_subdirectory 경로", "CMake 경로는 컴파일러가 보지 않는다(Linux 구성만 선다)", kCMake, "lint.CMakePathCase:checkCMakePathCase"),
    Rule("folder-layout", "경계", "잡동사니 폴더 · 엔진 쪽 Dev/ · Component 폴더", "파일 자리는 형식이 아니다", kAny, "lint.FolderLayout:checkFolderLayout", exceptions="Config/Rules/Lint/FolderLayout.json"),
    Rule("platform-chains", "경계", "빠진 플랫폼을 컴파일 오류로 내지 않는 #if 사슬", "전처리 갈래는 형식이 막지 못한다", kCpp, "lint.PlatformChains:checkPlatformChains", exceptions="Config/Rules/Lint/PlatformChains.json"),
    # --- 모양(공통 틀이 있는데 손으로 다시 적은 것) ----------------------------------------------------------------------------
    Rule("cpp-shape", "모양", "선언 순서 · 깃발 칸 · 익명 네임스페이스 · 주인 우선 · 기반 하나", "합법 C++ 의 배치 규칙이다", kCpp, "lint.CppShape:checkCppShape", perFile=True, exceptions="Config/Rules/Lint/CppShapeExceptions.json"),
    Rule("state-shape", "모양", "손으로 짝지은 writeState · readState( Archive& )", "스냅샷(ComponentStateStore)은 serializeState 만 받지만, Archive 는 바이트 그릇이라 그것을 받는 멤버 짝을 새로 짓는 것은 형식으로 막지 못한다", kCpp, "lint.StateShape:checkStateShape", perFile=True),
    Rule("byte-read", "모양", "파일에서 읽은 개수를 믿고 할당 · LittleEndianUtil 직접 읽기", "vector::resize 는 어떤 수든 받는다", kCpp, "lint.ByteReadShape:checkByteReadShape", perFile=True, exceptions="Config/Rules/Lint/ByteReadRawFiles.json"),
    Rule("catalog-root", "모양", "게임 데이터 루트의 원소를 손으로 고르기", "findChild 는 어디서든 부를 수 있다", kCpp, "lint.CatalogRootShape:checkCatalogRootShape", perFile=True, exceptions="Config/Rules/Lint/CatalogRootLoopFiles.json"),
    Rule("common-shapes", "모양", "공통 기반을 두고 손으로 적은 모양(열거 이름 표 · 다시 읽기 대조 · id 를 버리는 입구 …)", "칸 이름 · switch 글자 · 부른 함수는 형식이 말하지 못한다", kCpp, "lint.CommonShapes:checkCommonShapes", exceptions="Config/Rules/Lint/CommonShapeExceptions.json"),
    Rule("mutable-statics", "모양", "주인 객체 밖의 가변 정적 저장소(감춘 싱글턴)", "함수 정적 · 이름공간 범위 변수는 합법 C++ 이다", kCpp, "lint.MutableStatics:checkMutableStatics", exceptions="Config/Rules/Lint/MutableStaticExceptions.json"),
    Rule("frame-intern", "모양", "틱 · 갱신 · 칠하기 함수 안의 리터럴 hashed_string", "프레임 밖에서는 정상 코드다", kCpp, "lint.FrameIntern:checkFrameIntern", exceptions="Config/Rules/Lint/FrameInternExceptions.json"),
    Rule("tick-scene-walk", "모양", "컴포넌트 틱에서 닿는 씬 전체 순회", "순회는 틱 밖에서는 정상 코드다", kCpp, "lint.TickSceneWalk:checkTickSceneWalk", exceptions="Config/Rules/Lint/TickSceneWalkExceptions.json"),
    Rule("name-path-arguments", "모양", "경로 · 리소스 id 를 받는 함수에 hashed_string 의 c_str() · view() 를 그대로 넘김(빈 이름은 \"None\" 이다 — viewOrEmpty)", "받는 쪽이 string_view 라 어떤 글이든 들어간다", kCpp, "lint.NamePathArguments:checkNamePathArguments", exceptions="Config/Rules/Lint/NamePathArguments.json"),
    Rule("disk-path-case", "모양", "경로 포함 · 같음 판정(isPathAtOrUnderRoot · startsWithPathComponent …)에 소문자로 접은 글(normalizePath · toLower)을 넘김", "normalizePath 의 결과는 맵 키 · id 로도 쓰는 string 이고 판정은 string_view 를 받는다 — 출처를 형식이 가르지 못한다", kCpp, "lint.DiskPathCase:checkDiskPathCase", exceptions="Config/Rules/Lint/DiskPathCase.json"),
    Rule("optional-minus-one", "모양", "optional · expected 자리(반환 · 대입 · 초기값 · 기본 인자 · 인자)에 리터럴 -1 을 값으로", "표지 상수는 형식(InvalidIndex)이 막지만 리터럴 -1 은 정수라 optional<int32> 의 값으로 합법이다", kCpp, "lint.OptionalMinusOne:checkOptionalMinusOne"),
    Rule("required-name-fields", "모양", "이름 · 경로 PROPERTY 칸(뼈 · 소켓 · 액션 · 태그 …)의 C++ 문자열 기본값 — 데이터가 안 적으면 맞지 않는 이름이 조용히 쓰인다(Required 또는 예외)", "리터럴 초기값은 합법 C++ 이고, 그 값이 형식뿐인 빈 값인지 데이터가 덮을 기본값인지 형식이 말하지 못한다(이름 전용 형식은 생성기 설계 W-R3 와 겹친다)", (".h", ".cpp"), "lint.RequiredNameFields:checkRequiredNameFields", exceptions="Config/Rules/Lint/RequiredNameFieldExceptions.json"),
    Rule("editor-widget-labels", "모양", "칸 오른쪽에 붙는 ImGui 입력 위젯 이름표", "이름표는 문자열 내용이다", kCpp, "lint.EditorWidgetLabels:checkEditorWidgetLabels", perFile=True),
    # --- 이름 -------------------------------------------------------------------------------------------------------------
    Rule("naming-words", "이름", "표에 없는 함수 앞 단어 · 형식 끝 단어 · 금지 이름", "업계 용어인지는 형식이 말하지 못한다", kCpp, "lint.NamingWords:checkNamingWords", exceptions="Config/Rules/Lint/NamingWords.json"),
    Rule("acronyms", "이름", "식별자 · 파일 · 폴더의 약어 표기(모두 대문자)", "이름은 형식이 막지 못한다", kAny, "lint.Acronyms:checkAcronyms", exceptions="Config/Rules/Lint/Acronyms.json"),
    # --- 셰이더 -----------------------------------------------------------------------------------------------------------
    Rule("shader-kill-order", "셰이더", "픽셀 버리기(clip · discard) 뒤의 미분 읽기", "HLSL 에 미분을 쓰는 함수를 적을 형식이 없다", kShader, "lint.ShaderKillOrder:checkShaderKillOrder"),
    Rule("shader-branch-shape", "셰이더", "`if ( a && b ) … else if ( a )` 사슬(NVIDIA GL 오컴파일)", "분기 모양은 형식이 아니다", kShader, "lint.ShaderBranchShape:checkShaderBranchShape"),
    # --- 참조 · 생성물 ----------------------------------------------------------------------------------------------------
    Rule("feature-refs", "참조", "참조 0 인 컴포넌트 · 감시 값 · 개발 명령", "쓰는 곳이 있는지는 형식이 말하지 못한다", kCpp + (".xml",), "lint.FeatureReferences:checkFeatureReferences", exceptions="Config/Rules/Lint/FeatureReferenceExceptions.json"),
    Rule("client-only-types", "참조", "클라이언트 전용 형식 이름 표(Config/Engine/ClientOnlyTypes.generated.json)가 소스와 어긋남", "서버가 런타임에 읽는 생성 표다 — 고치는 것은 `py -3 -m Scripts client-only-types`", kCpp, "generate.ClientOnlyTypes:checkClientOnlyTypes"),
    # --- 래칫(늘지 않게만, 줄이면 `--accept`) -----------------------------------------------------------------------------
    Rule("magic-literals", "래칫", "일반 코드의 이름 없는 숫자 리터럴이 파일마다 늘어남", "리터럴이 매직인지는 형식이 가르지 못한다", kCpp, "lint.MagicLiterals:checkMagicLiterals", perFile=True, exceptions="Config/Rules/Lint/MagicLiteralBaseline.json"),
    Rule("result-types", "래칫", "std::optional · string& outError 모양(Core 0, 그 밖은 늘지 않게)", "언어가 주는 모양이다", kCpp, "lint.ResultTypes:checkResultTypes", perFile=True, exceptions="Config/Rules/Lint/ResultTypeBaseline.json"),
)

kRuleById: dict[str, Rule] = {rule.id: rule for rule in kRules}


def isTriggered(rule: Rule, listStagedName: list[str]) -> bool:
    """스테이지된 파일 이름 중 하나라도 규칙의 접미사로 끝나면 참이다."""
    if "*" in rule.trigger:
        return bool(listStagedName)
    return any(name.endswith(rule.trigger) for name in listStagedName)


def formatTable() -> str:
    """`lint --list` 가 찍는 표 — 갈래 · id · 막는 것 · 예외 파일."""
    listLine: list[str] = []
    group = ""
    for rule in kRules:
        if rule.group != group:
            group = rule.group
            listLine.append(f"\n[{group}]")
        suffix = f"  ({rule.exceptions})" if rule.exceptions else ""
        listLine.append(f"  {rule.id:<22} {rule.blocks}{suffix}")
        listLine.append(f"  {'':<22} 왜 린트: {rule.whyNotType}")
    return "\n".join(listLine)
