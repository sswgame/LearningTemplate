"""
CheckCodeConventions — 위반 · 줄 훑기 문맥 · 규칙 레지스트리(`ConventionRule` 을 상속하면 범위별 목록에 오른다).

게이트 `Scripts/lint/gate/CheckCodeConventions.py` 의 한 묶음이다(묶음 지도는 `__init__.py`).
"""
from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path



# --- 1. 자료구조 정의 --------------------------------------------------------

@dataclass
class ConventionViolation:
    file_path: str
    line_number: int
    rule_category: str
    message: str
    snippet: str
    suggested_fix: str | None = None


# --- 2. 줄 단위 규칙 — 컨텍스트와 레지스트리 ---------------------------------

@dataclass
class LineScanContext:
    """
    줄 하나를 볼 때 규칙이 필요로 하는 것 전부.

    규칙을 이 타입 하나만 받는 함수로 만들면 **규칙마다 독립**이 된다 — 규칙끼리 `inBlockComment` · `classStack` ·
    중괄호 깊이 같은 상태를 직접 나눠 쓰지 않으므로, 규칙 하나를 고칠 때 그 규칙만 읽으면 된다.
    """
    relPath: str
    rootDir: Path
    lineNum: int
    line: str
    trimmed: str
    codeWithoutStrings: str
    isHeader: bool
    isSource: bool


# 규칙 하나 = 클래스 하나. **상속만 하면 등록된다** — 목록에 이름을 더할 필요가 없다.
#
# 규칙이 자기 **위반 조각**(`badSample`)도 같이 든다. 그래서 음성 테스트
# (`CheckCodeConventionsSelfTest.py`)가 조각 표를 따로 들지 않는다 — 규칙과 그 증거가 붙어 있으면
# 둘이 어긋날 수가 없다. 이 저장소가 반복해서 배운 것이다: **목록이 아니라 자리가 규칙이다.**
#
# 줄을 **어떤 자리로 읽을지**(함수 서명 · 함수 본문 · 클래스 본문 · 생성자 초기화 목록)는 `checkFileConventionsInternal` 의 파서가
# 한 번에 정한다 — 그 판정은 `if/elif` 사슬이라 규칙마다 따로 하면 동작이 바뀐다. 규칙은 판정된 자리를 **훅으로 받기만** 한다.
_kRulesByScope: dict[str, list] = {
    "line": [],                    # 모든 줄 — onLine
    "classMember": [],             # 클래스 본문 직속 줄 — onLine
    "functionLocal": [],           # 함수 본문 줄(생성자 초기화 목록 줄 제외) — onLine
    "parameter": [],               # 함수 · 람다 · 생성자 서명의 매개변수 하나 — onParameter
    "constructorInitializer": [],  # 생성자 초기화 목록의 줄 — onInitializerLine
    "file": [],                    # 파일 하나 — onFile
}


class ConventionRule:
    """
    줄 하나를 보고 위반을 돌려주는 규칙.

    새 규칙을 넣으려면 이 클래스를 상속해 `category` · `badSample` 을 적고 `onLine` 만 쓰면 된다.
    등록·자가 테스트 편입은 자동이다.

    - `category`     : 위반에 붙는 카테고리. 여러 개를 내는 규칙은 `categories` 를 쓴다.
    - `scope`        : 규칙이 받는 자리(`_kRulesByScope` 의 키). "line" · "classMember" · "functionLocal" 은 `onLine`,
                       "parameter" 는 `onParameter`, "constructorInitializer" 는 `onInitializerLine`, "file" 은 `onFile`.
    - `badSample`    : 이 규칙이 **반드시 잡아야 하는** 조각. 없으면 "덮이지 않은 카테고리" 로 잡힌다.
    - `badSampleFile`: 그 조각을 쓸 파일 이름. 헤더 전용 규칙은 `.h` 로 준다.
    - `extraSamples` : 카테고리를 여럿 내는 규칙이 나머지를 증명하는 조각들 `(파일 이름, 내용)`.

    파일 **짝**이 있어야 성립하는 검사(헤더+소스, 같은 이름의 다른 파일)는 여기 조각으로 못 적는다.
    그런 것은 자가 테스트의 `_kWholeScanCases` 에 남는다.
    """
    category: str = ""
    categories: tuple[str, ...] = ()
    scope: str = "line"
    badSample: str = ""
    badSampleFile: str = "Source/Probe/Sample.cpp"
    extraSamples: tuple[tuple[str, str], ...] = ()

    def __init_subclass__(cls, **kwargs: object) -> None:
        super().__init_subclass__(**kwargs)
        if cls.scope not in _kRulesByScope:
            raise ValueError(f"{cls.__name__}: 알 수 없는 scope '{cls.scope}'")
        _kRulesByScope[cls.scope].append(cls())

    @classmethod
    def allCategories(cls) -> tuple[str, ...]:
        return cls.categories if cls.categories else ((cls.category,) if cls.category else ())

    def onLine(self, ctx: LineScanContext) -> list[ConventionViolation]:
        raise NotImplementedError

    def onParameter(self, ctx: LineScanContext, parameterText: str) -> list[ConventionViolation]:
        """서명 한 줄에서 쪼갠 매개변수 하나(`const vector<int32>& items`)."""
        raise NotImplementedError

    def onInitializerLine(self, ctx: LineScanContext, initMatch: re.Match | None) -> list[ConventionViolation]:
        """생성자 초기화 목록의 줄. `initMatch` 는 `_kConstructorInitRe` 의 결과(멤버 · 값), 맞지 않으면 None."""
        raise NotImplementedError

    def onFile(self, ctx: LineScanContext, listLine: list[str]) -> list[ConventionViolation]:
        """파일 하나. `ctx.lineNum` 은 0 이다."""
        raise NotImplementedError
