#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
이 트리에서 **복사돼 있는 코드 블록**을 길이순으로 뽑는다.

[왜 필요한가 — 복붙은 증상이고, 진짜 문제는 "한쪽만 고쳐진다" 는 것이다]
같은 블록이 두 곳에 있으면 언젠가 한쪽만 고쳐진다. 그리고 그 어긋남은 **조용하다** — 빌드도 테스트도
통과하고, 어느 한 경로로 들어온 데이터만 다르게 처리된다. 이 저장소가 실제로 겪은 것들이다:

  - `BindingKind` 를 하나 더하려면 여섯 곳을 고쳐야 했고, 그중 저장 경로를 빠뜨리면 **바인딩이 파일에서
    사라졌다**(2026-09-19).
  - 그래프 패널 둘이 "노드가 움직였나" 를 각자 적었는데 한쪽은 `||`, 다른 쪽은 `&&` 였다 —
    `&&` 쪽에서는 수평으로만 옮긴 노드의 레이아웃이 **저장되지 않았다**(2026-09-19).
  - DX11 드로우 진입점 넷 중 하나가 파이프라인 바인딩 블록을 빠뜨려 **화면이 클리어 색만 남았다**.
    고친 뒤에도 **네 번째 진입점에는 여전히 없었다**(2026-09-19).
  - 세 직렬화 포맷이 같은 스무 줄을 갖고 있었고 그중 한 줄만 달라서, 같은 데이터가 포맷마다 다른
    답을 냈다(2026-09-19).

넷 다 "사람이 주의하면 된다" 로는 막히지 않았다. 그래서 **정기적으로 묻는 도구**를 둔다.

[세는 방법이 중요하다 — 두 번 틀렸다]
1. **창을 병합하지 않으면 숫자가 거짓말을 한다.** 연속된 20줄 중복 하나는 8줄 창으로 보면 13건으로
   세어진다. 처음 이 조사를 했을 때 "헤더 207건" 이 나왔는데 실제로는 148건이었고, 무엇보다 **무엇이
   큰 건지** 보이지 않았다. 지금은 (파일쌍 · 오프셋차) 가 같은 창을 이어 붙여 한 건으로 센다.
2. **길이순으로 보지 않으면 고를 수 없다.** 짧은 것 수백 건 사이에 31줄짜리가 묻힌다.

[올라오지만 고칠 것이 아닌 것들 — 매번 같은 것이 나온다]
  - **include 묶음**: 같은 헤더를 여러 파일이 include 하는 것은 중복이 아니다. 이 스크립트는 세지 않는다.
  - **백엔드가 같은 인터페이스를 구현하는 선언**: `*RHICommandContext.h` · `*RHIResource.h` 넷이
    같은 가상 함수를 선언한다. 헤더 상위권은 거의 이것이고, **합칠 대상이 아니다.**
  - **레이스 검출기 래퍼의 전달**: `vector.h` · `deque.h` · `list.h` 가 `SW_SCOPED_RACE_WRITE()` 뒤에
    `Base::` 를 부르는 기계적 전달. 매크로로 접으면 읽기만 나빠진다.
  - **플랫폼 구현**: 같은 함수의 Windows/POSIX 판은 이름과 뼈대가 닮지만 본체가 다르다. 다만 **가드나
    정책이 양쪽에 복사돼 있으면** 그것은 합칠 값이 있다(크래시 경로의 `try_lock` 이 그랬다).
  - **enum 레이블 나열**: 같은 열거형을 `switch` 하는 두 함수는 `case` 줄이 통째로 같아 보인다.
    `-Wswitch-default` 때문에 `default:` 도 양쪽에 있다. **본체가 다르면 중복이 아니다**
    (`MaterialPacking` 의 두 switch 가 매번 올라온다).
  - **서비스 로케이터 둘**: `sw::editor::getService` 와 `sw::game::getService` 는 열두 줄이 닮았지만
    **서로 다른 DLL 의 서로 다른 레지스트리**다(`SW_GAMESERVICE_API` 가 그 경계다). 합치려면 내부
    함수를 주입해야 하고 그러면 경계가 흐려진다. 미발견 처리도 의도적으로 다르다 — 에디터는
    문서대로 `nullptr` 을 돌려주고(그래서 `CheckNullableServiceUse` 가 있다), 게임 쪽은 Debug 에서
    `SW_ASSERT` 로 죽는다.
  - **같은 컨테이너 래퍼의 미세한 차이**: `VectorWrapper`↔`DequeWrapper` 는 `reserve` 유무만 다르고
    `unordered_map.h`↔`unordered_set.h` 는 레이스 래퍼 전달이다. 둘 다 접으면 읽기만 나빠진다.

즉 **"백엔드마다 정말로 다른 일을 하는가" 를 먼저 묻고** 시작한다. 답이 "그렇다" 면 넘긴다.

[게이트가 아니다]
`Run*` 은 보고하고 `Check*` 이 막는다. 중복이 몇 줄부터 나쁜지는 코드마다 다르고, 위에 적었듯
**정당한 중복이 상위권을 채운다** — 막으면 거짓 양성으로 아무도 린트를 돌리지 않게 된다.

사용법:
  py -3 Scripts/lint/report/RunDuplicateCode.py                      # Source/ 소스+헤더, 상위 20건
  py -3 Scripts/lint/report/RunDuplicateCode.py --min-lines 12       # 12줄 이상만
  py -3 Scripts/lint/report/RunDuplicateCode.py --filter Engine/Graphics
  py -3 Scripts/lint/report/RunDuplicateCode.py --same-file-only     # 한 파일 안의 복사만
  py -3 Scripts/lint/report/RunDuplicateCode.py --top 50
"""

from __future__ import annotations

import argparse
import collections
import hashlib
import re
import sys
from pathlib import Path
from typing import Iterable, NamedTuple

#: 해시할 창의 줄 수. 짧으면 잡음이, 길면 놓치는 것이 는다 — 6 이 이 저장소에서 쓸 만했다.
kWindowLines = 6
#: 창 하나의 최소 글자 수. 짧은 줄만으로 이루어진 창(닫는 괄호 연속 등)을 버린다.
kMinWindowChars = 180
#: 기본으로 보고할 최소 길이(병합 후 줄 수).
kDefaultMinLines = 10

#: 정규화에서 아예 버리는 줄 — 있으나 없으나 중복 여부를 바꾸지 않는다.
kIgnoredLines = frozenset( { "{", "}", "};", "break;", "return;", "public:", "private:", "protected:", "else" } )


class Finding( NamedTuple ):
    """중복 한 건 — 두 자리와 병합된 길이."""

    lineCount: int
    leftPath: str
    leftLine: int
    rightPath: str
    rightLine: int

    def isSameFile( self ) -> bool:
        return self.leftPath == self.rightPath


def normalizeFile( repositoryRoot: Path, path: Path ) -> tuple[ list[ str ], list[ int ] ]:
    """주석·공백·include 를 걷어낸 줄들과, 각 줄의 원래 줄 번호를 돌려줍니다."""
    try:
        rawLines = ( repositoryRoot / path ).read_text( encoding = "utf-8", errors = "replace" ).split( "\n" )
    except OSError:
        return [], []

    normalized: list[ str ] = []
    lineNumbers: list[ int ] = []
    for lineNumber, rawLine in enumerate( rawLines, 1 ):
        stripped = rawLine.strip()
        if not stripped:
            continue
        # include 묶음은 중복이 아니다(파일 머리말 참고).
        if stripped.startswith( ( "//", "*", "/*", "#include" ) ):
            continue
        if stripped in kIgnoredLines:
            continue
        normalized.append( re.sub( r"\s+", " ", stripped ) )
        lineNumbers.append( lineNumber )
    return normalized, lineNumbers


def collectFindings( repositoryRoot: Path, files: Iterable[ Path ], bSameFileOnly: bool ) -> list[ Finding ]:
    """창을 해시해 짝을 모으고, **연속된 창을 한 건으로 병합**합니다."""
    fileLines: dict[ str, tuple[ list[ str ], list[ int ] ] ] = {}
    windowOwners: dict[ str, list[ tuple[ str, int ] ] ] = collections.defaultdict( list )

    for path in files:
        normalized, lineNumbers = normalizeFile( repositoryRoot, path )
        if len( normalized ) < kWindowLines:
            continue
        key = path.as_posix()
        fileLines[ key ] = ( normalized, lineNumbers )
        for offset in range( len( normalized ) - kWindowLines + 1 ):
            chunk = "\n".join( normalized[ offset : offset + kWindowLines ] )
            if len( chunk ) < kMinWindowChars:
                continue
            windowOwners[ hashlib.sha1( chunk.encode( "utf-8" ) ).hexdigest() ].append( ( key, offset ) )

    # (왼쪽 파일, 오른쪽 파일, 오프셋 차) 가 같은 창들은 **하나의 연속 블록**이다.
    runsByPair: dict[ tuple[ str, str, int ], list[ int ] ] = collections.defaultdict( list )
    for owners in windowOwners.values():
        if len( owners ) < 2:
            continue
        for leftIndex in range( len( owners ) ):
            for rightIndex in range( leftIndex + 1, len( owners ) ):
                leftPath, leftOffset = owners[ leftIndex ]
                rightPath, rightOffset = owners[ rightIndex ]
                if leftPath == rightPath and leftOffset == rightOffset:
                    continue
                if bSameFileOnly and leftPath != rightPath:
                    continue
                runsByPair[ ( leftPath, rightPath, rightOffset - leftOffset ) ].append( leftOffset )

    findings: list[ Finding ] = []
    for ( leftPath, rightPath, offsetDelta ), offsets in runsByPair.items():
        sortedOffsets = sorted( set( offsets ) )
        runStart = previous = sortedOffsets[ 0 ]
        for offset in sortedOffsets[ 1: ] + [ None ]:
            if offset is not None and offset == previous + 1:
                previous = offset
                continue
            findings.append( Finding(
                lineCount = ( previous - runStart ) + kWindowLines,
                leftPath  = leftPath,
                leftLine  = fileLines[ leftPath ][ 1 ][ runStart ],
                rightPath = rightPath,
                rightLine = fileLines[ rightPath ][ 1 ][ runStart + offsetDelta ],
            ) )
            if offset is not None:
                runStart = previous = offset

    findings.sort( key = lambda finding: -finding.lineCount )
    return findings


def selectFiles( repositoryRoot: Path, filterText: str, bIncludeHeaders: bool ) -> list[ Path ]:
    """검사할 파일을 **저장소 상대 경로**로 돌려줍니다 (보고가 짧고 클릭 가능한 경로가 되도록)."""
    patterns = [ "*.cpp" ] + ( [ "*.h", "*.inl" ] if bIncludeHeaders else [] )
    selected: list[ Path ] = []
    for pattern in patterns:
        for path in ( repositoryRoot / "Source" ).rglob( pattern ):
            relativePath = path.relative_to( repositoryRoot )
            if filterText and filterText.replace( "\\", "/" ) not in relativePath.as_posix():
                continue
            selected.append( relativePath )
    return sorted( selected )


def parseArgs( argv: list[ str ] | None = None ) -> argparse.Namespace:
    parser = argparse.ArgumentParser( description = "Report duplicated code blocks, longest first." )
    parser.add_argument( "--root", default = None, help = "저장소 루트 (기본: 이 스크립트 기준)" )
    parser.add_argument( "--filter", default = "", help = "경로에 이 문자열이 든 파일만" )
    parser.add_argument( "--min-lines", type = int, default = kDefaultMinLines, help = "보고할 최소 길이" )
    parser.add_argument( "--top", type = int, default = 20, help = "보고할 최대 건수" )
    parser.add_argument( "--same-file-only", action = "store_true", help = "한 파일 안의 복사만" )
    parser.add_argument( "--no-headers", action = "store_true", help = "헤더를 빼고 소스만" )
    return parser.parse_args( argv )


def main( argv: list[ str ] | None = None ) -> int:
    args = parseArgs( argv )
    repositoryRoot = Path( args.root ).resolve() if args.root else Path( __file__ ).resolve().parents[ 3 ]

    files = selectFiles( repositoryRoot, args.filter, bIncludeHeaders = not args.no_headers )
    if not files:
        print( "[DuplicateCode] 검사할 파일이 없습니다." )
        return 0

    print( f"[DuplicateCode] 파일 {len( files )}개에서 {kWindowLines}줄 창을 해시합니다 …" )
    findings = [ f for f in collectFindings( repositoryRoot, files, args.same_file_only ) if f.lineCount >= args.min_lines ]

    if not findings:
        print( f"[DuplicateCode] {args.min_lines}줄 이상 중복 없음." )
        return 0

    print( f"\n[DuplicateCode] {args.min_lines}줄 이상 {len( findings )}건 (긴 것부터 {min( args.top, len( findings ) )}건):\n" )
    for finding in findings[ : args.top ]:
        marker = "  [같은 파일]" if finding.isSameFile() else ""
        print( f"  {finding.lineCount:3d}줄  {finding.leftPath}:{finding.leftLine}" )
        print( f"         {finding.rightPath}:{finding.rightLine}{marker}" )

    print( "\n  고르기 전에 **'백엔드·플랫폼마다 정말로 다른 일을 하는가'** 를 먼저 물어보세요." )
    print( "  그렇다면 넘기고, 아니라면 합칠 값이 있습니다 (이 파일 머리말의 목록 참고)." )
    return 0  # 보고만 한다.


if __name__ == "__main__":
    sys.exit( main() )
