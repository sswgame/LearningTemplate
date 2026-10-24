#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
`CheckMutableStatics` 가 아직 살아 있는지 검사합니다 (음성 테스트 + 오탐 테스트).

**린트는 조용히 죽는다.** 규칙 하나가 정규식 한 글자 때문에 아무것도 못 잡게 되어도 결과는 "위반 0건" 이라 통과처럼 보인다.
그래서 모양마다 **일부러 어긴 조각**(잡혀야 한다)과 **어기지 않은 조각**(잡히면 오탐이다)을 둔다. 기대는 건수가 아니라 **걸린 변수 이름의 목록**이다 —
같은 건수라도 엉뚱한 줄이 걸렸다면 알아챈다. 각 이름 앞의 글자는 범주다(L 함수 안 · N 네임스페이스 수준 · M 클래스 멤버).

이 게이트는 이름 목록(`getInstance` · `instance` · `shared` …)에 기대지 않고 **모양**(정적 저장 기간의 변수, 상수가 아님)으로 판정한다.
첫 조각이 접근자 이름을 다섯 가지로 바꿔도 모두 걸리는 것을 보인다.

  python Scripts/lint/selftest/CheckMutableStaticsSelfTest.py [--root <repo>]
"""
from __future__ import annotations

import argparse
import re
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from lint.gate import CheckMutableStatics  # noqa: E402
from lint.LintGate import GateResult, LintGate  # noqa: E402
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)

_kViolationRe = re.compile(r"\[MutableStatic/(Local|Namespace|Member)\] (\w+) —")
_kMapCategoryLetter = {"Local": "L", "Namespace": "N", "Member": "M"}

# (이름, 파일 -> 내용, 걸려야 하는 "범주글자:변수 이름" — 순서 무관)
_kListCase: list[tuple[str, dict[str, str], list[str]]] = [
    # --- 어긴 조각: 함수 안 ---
    ("접근자 이름이 무엇이든(getXxx · instance · shared · cache · current) .cpp 익명 이름공간의 static T s",
     {"Source/Probe/A.cpp": ("namespace\n{\n    Foo& getFoo()\n    {\n        static Foo s_foo;\n        return s_foo;\n    }\n"
                             "    Foo& instance()\n    {\n        static Foo s_instance;\n        return s_instance;\n    }\n"
                             "    Foo& shared()\n    {\n        static Foo s_shared;\n        return s_shared;\n    }\n"
                             "    Foo* cache()\n    {\n        static Foo* s_pCache = new Foo();\n        return s_pCache;\n    }\n"
                             "    Foo& current()\n    {\n        static Foo s_current{};\n        return s_current;\n    }\n}\n")},
     ["L:s_foo", "L:s_instance", "L:s_shared", "L:s_pCache", "L:s_current"]),
    ("접근자가 아니어도 — static std::vector 캐시 · static int 카운터",
     {"Source/Probe/B.cpp": "void f()\n{\n    static std::vector<int> cache;\n    static int counter = 0;\n    ++counter;\n}\n"},
     ["L:cache", "L:counter"]),
    ("초기화 모양이 다른 선언 — 중괄호 · 괄호 · 배열 · 참조 · atomic · 템플릿 인자의 const",
     {"Source/Probe/C.cpp": ("void f()\n{\n    static Foo a{ 1 };\n    static Foo b( 2 );\n    static Foo arr[4];\n    static auto& r = g();\n"
                             "    static std::atomic<bool> bFlag{ false };\n    static std::vector<const Foo*> listConst;\n"
                             "    static std::map<int, std::vector<Foo>> map;\n}\n")},
     ["L:a", "L:b", "L:arr", "L:r", "L:bFlag", "L:listConst", "L:map"]),
    ("가리키는 쪽만 상수인 포인터 · constinit 은 상수가 아니다",
     {"Source/Probe/D.cpp": "void f()\n{\n    static const Foo* pCurrent = nullptr;\n    static const char* arrName[] = { \"a\", \"b\" };\n    static constinit int s_n = 0;\n}\n"},
     ["L:pCurrent", "L:arrName", "L:s_n"]),
    ("함수 포인터 변수 — 포인터 자체가 가변이면 걸리고 * const 면 상수다",
     {"Source/Probe/E.cpp": "void f()\n{\n    static void ( *pFn )( int ) = nullptr;\n    static void ( *const pFixed )( int ) = &g;\n}\n"},
     ["L:pFn"]),
    ("헤더의 인라인 함수 · 클래스 안 인라인 멤버 함수 · 템플릿 함수",
     {"Source/Probe/F.h": ("inline Registry& probe()\n{\n    static Registry s_registry;\n    return s_registry;\n}\n"
                           "struct Box\n{\n    static Box& make()\n    {\n        static Box s_box;\n        return s_box;\n    }\n};\n"
                           "template <typename T>\nT& slot()\n{\n    static T s_slot;\n    return s_slot;\n}\n")},
     ["L:s_registry", "L:s_box", "L:s_slot"]),
    ("깊이 든 자리 — if 블록 · 람다 · 네임스페이스 수준 람다 초기화 · 생성자 몸(초기화 목록의 중괄호 뒤) · 클래스 안 정의 함수",
     {"Source/Probe/G.cpp": ("void f( bool b )\n{\n    if ( b )\n    {\n        static int s_inBranch = 0;\n    }\n"
                             "    auto lambda = []()\n    {\n        static int s_inLambda = 0;\n        return s_inLambda;\n    };\n}\n"
                             "static const auto kCounter = []()\n{\n    static int s_n = 0;\n    return ++s_n;\n};\n"
                             "Foo::Foo()\n    : _a{ 1 }\n    , _b( 2 )\n{\n    static int s_inCtor = 0;\n}\n"
                             "struct Local\n{\n    void run()\n    {\n        static int s_inMember = 0;\n    }\n};\n")},
     ["L:s_inBranch", "L:s_inLambda", "L:s_n", "L:s_inCtor", "L:s_inMember"]),
    ("Test 폴더와 도구 폴더도 본다",
     {"Test/Probe/H.cpp": "void f()\n{\n    static int s_testCount = 0;\n}\n", "Tools/OnlineLoadBot/I.cpp": "void f()\n{\n    static int s_botCount = 0;\n}\n"},
     ["L:s_testCount", "L:s_botCount"]),
    # --- 어긴 조각: 네임스페이스 수준 · 클래스 멤버 · thread_local ---
    ("익명 이름공간 · static · ScopeOwned 로 감싼 칸 — 네임스페이스 수준의 가변 변수",
     {"Source/Probe/J.cpp": ("namespace\n{\n    ScopeOwned<vector<int>> s_listOwner;\n    vector<int> g_list;\n    int g_count = 0;\n    Foo g_foo{ 1 };\n"
                             "    auto g_lambda = []( int x ) { return x; };\n    mutex g_mutex;\n}\n"
                             "static atomic<uint32> s_serial{ 0 };\nstatic string s_path;\nalignas( 16 ) uint8 g_arrBuffer[16];\n")},
     ["N:s_listOwner", "N:g_list", "N:g_count", "N:g_foo", "N:g_lambda", "N:g_mutex", "N:s_serial", "N:s_path", "N:g_arrBuffer"]),
    ("클래스의 가변 static 데이터 멤버 — static inline · inline static · 선언만 두고 밖에서 정의(선언만 센다)",
     {"Source/Probe/K.cpp": ("namespace\n{\n    struct FooInternal\n    {\n        inline static ScopeOwned<vector<int>> s_bufferOwner{};\n"
                             "        static inline atomic<uint32> s_serial{ 0 };\n        static int s_count;\n    };\n    int FooInternal::s_count = 0;\n}\n")},
     ["M:s_bufferOwner", "M:s_serial", "M:s_count"]),
    ("헤더 클래스의 가변 static 멤버",
     {"Source/Probe/L.h": "class Cache\n{\npublic:\n    static inline std::vector<int> s_list{};\n    static Cache* s_pCurrent;\n};\n"},
     ["M:s_list", "M:s_pCurrent"]),
    ("thread_local — 네임스페이스 수준 · 함수 안 · 클래스 멤버 (const 면 아니다)",
     {"Source/Probe/M.cpp": ("namespace\n{\n    thread_local uint32 t_depth = 0;\n    thread_local const int t_kConst = 1;\n}\n"
                             "void f()\n{\n    thread_local ThreadCache t_cache{};\n    static thread_local bool t_bBusy = false;\n    thread_local static std::vector<int> t_scratch;\n}\n"
                             "struct Hook\n{\n    static inline thread_local bool t_bInside = false;\n};\n")},
     ["N:t_depth", "L:t_cache", "L:t_bBusy", "L:t_scratch", "M:t_bInside"]),
    # --- 어기지 않은 조각 ---
    ("상수는 허용 — const · constexpr · const 포인터 상수 · 이름표 · 표 · 정렬 지정(함수 안)",
     {"Source/Probe/N.cpp": ("void f()\n{\n    static const int kMax = 4;\n    static constexpr int kLimit = 8;\n    static const hashed_string kName{ \"name\" };\n"
                             "    static constexpr Row kTable[] = { { 1 }, { 2 } };\n    static const Foo* const pFixed = nullptr;\n"
                             "    static const std::string kText;\n    static Foo const kBackwards{};\n    static const Foo& rConst = g();\n"
                             "    alignas( 16 ) static constexpr float arrWeight[4] = {};\n    constexpr static int kOrder = 1;\n"
                             "    thread_local static const int t_kLimit = 2;\n}\n")},
     []),
    ("상수는 허용 — 네임스페이스 수준 · 클래스 멤버",
     {"Source/Probe/O.cpp": ("namespace\n{\n    constexpr int kMax = 4;\n    const hashed_string kName{ \"name\" };\n    static constexpr Row kTable[] = { { 1 }, { 2 } };\n"
                             "    const char* const kArrName[] = { \"a\", \"b\" };\n    inline constexpr float kPi = 3.14f;\n"
                             "    struct FooInternal\n    {\n        static constexpr int kSlots = 4;\n        static inline const hashed_string kTag{ \"tag\" };\n    };\n}\n")},
     []),
    ("static_assert · static_cast · 주석과 문자열의 static",
     {"Source/Probe/P.cpp": ("void f()\n{\n    static_assert( sizeof( int ) == 4 );\n    auto p = static_cast<int*>( q );\n"
                            "    // static int s_comment = 0;\n    const char* text = \"static int s_text = 0;\";\n}\n")},
     []),
    ("클래스 멤버 — 인스턴스 멤버 · static 함수 선언과 인라인 정의 · 상수 · enum",
     {"Source/Probe/Q.h": ("struct Box\n{\n    static const int kMax = 3;\n    int _count = 0;\n    vector<int> _list;\n"
                           "    static Box make() { return Box(); }\n    static void run();\n    static bool operator==( const Box& a, const Box& b );\n};\n"
                           "enum class Mode { A, B };\n")},
     []),
    ("네임스페이스 수준 — 함수 선언 · 정의 · using · typedef · extern · 전방 선언 · 전역 변수 매크로 · 템플릿",
     {"Source/Probe/R.cpp": ("namespace\n{\n    static void declared();\n    static int helper()\n    {\n        return 1;\n    }\n    void plain( int x );\n"
                             "    using List = vector<int>;\n    typedef int Handle;\n    extern int g_external;\n    class Forward;\n    struct Pod;\n    enum class Mode : uint8;\n"
                             "    template <typename T> inline T g_slot{};\n}\n"
                             "SW_GLOBAL_VARIABLE( int32, gv_viewMode, 0, \"view\" );\nSW_TEST_GLOBAL_VARIABLE( bool, gv_bench, false, \"bench\" );\n"
                             "static void other( int x )\n{\n    int local = x;\n}\n")},
     []),
    ("클래스 밖에서 하는 static 멤버의 정의(`int S::s_n = 0;`)는 선언 쪽이 세므로 다시 세지 않는다",
     {"Source/Probe/S.cpp": "int Counter::s_count = 0;\nvector<int> Counter::s_list;\n"},
     []),
    ("람다 안의 비정적 변수",
     {"Source/Probe/T.cpp": "void f()\n{\n    auto lambda = []( int x )\n    {\n        int count = x;\n        Foo local;\n        return count;\n    };\n}\n"},
     []),
    ("전처리 줄(매크로 본문)과 생성 파일 · ThirdParty",
     {"Source/Probe/U.cpp": "#define KEEP_ONCE do { static int s_once = 0; } while ( 0 )\nvoid f() {}\n",
      "Source/Probe/V.gen.cpp": "void f()\n{\n    static int s_gen = 0;\n}\n",
      "Source/ThirdParty/W.cpp": "void f()\n{\n    static int s_third = 0;\n}\n"},
     []),
]


def violationNames(listViolation: list[str]) -> list[str]:
    """위반 줄에서 걸린 `범주글자:변수 이름` 만 뽑습니다."""
    listName: list[str] = []
    for line in listViolation:
        match = _kViolationRe.search(line)
        if match is not None:
            listName.append(f"{_kMapCategoryLetter[match.group(1)]}:{match.group(2)}")
    return listName


class CheckMutableStaticsSelfTestGate(LintGate):
    description = "CheckMutableStatics 음성 · 오탐 테스트 — 모양마다 잡히는지, 상수 · 멤버 · 선언이 안 잡히는지"
    buildComment = "Checking that CheckMutableStatics still catches every mutable static-storage shape..."
    timeoutSeconds = 60
    selfTestSkipReason = "린트를 보는 린트 — 조각을 들지 않는다(대상이 린트 폴더 자체다)"
    violationHeader = "문제"

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listError: list[str] = []
        tempRoot = Path(tempfile.mkdtemp(prefix="swMutableStaticsSelfTest"))
        try:
            for caseIndex, (name, mapFile, listExpected) in enumerate(_kListCase):
                caseRoot = tempRoot / f"case{caseIndex}"
                for relative, content in mapFile.items():
                    path = caseRoot / relative
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text(content, encoding="utf-8", newline="\n")
                listFound = violationNames(CheckMutableStatics.findMutableStaticViolationsInTree(caseRoot, None))
                if sorted(listFound) != sorted(listExpected):
                    listError.append(f"{name}: 기대 {sorted(listExpected)}, 실제 {sorted(listFound)}")
        finally:
            shutil.rmtree(tempRoot, ignore_errors=True)
        return GateResult(listViolation=listError, summary=f"{len(_kListCase)} cases")


main = CheckMutableStaticsSelfTestGate.run


if __name__ == "__main__":
    sys.exit(main())
