#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
수정되거나 추가된(Untracked) C++ 파일들에 대해서만 
clang-format과 Include 순서 검사(자동 수정)를 실행하는 스크립트입니다.
"""

import sys
import subprocess
from pathlib import Path

# 부모 경로들을 sys.path에 추가하여 기존 스크립트들을 import 할 수 있게 함
script_dir = Path(__file__).resolve().parent
sys.path.insert(0, str(script_dir))
sys.path.insert(0, str(script_dir.parent))

from ConfigHelper import getProjectRoot, loadToolchainConfig, kKeyLlvmPath
from setup.SetupLlvm import ensureClangFormat
import CheckIncludeOrder

def get_modified_files(root: Path) -> list[Path]:
    files = set()
    
    # 1. Tracked but modified (Staged + Unstaged)
    try:
        res = subprocess.run(["git", "diff", "--name-only", "HEAD"], cwd=str(root), capture_output=True, text=True, check=True)
        for line in res.stdout.splitlines():
            line = line.strip()
            if not line:
                continue
            p = (root / line).resolve()
            if p.exists() and p.suffix.lower() in {".h", ".hpp", ".inl", ".c", ".cpp", ".cc", ".cxx"}:
                files.add(p)
    except subprocess.CalledProcessError:
        pass
        
    # 2. Untracked files
    try:
        res = subprocess.run(["git", "ls-files", "--others", "--exclude-standard"], cwd=str(root), capture_output=True, text=True, check=True)
        for line in res.stdout.splitlines():
            line = line.strip()
            if not line:
                continue
            p = (root / line).resolve()
            if p.exists() and p.suffix.lower() in {".h", ".hpp", ".inl", ".c", ".cpp", ".cc", ".cxx"}:
                files.add(p)
    except subprocess.CalledProcessError:
        pass

    return sorted(list(files))

def main() -> int:
    root = getProjectRoot()
    
    modified_files = get_modified_files(root)
    if not modified_files:
        print("[FormatModified] 변경된 C++ 소스 파일이 없습니다.")
        return 0

    print(f"[FormatModified] {len(modified_files)}개의 수정된 파일 발견.")
    
    # 1. Run CheckIncludeOrder (Auto-fix duplicates and check order)
    print("\n[1/2] Include 순서 및 중복 검사 실행 중...")
    all_violations = []
    for p in modified_files:
        violations = CheckIncludeOrder.processFile(p, root)
        if violations:
            all_violations.extend(violations)
            
    if all_violations:
        for v in all_violations:
            print(f"  - {v}")
    else:
        print("  - Include 검사 OK")
        
    # 2. Run clang-format
    print("\n[2/2] clang-format 실행 중...")
    toolchain = loadToolchainConfig()
    llvmPath = str(toolchain.get(kKeyLlvmPath, "") or "")
    
    import shutil
    clangFormat = shutil.which("clang-format")
    if not clangFormat:
        clangFormat = ensureClangFormat(llvmPath, allowDownload=True)
        
    if not clangFormat:
        print("[FormatModified] clang-format을 찾을 수 없습니다. (SetupEnvironment 필요)")
        return 1
        
    # 배치로 나누어 실행 (명령어 길이 제한 대비)
    batch_size = 64
    for start in range(0, len(modified_files), batch_size):
        batch = modified_files[start : start + batch_size]
        cmd = [clangFormat, "-i"] + [str(p) for p in batch]
        res = subprocess.run(cmd, cwd=str(root))
        if res.returncode != 0:
            print(f"\n[FormatModified] clang-format 실행 중 오류 발생 (exit {res.returncode})")
            return res.returncode
            
    print("\n[FormatModified] 성공적으로 모든 수정된 파일의 포매팅을 완료했습니다!")
    return 0

if __name__ == "__main__":
    sys.exit(main())
