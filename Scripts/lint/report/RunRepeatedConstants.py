#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
같은 뜻이 여러 곳에 따로 적힌 **상수와 리터럴**을 뽑는다(보고만 한다).

[왜 필요한가 — 값 둘은 언젠가 갈라진다]
같은 값을 두 곳에 적으면 한쪽만 바뀐다. 컴파일도 시험도 통과하고, 어느 한 경로만 조용히 다르게 동작한다. 이 트리에서 실제로:
  - 루트 상수 dword 를 DX12 · Vulkan 은 16(셰이더 계약), DX11 · GL 은 64 로 따로 적어 오프셋 16 이상의 쓰기가 백엔드마다 달랐다.
  - 모프 · 스킨 버퍼 배치(`SW_MORPH_FLOAT4_PER_VERTEX`)가 셰이더 세 파일과 C++ 한 파일에 "같아야 한다" 는 주석만 달고 따로 있었다.
  - 4 글자 형식 표식이 파일마다 바이트 순서가 달랐다(`'SWHF'` 는 파일 순서, `'SWST'` 는 반대).

[네 가지를 본다]
  1. 이름도 값도 같은 상수가 **다른 모듈**에 따로 정의된 것(`kTwoPi` 다섯) — 한 곳으로 올릴 후보.
  2. 이름 꼬리 · 값이 같은 형제 상수(`kFallbackFovY` ↔ `kDefaultFovY`) — 같은 이유로 같으면 공유, 우연이면 그대로 둔다(사람이 판단).
  3. 이름 붙은 문자열 상수가 있는데 다른 곳에서 같은 리터럴을 다시 적은 것(`".meta"` ↔ `path::kMetaExtension`).
  4. 코드 안 숫자 · 문자열 · `hashed_string( "..." )` 리터럴이 여러 파일에 반복되는 것.

[올라오지만 고칠 것이 아닌 것]
  - **관례 이름**: `kXmlRootName` · `kExtension` · `kVersion` · `kMagic` · `kStateTag` · `kStateVersion` 은 타입마다 자기 값을 갖는 같은 모양이다(`kConventionNames`).
  - **0 · 1 · -1 · 2 같은 자명한 수**, 시험의 기대값(`Test/` 는 보지 않는다), 표 초기화 행(`{ ... },` 줄), 로그 · 단언 문자열.
  - 직렬화 키(`"name"` · `"kind"`)는 형식마다 자기 스키마다 — 같은 형식의 읽기 · 쓰기가 다른 파일에 있을 때만 상수로 묶는다.

[게이트가 아니다]
`Run*` 은 보고하고 `Check*` 이 막는다. 잘 알려진 상수(π · 해시 상수 · 중력)만 `CheckWellKnownConstants` 가 막는다 — 나머지는 "같은 값이
같은 이유로 같은가" 를 사람이 봐야 해서 막으면 거짓 양성이 쌓인다.

사용법:
  py -3 Scripts/lint/report/RunRepeatedConstants.py                 # 네 절 모두, 절마다 상위 30
  py -3 Scripts/lint/report/RunRepeatedConstants.py --section 2     # 형제 상수만
  py -3 Scripts/lint/report/RunRepeatedConstants.py --top 100 --filter Engine/Graphics
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path
from typing import NamedTuple

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common

from common import blankComments, normalizePath, readTextFiles  # noqa: E402

kListScanRoot = ("Source", "Tools/ReflectionParser")
kSuffixes = (".h", ".hpp", ".inl", ".cpp", ".xxx")

#: 타입마다 자기 값을 갖는 관례 이름 — 이름이 같아도 중복이 아니다.
kConventionNames = frozenset({"kXmlRootName", "kExtension", "kVersion", "kMagic", "kStateTag", "kStateVersion", "kName", "kType",
                              "kTypeSize", "kKindName", "kRootName", "kArrRootAttribute", "kDefaultPath", "kResourcePath", "kCount"})
#: 자명한 수 — 반복돼도 보고하지 않는다.
kTrivialNumbers = frozenset({"0", "1", "-1", "2", "3", "4", "8", "10", "16", "32", "64", "100", "255", "0.0", "1.0", "0.5", "2.0", "-1.0"})
#: 리터럴을 보지 않는 줄 — 로그 · 단언 · 메타데이터 · 상수 선언 자체.
kSkipLineRe = re.compile(r"SW_LOG_|SW_ASSERT|SW_VERIFY|SW_ENSURE|static_assert|\b(?:PROPERTY|FUNCTION|REFLECT|ENUM)\s*\(|^\s*#|\bconstexpr\b|ImGui::(?:Text|SetTooltip|TextUnformatted|TextDisabled|BulletText|SeparatorText)")
kDeclRe = re.compile(r"^\s*(?:(?:inline|static)\s+)*(?:constexpr|const)\s+[\w:<>,\s\*]+?\s+(?P<name>(?:_s_|_)?k[A-Z0-9]\w*)\s*(?:\[[^\]]*\])?\s*(?:=|\{)\s*(?P<value>[^;]*);")
kStringRe = re.compile(r'(?<![A-Za-z0-9_])(?:u8)?"((?:[^"\\\n]|\\.){3,40})"')
kHashedRe = re.compile(r'hashed_string\s*\(\s*"((?:[^"\\\n]|\\.)+)"\s*\)')
kNumberRe = re.compile(r"(?<![\w.])(-?(?:0[xX][0-9A-Fa-f']+|\d[\d']*\.\d*(?:[eE][-+]?\d+)?|\d[\d']*(?:[eE][-+]?\d+)?))[uUlLfF]*(?![\w.])")
kTableRowRe = re.compile(r"^\s*\{.*\}\s*,?\s*$")


class Decl(NamedTuple):
    path: str
    line: int
    name: str
    value: str


def moduleOf(relative: str) -> str:
    parts = relative.split("/")
    if parts[0] != "Source":
        return parts[0]
    if len(parts) > 4 and parts[1] == "GameFramework" and parts[2] == "Kits":
        return "/".join(parts[1:5])
    return "/".join(parts[1:3]) if len(parts) > 3 else parts[1]


def normalizeValue(value: str) -> str:
    value = re.sub(r"\s+", "", value).rstrip(",")
    value = re.sub(r"(?<=\d)[uUlLfF]+$", "", value)
    return re.sub(r"(?<=\d)\.0*(?=\D|$)", "", value)


def nameTail(name: str) -> str:
    words = re.findall(r"[A-Z][a-z0-9]*|[0-9]+", name.replace("_s_k", "k").lstrip("_")[1:])
    return "".join(words[-2:]) if len(words) >= 2 else "".join(words)


def collect(repositoryRoot: Path, filterText: str):
    listPath = []
    for scanRoot in kListScanRoot:
        listPath.extend(p for p in (repositoryRoot / scanRoot).rglob("*") if p.suffix in kSuffixes and p.is_file())
    listPath = [p for p in listPath if filterText in normalizePath(str(p.relative_to(repositoryRoot)))]
    listDecl: list[Decl] = []
    mapNumber: dict[str, set[str]] = defaultdict(set)
    mapString: dict[str, list[str]] = defaultdict(list)
    mapHashed: dict[str, set[str]] = defaultdict(set)
    for path, text in readTextFiles(listPath):
        relative = normalizePath(str(path.relative_to(repositoryRoot)))
        for lineIndex, line in enumerate(blankComments(text).splitlines(), start=1):
            match = kDeclRe.match(line)
            if match:
                listDecl.append(Decl(relative, lineIndex, match.group("name"), match.group("value").strip()))
                continue
            if kSkipLineRe.search(line):
                continue
            for hashed in kHashedRe.finditer(line):
                mapHashed[hashed.group(1)].add(relative)
            for literal in kStringRe.finditer(line):
                mapString[literal.group(1)].append(f"{relative}:{lineIndex}")
            if kTableRowRe.match(line):
                continue
            for number in kNumberRe.finditer(kStringRe.sub('""', line)):
                key = normalizeValue(number.group(1).replace("'", "").lower())
                if key not in kTrivialNumbers:
                    mapNumber[key].add(relative)
    return listDecl, mapNumber, mapString, mapHashed


def printSection(title: str, rows: list[str], top: int) -> None:
    print(f"\n== {title}: {len(rows)} 건 (위에서 {min(top, len(rows))})")
    for row in rows[:top]:
        print("  " + row)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Report constants and literals defined in more than one place.")
    parser.add_argument("--root", default=None, help="저장소 루트 (기본: 이 스크립트 기준)")
    parser.add_argument("--filter", default="", help="경로에 이 문자열이 든 파일만")
    parser.add_argument("--top", type=int, default=30, help="절마다 보고할 최대 건수")
    parser.add_argument("--section", type=int, choices=(1, 2, 3, 4), default=None, help="이 절만")
    parser.add_argument("--min-files", type=int, default=6, help="4 절: 숫자 리터럴이 이 수 이상의 파일에 있을 때만")
    args = parser.parse_args(argv)
    repositoryRoot = Path(args.root).resolve() if args.root else Path(__file__).resolve().parents[3]

    listDecl, mapNumber, mapString, mapHashed = collect(repositoryRoot, args.filter)
    mapByName: dict[str, list[Decl]] = defaultdict(list)
    for decl in listDecl:
        mapByName[decl.name].append(decl)

    if args.section in (None, 1):
        rows = []
        for name, group in sorted(mapByName.items(), key=lambda item: -len(item[1])):
            if name in kConventionNames:
                continue
            setModule = {moduleOf(d.path) for d in group}
            setValue = {normalizeValue(d.value) for d in group}
            if len(setModule) >= 2 and len(setValue) == 1:
                rows.append(f"{name} = {group[0].value[:30]}  ({len(group)} 곳 · {len(setModule)} 모듈)  " + " | ".join(f"{d.path}:{d.line}" for d in group[:4]))
        printSection("1) 이름 · 값이 같은 상수가 여러 모듈에", rows, args.top)

    if args.section in (None, 2):
        mapSibling: dict[tuple[str, str], list[Decl]] = defaultdict(list)
        for decl in listDecl:
            value = normalizeValue(decl.value)
            if value in kTrivialNumbers or value in ("true", "false", "{}", "") or value.startswith("{") or '"' in value:
                continue
            mapSibling[(nameTail(decl.name), value)].append(decl)
        rows = []
        for (tail, value), group in sorted(mapSibling.items(), key=lambda item: -len(item[1])):
            if len({d.path for d in group}) >= 2 and len({d.name for d in group}) >= 1 and not all(d.name in kConventionNames for d in group):
                rows.append(f"*{tail} = {value[:28]}  " + " | ".join(f"{d.name}@{d.path}:{d.line}" for d in group[:4]))
        printSection("2) 형제 상수(이름 꼬리 · 값이 같다)", rows, args.top)

    if args.section in (None, 3):
        mapStringConst: dict[str, list[Decl]] = defaultdict(list)
        for decl in listDecl:
            match = re.fullmatch(r'(?:u8)?"([^"\\]{3,})"', decl.value)
            if match:
                mapStringConst[match.group(1)].append(decl)
        rows = []
        for value, group in mapStringConst.items():
            listSite = [site for site in mapString.get(value, []) if site.split(":")[0] not in {d.path for d in group}]
            if listSite and (value.startswith((".", "/")) or "/" in value or re.search(r"[A-Z].*[a-z]", value) or len(value) > 8):
                rows.append((len(listSite), f'"{value}" 이 {len(listSite)} 곳 — 상수 {group[0].name}@{group[0].path}:{group[0].line}  예: ' + ", ".join(listSite[:3])))
        printSection("3) 이름 붙은 문자열 상수를 리터럴로 다시 적음", [row for _, row in sorted(rows, key=lambda r: -r[0])], args.top)

    if args.section in (None, 4):
        rows = [f"{len(files):4} 파일  {value}" for value, files in sorted(mapNumber.items(), key=lambda item: -len(item[1])) if len(files) >= args.min_files]
        printSection(f"4a) 숫자 리터럴 반복({args.min_files} 파일 이상)", rows, args.top)
        rows = [f"{len(files):4} 파일  hashed_string( \"{value}\" )  " + ", ".join(sorted(files)[:3]) for value, files in sorted(mapHashed.items(), key=lambda item: -len(item[1])) if len(files) >= 2]
        printSection("4b) hashed_string 리터럴 반복(2 파일 이상)", rows, args.top)

    print("\n  고르기 전에 **'같은 이유로 같은 값인가'** 를 먼저 물어보세요. 계약이면 한 곳으로, 우연이면 그대로 둡니다(AGENTS.md 상수 절).")
    return 0  # 보고만 한다.


if __name__ == "__main__":
    sys.exit(main())
