"""
Scripts/common/ReflectionAnnotationKeys.py

편집기(clangd)가 `REFLECT` · `PROPERTY` · `FUNCTION` · `ENUM` · `REFLECT_CONTAINER` 의 인자 키를
완성하고 오타를 잡게 하는 선언 헤더(`Source/Engine/Reflection/ReflectionAnnotationKeys.h`)의 내용을 만듭니다.
파일을 쓰는 것은 `Scripts/generate/GenerateReflectionAnnotationKeys.py` 이고, 커밋된 파일이 이 내용과 같은지는
`Scripts/lint/gate/CheckReflectionAnnotationKeys.py` 가 봅니다.

키 목록은 생성기가 받는 철자 목록 그대로입니다. 손으로 두 번 적지 않습니다.

- 키와 철자: `Source/Core/Predefined/AnnotationMeta.txt` (생성기가 시작할 때 필드 표 `PredefinedAnnotationField.xxx` 와 대조한다)
- `Units = m` 의 맨 이름: `Source/Engine/Reflection/ReflectUnits.h` 의 `kArrReflectUnit` (식별자인 철자만 — `m/s` 는 따옴표로 쓴다)
- `REFLECT_CONTAINER( Sequence )` 의 종류: `Source/Core/Predefined/PredefinedContainerKind.xxx` (`None` 은 쓰지 않는다)

표준 라이브러리만 씁니다. 같은 입력이면 같은 글이 나옵니다.
"""
from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

kHeaderRelativePath = "Source/Engine/Reflection/ReflectionAnnotationKeys.h"
kAnnotationMetaRelativePath = "Source/Core/Predefined/AnnotationMeta.txt"
kUnitsRelativePath = "Source/Engine/Reflection/ReflectUnits.h"
kContainerKindRelativePath = "Source/Core/Predefined/PredefinedContainerKind.xxx"

# AnnotationMeta.txt 절 이름 → 선언 구조체 이름 · 키 이름 배열 이름
kListSection: list[tuple[str, str, str]] = [
    ("REFLECT", "ReflectKeys", "kArrReflectKey"),
    ("ENUM", "EnumKeys", "kArrEnumKey"),
    ("PROPERTY", "PropertyKeys", "kArrPropertyKey"),
    ("FUNCTION", "FunctionKeys", "kArrFunctionKey"),
]

# 값 종류(AnnotationMeta.txt 의 kind) → 쓰는 모양
_kMapKindUsage: dict[str, str] = {
    "flag": "단독 토큰 `{0}` 또는 `{0} = true`",
    "bool": "`{0} = true`",
    "string": "`{0} = \"…\"`",
    "float": "`{0} = 1.5`",
    "netrole": "단독 토큰 `{0}`(넷 역할)",
}

# 식별자로 선언할 수 없는 철자(C++ 키워드). 예: REFLECT 의 `static` — 편집기 완성에서만 빠지고 생성기는 그대로 받는다.
_kSetCppKeyword = frozenset({
    "alignas", "alignof", "and", "asm", "auto", "bool", "break", "case", "catch", "char", "class", "const", "constexpr",
    "continue", "decltype", "default", "delete", "do", "double", "else", "enum", "explicit", "export", "extern", "false",
    "float", "for", "friend", "goto", "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept", "not",
    "nullptr", "operator", "or", "private", "protected", "public", "register", "return", "short", "signed", "sizeof",
    "static", "struct", "switch", "template", "this", "throw", "true", "try", "typedef", "typeid", "typename", "union",
    "unsigned", "using", "virtual", "void", "volatile", "while",
})


@dataclass
class AnnotationKey:
    """철자 하나 — 이름, 정규 필드(`flag.ReadOnly`), 그 줄 위 `#` 주석."""

    name: str
    kind: str
    canonical: str
    listComment: list[str] = field(default_factory=list)


def readAnnotationSections(text: str) -> dict[str, list[AnnotationKey]]:
    """AnnotationMeta.txt 글 → 절 이름 → 철자 목록(파일 순서). 같은 절의 같은 철자는 처음 것만 둔다."""
    mapSection: dict[str, list[AnnotationKey]] = {}
    section = ""
    listPendingComment: list[str] = []
    for rawLine in text.splitlines():
        line = rawLine.strip()
        if not line:
            listPendingComment = []
            continue
        if line.startswith("#"):
            comment = line.lstrip("#").strip()
            if comment and not set(comment) <= set("=-"):
                listPendingComment.append(comment)
            continue
        matchSection = re.fullmatch(r"\[(\w+)\]", line)
        if matchSection:
            section = matchSection.group(1)
            mapSection.setdefault(section, [])
            listPendingComment = []
            continue
        matchEntry = re.fullmatch(r"(\w+)\.(\w+)\s*=\s*(.+)", line)
        if matchEntry is None or not section:
            raise ValueError(f"{kAnnotationMetaRelativePath}: 읽을 수 없는 줄: {rawLine!r}")
        kind, canonical = matchEntry.group(1), matchEntry.group(2)
        listKey = mapSection[section]
        for spelling in (part.strip() for part in matchEntry.group(3).split(",")):
            if spelling and all(key.name != spelling for key in listKey):
                listKey.append(AnnotationKey(spelling, kind, f"{kind}.{canonical}", list(listPendingComment)))
        listPendingComment = []
    return mapSection


def readUnitNames(text: str) -> list[tuple[str, str]]:
    """ReflectUnits.h 글 → `kArrReflectUnit` 의 (철자, 차원) 중 식별자인 것."""
    start = text.index("kArrReflectUnit[]")
    end = text.index("};", start)
    listUnit = re.findall(r'\{\s*"([^"]+)"\s*,\s*ReflectUnitDimension::(\w+)', text[start:end])
    if not listUnit:
        raise ValueError(f"{kUnitsRelativePath}: kArrReflectUnit 에서 단위를 찾지 못했습니다")
    return [(name, dimension) for name, dimension in listUnit if re.fullmatch(r"[A-Za-z_]\w*", name)]


def readContainerKinds(text: str) -> list[str]:
    """PredefinedContainerKind.xxx 글 → `REGISTER_CONTAINER_KIND( X )` 의 X 중 None 이 아닌 것."""
    return [kind for kind in re.findall(r"^\s*REGISTER_CONTAINER_KIND\(\s*(\w+)\s*\)", text, re.MULTILINE) if kind != "None"]


def describeKeyInternal(key: AnnotationKey, listCanonicalSpelling: list[str]) -> str:
    usage = _kMapKindUsage.get(key.kind, "`{0}`").format(key.name)
    listLine = [f"{key.canonical} — {usage}."]
    if len(listCanonicalSpelling) > 1:
        listLine.append(f"같은 필드의 철자: {', '.join(listCanonicalSpelling)}.")
    listLine.extend(key.listComment)
    return " ".join(listLine)


def escapeCommentInternal(text: str) -> str:
    return text.replace("*/", "* /")


def buildKeyStructInternal(structName: str, title: str, listKey: list[AnnotationKey], listExtra: list[tuple[str, str]]) -> list[str]:
    listLine = [f"    /** @brief {title} */", f"    struct {structName}", "    {"]
    for key in listKey:
        if key.name in _kSetCppKeyword:
            continue
        listSpelling = [other.name for other in listKey if other.canonical == key.canonical]
        listLine.append(f"        /** @brief {escapeCommentInternal(describeKeyInternal(key, listSpelling))} */")
        listLine.append(f"        static constexpr AnnotationKey {key.name}{{}};")
    for name, description in listExtra:
        listLine.append(f"        /** @brief {escapeCommentInternal(description)} */")
        listLine.append(f"        static constexpr AnnotationKey {name}{{}};")
    listLine.append("    };")
    return listLine


def buildKeyArrayInternal(arrayName: str, listName: list[str]) -> list[str]:
    listLine = [f"    inline constexpr const utf8* {arrayName}[] = {{"]
    listLine.extend(f'        "{name}",' for name in listName)
    listLine.append("        nullptr,")
    listLine.append("    };")
    return listLine


def buildAnnotationKeysHeader(root: Path) -> str:
    """저장소 루트 → 헤더 글(줄끝 `\\n`)."""
    mapSection = readAnnotationSections((root / kAnnotationMetaRelativePath).read_text(encoding="utf-8"))
    listUnit = readUnitNames((root / kUnitsRelativePath).read_text(encoding="utf-8"))
    listContainerKind = readContainerKinds((root / kContainerKindRelativePath).read_text(encoding="utf-8"))

    listLine = [
        "/**",
        " * @file ReflectionAnnotationKeys.h",
        " * @brief 리플렉션 어노테이션 인자 키를 편집기에 알리는 선언입니다. **생성 파일 — 고치지 마십시오.**",
        " * @details `Scripts/generate/GenerateReflectionAnnotationKeys.py` 가 `AnnotationMeta.txt`(키와 철자), `ReflectUnits.h`(단위),",
        " *          `PredefinedContainerKind.xxx`(컨테이너 종류)에서 만듭니다. 키를 더하려면 그 파일들을 고치고 스크립트를 다시 실행합니다.",
        " *          편집기에서만 include 됩니다(`ReflectionIntellisense.h`). 빌드와 ReflectionParser 는 이 파일을 읽지 않습니다.",
        " */",
        "#pragma once",
        "#include \"Core/Common/Types.h\"",
        "",
        "namespace sw::reflect::attr",
        "{",
        "    /** @brief 키 하나. `Key = 값` 의 값은 무엇이든 받습니다(값 형식은 ReflectionParser 가 검사합니다). */",
        "    struct AnnotationKey",
        "    {",
        "        /** @brief `Key = 값` 을 식으로 받습니다. */",
        "        template <typename T>",
        "        constexpr const AnnotationKey& operator=( T&& ) const noexcept",
        "        {",
        "            return *this;",
        "        }",
        "    };",
        "} // namespace sw::reflect::attr",
    ]
    # 정의 하나에 namespace 블록 하나(CheckNamespaceBlocks)
    for section, structName, arrayName in kListSection:
        listKey = mapSection.get(section)
        if not listKey:
            raise ValueError(f"{kAnnotationMetaRelativePath}: [{section}] 절이 없습니다")
        listExtra: list[tuple[str, str]] = []
        if section == "PROPERTY":
            listExtra = [(name, f"단위 — `Units = {name}` ({dimension}). 단위 표는 ReflectUnits.h 의 kArrReflectUnit 입니다.")
                         for name, dimension in listUnit]
        listLine.extend(["", "namespace sw::reflect::attr", "{"])
        listLine.extend(buildKeyStructInternal(structName, f"`{section}( … )` 의 키입니다.", listKey, listExtra))
        listLine.append("")
        listLine.extend(buildKeyArrayInternal(arrayName, [key.name for key in listKey]))
        listLine.append("} // namespace sw::reflect::attr")
    listLine.extend(["", "namespace sw::reflect::attr", "{"])
    listLine.extend(buildKeyStructInternal(
        "ContainerKeys", "`REFLECT_CONTAINER( 종류[, 래퍼] )` 의 종류입니다. 둘째 인자는 래퍼 이름(`List` → ListWrapper)입니다.", [],
        [(kind, f"컨테이너 종류 `{kind}`") for kind in listContainerKind]))
    listLine.append("} // namespace sw::reflect::attr")
    listLine.append("")
    return "\n".join(listLine)


def buildAnnotationKeysFiles(root: Path) -> dict[str, bytes]:
    """저장소 기준 경로 → 파일 바이트."""
    return {kHeaderRelativePath: buildAnnotationKeysHeader(root).encode("utf-8")}
