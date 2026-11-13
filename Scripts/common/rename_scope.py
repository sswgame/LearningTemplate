"""이름 · 경로 바꾸기 도구(`move-source` · `rename-symbol` · `Scripts/dev/rename_identifier.py`)가 함께 쓰는 예외 규칙.

문서도 코드와 같이 바꾼다 — 문서를 건너뛰면 개명마다 규칙 문서(`docs/agents/`)의 이름 · 경로가 낡는다.
옛 이름을 일부러 적은 줄("쓰지 않는 것" 예, 이력)만 그 줄에 표지 `keep-old-name` 를 적어 남긴다(Markdown 은 `<!-- keep-old-name -->`).
린트 예외 표(`Config/Rules/Lint/*.json`)도 지금 이름을 가리키니 함께 고친다 — 옛 이름을 일부러 적는 두 곳만 지킨다(`rewriteRenameTarget`).
"""

from __future__ import annotations

import json
import re
from typing import Callable, Mapping

KEEP_OLD_NAME_MARKER = "keep-old-name"
NAMING_WORDS_PATH = "Config/Rules/Lint/NamingWords.json"     # `_bannedNames` 의 키가 옛 이름이다
NAMING_BASELINE_PATH = "Config/Rules/Lint/NamingBaseline.json"   # 남은 위반 이름 — `--lower-baseline` 만 줄인다(새 이름이 또 위반이면 린트가 지게)
RENAME_PAIRS_FOLDER = "Config/Rules/Renames/"   # 병합 뒤 다시 돌릴 쌍 표(`rename_identifier.py --reapply`) — 옛 이름이 열쇠다


def dump_naming_table(data: dict) -> str:
    return json.dumps(data, ensure_ascii=False, indent=2) + "\n"


def rewrite_rename_target(relative_path: str, text: str, rewrite: Callable[[str], str]) -> str:
    """
    이름 바꾸기 도구가 파일 하나(@p relative_path — 저장소 기준 posix)를 고친다. 두 도구(`rename-symbol` · `rename_identifier.py`)가 이 함수 하나를 쓴다 —
    한 도구는 린트 표를 통째로 건너뛰고 한 도구는 금지 이름 표의 키까지 바꿔, 개명마다 예외 표가 낡거나 금지 이름이 지워졌다.
    """
    if relative_path == NAMING_BASELINE_PATH or relative_path.startswith(RENAME_PAIRS_FOLDER):
        return text
    if relative_path == NAMING_WORDS_PATH:
        data = json.loads(text)
        banned = data.get("_bannedNames", {})
        data["_bannedNames"] = {}
        rewritten = json.loads(rewrite_unmarked_lines(dump_naming_table(data), rewrite))
        rewritten["_bannedNames"] = {old: rewrite_unmarked_lines(advice, rewrite) for old, advice in banned.items()}   # 키(옛 이름)는 지키고 안내의 새 이름은 따라간다
        return dump_naming_table(rewritten)
    return rewrite_unmarked_lines(text, rewrite)


def rewrite_unmarked_lines(text: str, rewrite: Callable[[str], str]) -> str:
    """@p text 의 줄 가운데 표지가 없는 줄에만 @p rewrite 를 적용한다(표지가 없는 글은 한 번에 바꾼다)."""
    if KEEP_OLD_NAME_MARKER not in text:
        return rewrite(text)
    return "".join(line if KEEP_OLD_NAME_MARKER in line else rewrite(line) for line in text.splitlines(keepends=True))


# ── 리플렉션 프로퍼티의 직렬화 이름 ──
# `PROPERTY(...)` 가 붙은 멤버 `_bWaitForStability` 는 XML · JSON 에 `waitForStability` 로 적힌다(`XMLFields::getAttributeName` · `JSONFields`).
# 멤버 이름만 바꾸면 데이터의 속성이 모르는 이름이 된다 — 개명 도구가 같은 규칙으로 직렬화 이름 쌍을 만들어 데이터에도 적용한다.
# (`XMLSerializer` 는 멤버 이름 `_foo` 를 글자 그대로 적으므로 낱말 치환이 이미 따라간다.)

COMMENT = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
PROPERTY_OPEN = re.compile(r"^(?P<head>[^\n]*?)\bPROPERTY\s*\(", re.M)
FIELD_NAME = re.compile(r"\s*[\w:<>,\s*&()]+?[\s*&>](_[A-Za-z]\w*)\s*(?:[{=;\[:]|SW_)")   # `타입 _이름` 뒤에 초기값 · 배열 · 비트필드 · 스레드 주석 · 끝
EXPLICIT_NAME = re.compile(r'\bName\s*=\s*"([^"]+)"')


def make_serialized_name(member: str) -> str:
    """멤버 이름의 XML 속성 · JSON 키 이름 — `XMLFields::getAttributeName`(Source/Engine/Serialization/XML/XMLFields.cpp) ·
    `makeXMLAttributeName`(ReflectionParser/ReflectionPipeline.cpp)과 같은 규칙이다. 규칙을 바꾸면 셋을 함께 고친다."""
    if not member.startswith("_"):
        return member
    name = member[1:]
    if len(name) > 1 and name[0] == "b" and "A" <= name[1] <= "Z":
        return name[1].lower() + name[2:]
    return name


def collect_reflected_properties(map_header: Mapping[str, str]) -> dict[str, list[tuple[str, str]]]:
    """헤더(경로 → 글)에서 `PROPERTY(...)` 가 붙은 칸을 모은다: 리플렉션 이름 → [(경로, 직렬화 이름)].
    리플렉션 이름은 `Name = "x"` 를 적었으면 그것(멤버 이름을 바꿔도 데이터 이름은 그대로다), 아니면 `_` 멤버 이름이다."""
    properties: dict[str, list[tuple[str, str]]] = {}
    for path, raw in map_header.items():
        if "PROPERTY" not in raw:
            continue
        text = COMMENT.sub(" ", raw)
        for match in PROPERTY_OPEN.finditer(text):
            if "#" in match.group("head"):   # 매크로 정의
                continue
            depth, end = 1, match.end()
            while depth and end < len(text):
                depth += {"(": 1, ")": -1}.get(text[end], 0)
                end += 1
            named = EXPLICIT_NAME.search(text, match.end(), end) or FIELD_NAME.match(text, end)
            if named is not None:
                properties.setdefault(named.group(1), []).append((path, make_serialized_name(named.group(1))))
    return properties


def make_serialized_pattern(name: str) -> re.Pattern[str]:
    """직렬화 이름이 적히는 꼴만 — 속성 · `이름=값` 토큰(`name=`), 요소(`<name>` · `</name>`), 따옴표로 감싼 키(`"name"`). 글 속 영어 낱말은 건드리지 않는다.
    shortcut: `name = "x"` 처럼 `=` 앞을 띄운 XML 속성은 보지 않는다(저장소 XML 에 없다), 그런 데이터가 생기면 XML 파일에만 공백을 허용한다."""
    word = re.escape(name)
    return re.compile(rf'(?<![\w.-]){word}(?==[^=])|(?<=<){word}(?=[\s/>])|(?<=</){word}(?=>)|(?<="){word}(?=\\?")')


def plan_serialized_rename(properties: Mapping[str, list[tuple[str, str]]], map_text: Mapping[str, str], old: str, new: str) -> tuple[tuple[str, str] | None, list[str]]:
    """
    멤버 개명 @p old → @p new 가 데이터에서 함께 바꿀 직렬화 이름 쌍과, 바꿀 수 없는 까닭(있으면 도구가 멈춘다 — 폴백 없음).
    병합 뒤 다시 돌릴 때는 선언이 이미 새 이름이라 두 이름 모두로 찾는다. @p map_text 는 치환할 파일(경로 → 글)이다.
    """
    serialized_old, serialized_new = make_serialized_name(old), make_serialized_name(new)
    if (old not in properties and new not in properties) or serialized_old == serialized_new:
        return None, []
    pattern = make_serialized_pattern(serialized_old)
    list_use = [path for path, text in map_text.items() if serialized_old in text and pattern.search(text)]
    if not list_use:
        return None, []
    # 이름만으로는 어느 타입의 칸인지 모른다 — 같은 직렬화 이름을 쓰는 다른 멤버가 있으면 데이터의 그 이름이 누구 것인지 가릴 수 없다.
    list_other = [f"{member}({path})" for member, list_entry in properties.items() if member not in (old, new) for path, serialized in list_entry if serialized == serialized_old]
    where = ", ".join(list_use[:10]) + (" …" if len(list_use) > 10 else "")
    if list_other:
        return None, [f"직렬화 이름 '{serialized_old}' 를 다른 리플렉션 멤버도 쓴다: {', '.join(list_other[:10])} — 데이터({where})의 그 이름이 어느 것인지 가릴 수 없다"]
    if len(serialized_old) < 6 or re.search(r"[a-z0-9][A-Z]", serialized_old) is None:
        return None, [f"직렬화 이름 '{serialized_old}' 는 흔한 낱말이라 손으로 읽는 속성과 겹칠 수 있다 — 데이터({where})"]
    return (serialized_old, serialized_new), []


def replace_serialized_name(text: str, serialized_old: str, serialized_new: str) -> tuple[str, int]:
    if serialized_old not in text:
        return text, 0
    return make_serialized_pattern(serialized_old).subn(lambda _: serialized_new, text)


def check_serialized_rename() -> None:
    """실제 사례(e15f47d876): 멤버 `_bWaitForStability` 로 옮긴 개명이 시나리오 13 개의 옛 속성 이름을 남겼다."""
    assert make_serialized_name("_bWaitForStability") == "waitForStability" and make_serialized_name("_fileName") == "fileName"
    assert make_serialized_name("_bias") == "bias" and make_serialized_name("_b") == "b" and make_serialized_name("clips") == "clips"
    header = (
        "#define PROPERTY( ... )\n"
        "struct Shot\n{\n"
        "    // PROPERTY() 설명 속 _notAField;\n"
        "    PROPERTY( Required )\n    string _fileName;\n"
        "    PROPERTY( Min = min( 0, 1 ) )\n    bool32 _bHoldFrame : 1;\n"
        "    PROPERTY( Name = \"clips\" )\n    vector<string> _listClipName{};\n"
        "    PROPERTY()\n    AssetRef<Texture>* _pMainTexture{ nullptr };\n"
        "    float32 _plainField;\n};\n"
        "struct Other\n{\n    PROPERTY()\n    float32 _holdFrame;\n};\n"
    )
    properties = collect_reflected_properties({"Shot.h": header})
    assert properties == {"_fileName": [("Shot.h", "fileName")], "_bHoldFrame": [("Shot.h", "holdFrame")], "clips": [("Shot.h", "clips")],
                      "_pMainTexture": [("Shot.h", "pMainTexture")], "_holdFrame": [("Shot.h", "holdFrame")]}, properties
    data = {"a.scenario.xml": '<Shot fileName="a.ppm" holdFrame="true" otherFileName="x"><fileName>b</fileName></Shot>',
            "Steps.cpp": 'find( "fileName" ); parse( "Shot fileName=a" ); string fileName = "a"; if ( fileName == b )',
            "notes.md": "the fileName of a shot"}
    pair, problems = plan_serialized_rename(properties, data, "_fileName", "_outputPath")
    assert pair == ("fileName", "outputPath") and not problems, (pair, problems)
    assert replace_serialized_name(data["a.scenario.xml"], *pair) == ('<Shot outputPath="a.ppm" holdFrame="true" otherFileName="x"><outputPath>b</outputPath></Shot>', 3)
    assert replace_serialized_name(data["Steps.cpp"], *pair) == ('find( "outputPath" ); parse( "Shot outputPath=a" ); string fileName = "a"; if ( fileName == b )', 2)
    assert replace_serialized_name(data["notes.md"], *pair) == (data["notes.md"], 0), "글 속 낱말은 남긴다"
    # 병합 뒤 다시 돌리기: 선언은 이미 새 이름이다
    renamed = collect_reflected_properties({"Shot.h": header.replace("_fileName", "_outputPath")})
    assert plan_serialized_rename(renamed, data, "_fileName", "_outputPath") == (("fileName", "outputPath"), [])
    # 겹침: `_bHoldFrame` 과 `_holdFrame` 이 같은 속성 이름이다 — 조용히 바꾸지 않고 멈춘다
    pair, problems = plan_serialized_rename(properties, data, "_bHoldFrame", "_bFreezeFrame")
    assert pair is None and len(problems) == 1 and "_holdFrame(Shot.h)" in problems[0] and "a.scenario.xml" in problems[0], problems
    # 데이터에 쓰인 곳이 없거나, 이름을 직접 적었거나(`Name =`), 리플렉션 칸이 아니면 할 일이 없다
    assert plan_serialized_rename(properties, {"notes.md": "holdFrame"}, "_bHoldFrame", "_bFreezeFrame") == (None, [])
    assert plan_serialized_rename(properties, {"a.xml": '<A clips="x" listClipName="y" plainField="1"/>'}, "_listClipName", "_listClipTitle") == (None, [])
    assert plan_serialized_rename(properties, {"a.xml": '<A plainField="1"/>'}, "_plainField", "_simpleField") == (None, [])
    # 흔한 낱말(한 낱말 · 짧은 이름)은 손으로 읽는 속성과 가릴 수 없다
    common = collect_reflected_properties({"T.h": "PROPERTY()\nfloat32 _tolerance;"})
    pair, problems = plan_serialized_rename(common, {"a.xml": '<Expect tolerance="1"/>'}, "_tolerance", "_errorMargin")
    assert pair is None and len(problems) == 1, problems


def check_rewrite_unmarked_lines() -> None:
    """표지 줄은 지키고 나머지 줄은 바꾸는지 본다."""
    rename = lambda part: part.replace("OldName", "NewName")  # noqa: E731
    text = "use `OldName`\nnot `OldName` <!-- keep-old-name -->\n`OldName` again\n"
    assert rewrite_unmarked_lines(text, rename) == "use `NewName`\nnot `OldName` <!-- keep-old-name -->\n`NewName` again\n"
    assert rewrite_unmarked_lines("`OldName`", rename) == "`NewName`"
    table = dump_naming_table({"_bannedNames": {"OldName": "NewName — x"}, "_keep": {"OldName": "y"}})
    assert json.loads(rewrite_rename_target(NAMING_WORDS_PATH, table, rename)) == {"_bannedNames": {"OldName": "NewName — x"}, "_keep": {"NewName": "y"}}
    assert rewrite_rename_target(NAMING_BASELINE_PATH, "OldName", rename) == "OldName"
    assert rewrite_rename_target(RENAME_PAIRS_FOLDER + "CMakeFunctions.json", "OldName", rename) == "OldName"
    assert rewrite_rename_target("Config/Rules/Lint/ThreadOwners.json", "OldName", rename) == "NewName"
