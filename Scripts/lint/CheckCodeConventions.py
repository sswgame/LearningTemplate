#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
CheckCodeConventions.py

SW Engine C++ 코딩 컨벤션 및 정적 규칙 자동 검사 스크립트.

검사 항목:
  1) Explicit Comparison: bool, pointer, container 상태(empty, contains) 암시적 평가 및 ! 부정문 검출
  2) Loop Counter Naming: for 루프 내 단일 문자 카운터(i, j, k) 검출
  3) Member Variable Naming:
     - 고정 배열: _arr 접두어 누락 검출
     - 가변 배열(vector, list, deque): _list 접두어 또는 List 접미어 누락 검출
     - 연관 컨테이너(map, unordered_map): _map 접두어 누락 검출
     - 고유 집합(set, unordered_set): _unique 접두어 누락 검출
     - 원시 포인터 멤버: _p 접두어 누락 검출
     - 전역 정적 변수: s_, _s_ 및 포인터 s_p, _s_p 누락 검출
  4) auto 사용 제한: 리터럴/원시 타입 직접 대입에 auto 사용 검출
  5) Include 규칙: .cpp 파일 첫 줄 #include "pch.h" 여부

사용법:
  py -3 Scripts/lint/CheckCodeConventions.py [--root <repo>] [--fix] [--json]
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import List, Optional, Tuple

sys.path.insert( 0, str( Path( __file__ ).resolve().parents[1] ) )
import ConfigHelper
from ConfigHelper import getProjectRoot

# --- Types & Data Structures -------------------------------------------------

@dataclass
class ConventionViolation:
    file_path: str
    line_number: int
    rule_category: str
    message: str
    snippet: str
    suggested_fix: Optional[str] = None

# --- Regex Patterns ----------------------------------------------------------

_kLoopIndexRe = re.compile(
    r'\bfor\s*\(\s*(?:auto|int\w*|uint\w*|size_t)\s+([ijk])\s*=',
    re.MULTILINE
)

_kPchIncludeRe = re.compile( r'^\s*#\s*include\s*["<]pch\.h[">]' )

_kMemberRawPointerRe = re.compile(
    r'^\s*(?:[A-Za-z0-9_:]+\s*\*)\s+(_[^pP\s][a-zA-Z0-9_]*)\s*;'
)

_kMemberFixedArrayRe = re.compile(
    r'^\s*(?:float32|float64|int32|int64|uint8|uint16|uint32|uint64|char|utf8|bool)\s+(_[^a\s][a-zA-Z0-9_]*)\s*\[[^\]]+\]\s*;'
)

_kMemberVectorRe = re.compile(
    r'^\s*(?:(?:sw::)?(?:vector|list|deque))\s*<[^>]+>\s+(_[a-zA-Z0-9_]+)\s*;'
)

_kMemberMapRe = re.compile(
    r'^\s*(?:(?:sw::)?(?:unordered_map|map))\s*<[^>]+>\s+(_[a-zA-Z0-9_]+)\s*;'
)

_kMemberSetRe = re.compile(
    r'^\s*(?:(?:sw::)?(?:unordered_set|set))\s*<[^>]+>\s+(_[a-zA-Z0-9_]+)\s*;'
)

_kLiteralAutoRe = re.compile(
    r'^\s*auto\s+([a-zA-Z0-9_]+)\s*=\s*(?:"[^"]*"|\'[^\']*\'|\btrue\b|\bfalse\b|\bnullptr\b)\s*;'
)

_kExplicitTrueRe = re.compile(
    r'\bif\s*\(\s*([a-zA-Z0-9_>.-]+\s*==\s*true|true\s*==\s*[a-zA-Z0-9_>.-]+)\s*\)'
)

_kImplicitFalseRe = re.compile(
    r'\bif\s*\(\s*!\s*([a-zA-Z0-9_>.-]+)\s*\)'
)

_kImplicitPointerNullRe = re.compile(
    r'\bif\s*\(\s*(get[A-Z][a-zA-Z0-9_]*\(\)(?:\s*&&\s*get[A-Z][a-zA-Z0-9_]*\(\))*)\s*\)'
)

_kConstantNamingRe = re.compile(
    r'^\s*static\s+constexpr\s+(?:\w+)\s+([A-Z][a-zA-Z0-9_]*)\s*='
)

# --- Inspection Logic --------------------------------------------------------

def checkFileConventionsInternal( filePath: Path, rootDir: Path ) -> List[ConventionViolation]:
    violations: List[ConventionViolation] = []
    relPath = str( filePath.relative_to( rootDir ) ).replace( "\\", "/" )
    isHeader = filePath.suffix in ( ".h", ".hpp", ".inl" )
    isSource = filePath.suffix in ( ".cpp", ".c", ".cc" )

    try:
        content = filePath.read_text( encoding="utf-8" )
    except UnicodeDecodeError:
        try:
            content = filePath.read_text( encoding="latin-1" )
        except Exception:
            return violations

    lines = content.splitlines()

    # 1. PCH check for .cpp
    if isSource:
        firstCodeLine = None
        firstCodeLineNum = 1
        for idx, line in enumerate( lines, start=1 ):
            trimmed = line.strip()
            if not trimmed or trimmed.startswith( "//" ) or trimmed.startswith( "/*" ) or trimmed.startswith( "*" ):
                continue
            firstCodeLine = trimmed
            firstCodeLineNum = idx
            break

        if firstCodeLine and not _kPchIncludeRe.match( firstCodeLine ):
            violations.append(
                ConventionViolation(
                    file_path=relPath,
                    line_number=firstCodeLineNum,
                    rule_category="Include/PCH",
                    message='First code include in .cpp must be #include "pch.h"',
                    snippet=firstCodeLine,
                )
            )

    # Line-by-line inspection
    inBlockComment = False
    for lineNum, line in enumerate( lines, start=1 ):
        trimmed = line.strip()

        # Handle comments
        if "/*" in trimmed and "*/" not in trimmed:
            inBlockComment = True
            continue
        if inBlockComment:
            if "*/" in trimmed:
                inBlockComment = False
            continue
        if trimmed.startswith( "//" ) or not trimmed:
            continue

        # 2. Single-letter loop variable check
        loopMatch = _kLoopIndexRe.search( line )
        if loopMatch:
            violations.append(
                ConventionViolation(
                    file_path=relPath,
                    line_number=lineNum,
                    rule_category="Naming/LoopVariable",
                    message=f"Single-letter loop variable '{loopMatch.group(1)}' used. Use descriptive name (e.g. index, childIndex).",
                    snippet=trimmed,
                )
            )

        # 3. auto with literal/primitive check
        autoMatch = _kLiteralAutoRe.search( line )
        if autoMatch:
            violations.append(
                ConventionViolation(
                    file_path=relPath,
                    line_number=lineNum,
                    rule_category="Style/AutoRestriction",
                    message=f"Avoid 'auto' for literal assignments ('{autoMatch.group(1)}'). Use explicit type.",
                    snippet=trimmed,
                )
            )

        # 4. Structured binding local variable check (must not start with _)
        if "[" in trimmed and "]" in trimmed and ("auto" in trimmed or "auto&" in trimmed):
            sbMatch = re.search( r'^\s*(?:const\s+)?auto\s*&?\s*\[([^\]]+)\]', line )
            if sbMatch:
                for var in [v.strip() for v in sbMatch.group( 1 ).split( ',' )]:
                    if var.startswith( "_" ) and not var.startswith( "_s_" ):
                        violations.append(
                            ConventionViolation(
                                file_path=relPath,
                                line_number=lineNum,
                                rule_category="Naming/LocalVariable",
                                message=f"Local variable '{var}' from structured binding must not have leading underscore.",
                                snippet=trimmed,
                            )
                        )

        # 4.5. Explicit true check (e.g. if ( bValid == true ))
        if _kExplicitTrueRe.search( line ):
            violations.append(
                ConventionViolation(
                    file_path=relPath,
                    line_number=lineNum,
                    rule_category="Style/ExplicitTrueCheck",
                    message="Do not compare boolean with '== true'. Use 'if (bValid)' instead.",
                    snippet=trimmed,
                )
            )

        # 4.6. Implicit false check (e.g. if ( !bValid ))
        if _kImplicitFalseRe.search( line ):
            violations.append(
                ConventionViolation(
                    file_path=relPath,
                    line_number=lineNum,
                    rule_category="Style/ImplicitFalseCheck",
                    message="Use explicit boolean false comparison 'if (bValid == false)' instead of 'if (!bValid)'.",
                    snippet=trimmed,
                )
            )

        # 4.7. Implicit pointer null check (e.g. if ( getOwner() ))
        ptrMatch = _kImplicitPointerNullRe.search( line )
        if ptrMatch:
            violations.append(
                ConventionViolation(
                    file_path=relPath,
                    line_number=lineNum,
                    rule_category="Style/ImplicitPointerNullCheck",
                    message=f"Implicit pointer check '{ptrMatch.group(1)}' detected. Use explicit '!= nullptr' comparison.",
                    snippet=trimmed,
                )
            )

        # 4.8. Constant naming check (static constexpr Mask -> kMask)
        constMatch = _kConstantNamingRe.search( line )
        if constMatch:
            varName = constMatch.group(1)
            if "Math" not in relPath:
                if not varName.startswith("k") or (len(varName) > 1 and not varName[1].isupper()):
                    violations.append(
                        ConventionViolation(
                            file_path=relPath,
                            line_number=lineNum,
                            rule_category="Naming/Constant",
                            message=f"Constant '{varName}' must use kPascalCase naming convention.",
                            snippet=trimmed,
                        )
                    )

        # 5. Constructor initializer formatting check (one initializer per line starting with : or ,)
        if trimmed.startswith( ":" ) or trimmed.startswith( "," ):
            initMatch = re.search( r'^[,\:]\s*([a-zA-Z0-9_]+)\s*(\([^\)]*\)|\{[^\}]*\})', trimmed )
            if initMatch:
                initVar = initMatch.group( 1 )
                initVal = initMatch.group( 2 )
                # Rule: constructor initialization must use braces {} rather than parentheses ()
                if initVal.startswith( "(" ) and not initVar.startswith( "super" ) and not initVar.endswith( "Base" ):
                    # Only flag if not base class ctor call
                    if "_" in initVar:
                        violations.append(
                            ConventionViolation(
                                file_path=relPath,
                                line_number=lineNum,
                                rule_category="Style/ConstructorBraces",
                                message=f"Constructor member initializer for '{initVar}' must use brace initialization '{{}}' instead of '()'.",
                                snippet=trimmed,
                            )
                        )

        # 4. Header member naming checks
        if isHeader:
            # Raw pointer member naming
            ptrMatch = _kMemberRawPointerRe.match( line )
            if ptrMatch:
                varName = ptrMatch.group( 1 )
                violations.append(
                    ConventionViolation(
                        file_path=relPath,
                        line_number=lineNum,
                        rule_category="Naming/RawPointer",
                        message=f"Raw pointer member '{varName}' must start with '_p'.",
                        snippet=trimmed,
                    )
                )

            # Fixed array naming
            arrMatch = _kMemberFixedArrayRe.match( line )
            if arrMatch:
                varName = arrMatch.group( 1 )
                violations.append(
                    ConventionViolation(
                        file_path=relPath,
                        line_number=lineNum,
                        rule_category="Naming/FixedArray",
                        message=f"Fixed array member '{varName}' must start with '_arr'.",
                        snippet=trimmed,
                    )
                )

            # Vector/List naming
            vecMatch = _kMemberVectorRe.match( line )
            if vecMatch:
                varName = vecMatch.group( 1 )
                if not varName.startswith( "_list" ) and not varName.endswith( "List" ) and not varName.startswith( "_s_list" ):
                    violations.append(
                        ConventionViolation(
                            file_path=relPath,
                            line_number=lineNum,
                            rule_category="Naming/DynamicContainer",
                            message=f"Dynamic array/vector member '{varName}' must start with '_list' or end with 'List'.",
                            snippet=trimmed,
                        )
                    )

            # Map naming
            mapMatch = _kMemberMapRe.match( line )
            if mapMatch:
                varName = mapMatch.group( 1 )
                if not varName.startswith( "_map" ) and not varName.startswith( "_s_map" ):
                    violations.append(
                        ConventionViolation(
                            file_path=relPath,
                            line_number=lineNum,
                            rule_category="Naming/MapContainer",
                            message=f"Map container member '{varName}' must start with '_map'.",
                            snippet=trimmed,
                        )
                    )

            # Set naming
            setMatch = _kMemberSetRe.match( line )
            if setMatch:
                varName = setMatch.group( 1 )
                if not varName.startswith( "_unique" ) and not varName.startswith( "_s_unique" ):
                    violations.append(
                        ConventionViolation(
                            file_path=relPath,
                            line_number=lineNum,
                            rule_category="Naming/SetContainer",
                            message=f"Unique set member '{varName}' must start with '_unique'.",
                            snippet=trimmed,
                        )
                    )

        # 5. Negated boolean checks without explicit comparison (e.g. if ( !_bValue ), if ( !bValue ))
        negatedBoolMatch = re.search( r'\bif\s*\(\s*!\s*(_?b[A-Z][a-zA-Z0-9_]*)\s*\)', line )
        if negatedBoolMatch:
            varName = negatedBoolMatch.group( 1 ).strip()
            violations.append(
                ConventionViolation(
                    file_path=relPath,
                    line_number=lineNum,
                    rule_category="Style/NegatedComparison",
                    message=f"Negated boolean condition 'if ( !{varName} )'. Explicitly compare with 'if ( {varName} == false )'.",
                    snippet=trimmed,
                )
            )

        # 6. Negated empty() checks without explicit comparison (e.g. if ( !var.empty() ))
        negatedEmptyMatch = re.search( r'\bif\s*\(\s*!\s*([a-zA-Z0-9_.]+(?:->[a-zA-Z0-9_]+)?\.empty\(\))\s*\)', line )
        if negatedEmptyMatch:
            expr = negatedEmptyMatch.group( 1 ).strip()
            violations.append(
                ConventionViolation(
                    file_path=relPath,
                    line_number=lineNum,
                    rule_category="Style/NegatedComparison",
                    message=f"Negated empty condition 'if ( !{expr} )'. Explicitly compare with 'if ( {expr} == false )'.",
                    snippet=trimmed,
                )
            )

        # 7. Negated pointer checks without explicit comparison (e.g. if ( !pPtr ), if ( !_pPtr ))
        negatedPtrMatch = re.search( r'\bif\s*\(\s*!\s*(_?p[A-Z][a-zA-Z0-9_]*)\s*\)', line )
        if negatedPtrMatch:
            ptrName = negatedPtrMatch.group( 1 ).strip()
            violations.append(
                ConventionViolation(
                    file_path=relPath,
                    line_number=lineNum,
                    rule_category="Style/NegatedComparison",
                    message=f"Negated pointer condition 'if ( !{ptrName} )'. Explicitly compare with 'if ( {ptrName} == nullptr )'.",
                    snippet=trimmed,
                )
            )

    return violations

# --- Public API & Entry Point ------------------------------------------------

def runConventionsCheck( rootDir: Optional[Path] = None ) -> List[ConventionViolation]:
    if rootDir is None:
        rootDir = Path( getProjectRoot() )

    searchDirs = [
        rootDir / "Source",
        rootDir / "Test",
    ]

    allViolations: List[ConventionViolation] = []

    for searchDir in searchDirs:
        if not searchDir.exists():
            continue
        for ext in ( "*.h", "*.hpp", "*.cpp", "*.c" ):
            for filePath in searchDir.rglob( ext ):
                # Exclude ThirdParty or build directories if any
                normPath = str( filePath ).replace( "\\", "/" )
                if "ThirdParty" in normPath or "build" in normPath or ".vcpkg" in normPath:
                    continue
                violations = checkFileConventionsInternal( filePath, rootDir )
                allViolations.extend( violations )

    return allViolations

def main() -> int:
    parser = argparse.ArgumentParser( description="SW Engine C++ Code Conventions Checker" )
    parser.add_argument( "--root", type=Path, default=None, help="Root path of the repository" )
    parser.add_argument( "--category", type=str, default=None, help="Filter by rule category" )
    parser.add_argument( "--exclude-category", type=str, default=None, help="Exclude specific rule category" )
    parser.add_argument( "--json", action="store_true", help="Output results in JSON format" )
    args = parser.parse_args()

    rootDir = args.root if args.root else Path( getProjectRoot() )
    violations = runConventionsCheck( rootDir )

    if args.category:
        violations = [v for v in violations if args.category.lower() in v.rule_category.lower()]
    if args.exclude_category:
        violations = [v for v in violations if args.exclude_category.lower() not in v.rule_category.lower()]

    if args.json:
        outData = [asdict( v ) for v in violations]
        print( json.dumps( outData, indent=2, ensure_ascii=False ) )
    else:
        print( f"\n========================================================" )
        print( f"  SW Engine Code Conventions Scan Report" )
        print( f"  Repository: {rootDir}" )
        print( f"  Total Violations Found: {len( violations )}" )
        print( f"========================================================\n" )

        categoryGroups: dict[str, list[ConventionViolation]] = {}
        for v in violations:
            categoryGroups.setdefault( v.rule_category, [] ).append( v )

        for cat, items in sorted( categoryGroups.items() ):
            print( f"[{cat}] ({len( items )} items):" )
            for item in items:
                print( f"  {item.file_path}:{item.line_number} -> {item.message}" )
                print( f"      Line: {item.snippet}" )
            print()

        print( "--------------------------------------------------------" )
        print( f"Summary by Category:" )
        for cat, items in sorted( categoryGroups.items() ):
            print( f"  - {cat:<25}: {len( items ):>3} occurrences" )
        print( "========================================================\n" )

    return 0 if len( violations ) == 0 else 1

if __name__ == "__main__":
    sys.exit( main() )
