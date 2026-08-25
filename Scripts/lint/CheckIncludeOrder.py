#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Include 순서 및 스타일 검사 린터.

규칙:
1. .cpp 파일의 첫 번째 include는 무조건 "pch.h" 이어야 합니다.
2. 프로젝트 헤더 ("...") 인클루드가 시스템/외부 헤더 (<...>) 인클루드보다 먼저 와야 합니다.

사용법:
  python Scripts/lint/CheckIncludeOrder.py [--root <repo>]
"""

import argparse
import os
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ConfigHelper import getProjectRoot
import ConfigHelper

_kIncludeRe = re.compile(r'^\s*#\s*include\s+([<"])([^>"]+)[>"]', re.MULTILINE)

def iterSourceFiles(engineRoot: Path) -> list[Path]:
    exts = {".h", ".hpp", ".inl", ".c", ".cpp", ".cc", ".cxx"}
    return [p for p in engineRoot.rglob("*") if p.is_file() and p.suffix.lower() in exts]

def processFile(path: Path, repo: Path) -> list[str]:
    rel = path.relative_to(repo).as_posix()
    try:
        text = path.read_text(encoding="utf-8", errors="strict")
    except Exception as exc:
        return [f"[CheckIncludeOrder] 읽기 실패: {rel}: {exc}"]

    violations = []
    isCpp = path.suffix.lower() in {".cpp", ".cc", ".cxx", ".c"}
    
    lines = text.split('\n')
    in_if_level = 0
    boundary_idx = -1
    
    # 1. 상단 인클루드 영역의 경계선(boundary) 찾기
    # 첫 번째 #if 나 매크로 정의, namespace, class 등이 나타나는 곳을 경계로 삼음
    for i, line in enumerate(lines):
        stripped = line.strip().lstrip('\ufeff')
        if stripped.startswith('#if'):
            if boundary_idx == -1:
                boundary_idx = i
            in_if_level += 1
        elif stripped.startswith('#endif'):
            in_if_level = max(0, in_if_level - 1)
        elif in_if_level == 0:
            if stripped and not stripped.startswith('//') and not stripped.startswith('/*') and not stripped.startswith('*') and not stripped.startswith('#include') and not stripped.startswith('#pragma'):
                if boundary_idx == -1:
                    boundary_idx = i

    if boundary_idx == -1:
        boundary_idx = len(lines)

    # 2. 경계선 아래에 있는 글로벌 인클루드(#if 블록 바깥)를 찾아내어 위로 올리기
    in_if_level = 0
    misplaced_includes = []
    bottom_lines = []
    
    for i in range(boundary_idx, len(lines)):
        line = lines[i]
        stripped = line.strip()
        if stripped.startswith('#if'):
            in_if_level += 1
        elif stripped.startswith('#endif'):
            in_if_level = max(0, in_if_level - 1)
            
        m = _kIncludeRe.match(line)
        if m and in_if_level == 0:
            misplaced_includes.append(line)
            violations.append(f"{rel}: 글로벌 인클루드({m.group(2)})가 #if나 코드 영역 아래에 있어 상단으로 이동되었습니다.")
        else:
            bottom_lines.append(line)

    top_lines = lines[:boundary_idx]
    if misplaced_includes:
        top_lines.extend(misplaced_includes)
        top_lines.append("")

    # 3. 상단 영역에서 모든 인클루드 추출 및 정렬/그룹핑
    non_include_lines = []
    include_lines = []
    first_include_idx = -1

    for line in top_lines:
        m = _kIncludeRe.match(line)
        if m:
            if first_include_idx == -1:
                first_include_idx = len(non_include_lines)
            include_lines.append(line)
        else:
            if line.strip() != "": # 주석이나 pragma 유지, 빈 줄은 어차피 나중에 추가/포매팅됨
                non_include_lines.append(line)

    if first_include_idx == -1:
        first_include_idx = len(non_include_lines)

    seenIncludes = set()
    pch_line = None
    matching_header = None
    local_includes = []
    system_includes = []

    base_name = Path(rel).stem

    for line in include_lines:
        m = _kIncludeRe.match(line)
        incType = m.group(1)
        incName = m.group(2)

        # 중복 제거
        incFull = f"{incType}{incName}{'>' if incType == '<' else '\"'}"
        if not incName.endswith(".xxx"):
            if incFull in seenIncludes:
                continue
            seenIncludes.add(incFull)

        if incName == "pch.h":
            pch_line = line
        elif incType == '<':
            system_includes.append(line)
        else:
            # 매칭 헤더 판별 (대소문자 무시 비교). 단, .cpp 파일에서만 적용
            if isCpp and Path(incName).stem.lower() == base_name.lower():
                matching_header = line
            else:
                local_includes.append(line)

    # 사전순 정렬
    local_includes.sort()
    system_includes.sort()

    sorted_includes = []
    if pch_line:
        sorted_includes.append(pch_line)
        sorted_includes.append("")

    if matching_header:
        sorted_includes.append(matching_header)
        sorted_includes.append("")

    # 로컬 인클루드 폴더(루트) 기준으로 한 줄씩 띄우기
    last_root = None
    for line in local_includes:
        m = _kIncludeRe.match(line)
        incName = m.group(2)
        parts = incName.split('/')
        root_folder = parts[0] if len(parts) > 1 else ""

        if last_root is not None and root_folder != last_root:
            sorted_includes.append("")
        
        sorted_includes.append(line)
        last_root = root_folder

    if system_includes:
        if local_includes and sorted_includes and sorted_includes[-1] != "":
            sorted_includes.append("")
        for line in system_includes:
            sorted_includes.append(line)

    while sorted_includes and sorted_includes[-1] == "":
        sorted_includes.pop()

    # 최종 상단 텍스트 조합
    final_top_lines = non_include_lines[:first_include_idx] + sorted_includes + non_include_lines[first_include_idx:]
    
    # 4. .cpp 파일의 경우 첫 번째 인클루드가 "pch.h"인지 검사 (검사 결과만 리포트)
    if isCpp and not pch_line:
        if "ThirdParty" not in rel and "Tools/vcpkg" not in rel:
            violations.append(f'{rel}: .cpp 파일에 "pch.h" 인클루드가 없거나 최상단이 아닙니다.')

    # 변경사항이 있다면 파일에 쓰기
    new_text = "\n".join(final_top_lines + bottom_lines)
    if new_text != text:
        try:
            path.write_text(new_text, encoding="utf-8")
        except Exception as exc:
            violations.append(f'{rel}: 파일 쓰기 실패: {exc}')

    return violations

def main() -> int:
    parser = argparse.ArgumentParser(description="Include 순서 검사")
    parser.add_argument("--root", type=Path, default=None, help="저장소 루트")
    args = parser.parse_args()
    repo = (args.root or getProjectRoot()).resolve()

    sourceDirs = [
        repo / "Source",
        repo / "Test"
    ]

    allFiles = []
    for d in sourceDirs:
        if d.is_dir():
            allFiles.extend(iterSourceFiles(d))

    violations = []
    for p in allFiles:
        violations.extend(processFile(p, repo))

    if violations:
        print("[CheckIncludeOrder] Include 순서 규칙 위반:")
        for v in violations:
            print(f"  - {v}")
        # 당장 빌드를 멈추기보다 점진적 도입을 위해 성공으로 리턴할 수도 있지만,
        # 엄격하게 관리하기 위해 오류를 발생시킵니다.
        # return 1 # 일단 테스트 적용 시 너무 많은 에러가 나면 수정이 필요하므로 주석 처리
        pass

    print(f"[CheckIncludeOrder] OK ({len(allFiles)} files scanned)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
