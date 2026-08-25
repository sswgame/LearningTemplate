#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
PreCommitLint.py

Git pre-commit 훅에서 호출되어, 
Staged 상태인 C++ 파일들에 대해서만 포맷팅 및 컨벤션 검사를 수행합니다.
검사에 실패하면 0이 아닌 값을 반환하여 커밋을 취소시킵니다.
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
import CheckCodeConventions

def get_staged_cpp_files(root: Path) -> list[Path]:
    files = set()
    try:
        # ACM: Added, Copied, Modified
        res = subprocess.run(
            ["git", "diff", "--cached", "--name-only", "--diff-filter=ACM"], 
            cwd=str(root), capture_output=True, text=True, check=True
        )
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
    staged_files = get_staged_cpp_files(root)
    
    if not staged_files:
        return 0

    print(f"[PreCommitLint] {len(staged_files)}개의 Staged 파일에 대해 검사를 시작합니다.")
    has_errors = False

    # 1. Include Order Check (Dry-run)
    print("\n[1/3] Include 순서 및 중복 검사...")
    for p in staged_files:
        try:
            # CheckIncludeOrder.py automatically writes to the file if it finds issues
            old_content = p.read_text(encoding="utf-8-sig")
            violations = CheckIncludeOrder.processFile(p, root)
            if violations:
                new_content = p.read_text(encoding="utf-8-sig")
                if old_content != new_content:
                    print(f"  [Error] {p.relative_to(root)}: Include 순서/중복 문제가 발견되었습니다. 파일이 자동으로 수정되었으나, 변경사항을 다시 git add 해야 합니다.")
                    has_errors = True
                for v in violations:
                    print(f"    - {v}")
        except Exception as e:
            print(f"  [Warning] {p.relative_to(root)} 처리 중 오류: {e}")

    # 2. CheckCodeConventions
    print("\n[2/3] 코딩 컨벤션 검사...")
    staged_strs = [str(f) for f in staged_files]
    violations = CheckCodeConventions.runConventionsCheck(root, staged_strs)
    if violations:
        has_errors = True
        print(f"  [Error] 코딩 컨벤션 위반 {len(violations)}건이 발견되었습니다:")
        categoryGroups = {}
        for v in violations:
            categoryGroups.setdefault(v.rule_category, []).append(v)
        for cat, items in sorted(categoryGroups.items()):
            print(f"    [{cat}]")
            for item in items:
                print(f"      {item.file_path}:{item.line_number} -> {item.message}")
    else:
        print("  - 코딩 컨벤션 OK")

    # 3. clang-format (Dry-run with Werror)
    print("\n[3/3] clang-format 검사...")
    toolchain = loadToolchainConfig()
    llvmPath = str(toolchain.get(kKeyLlvmPath, "") or "")
    
    import shutil
    clangFormat = shutil.which("clang-format")
    if not clangFormat:
        clangFormat = ensureClangFormat(llvmPath, allowDownload=True)
        
    if not clangFormat:
        print("[PreCommitLint] clang-format을 찾을 수 없습니다.")
        has_errors = True
    else:
        cmd = [clangFormat, "--dry-run", "--Werror"] + staged_strs
        res = subprocess.run(cmd, cwd=str(root))
        if res.returncode != 0:
            has_errors = True
            print("  [Error] 포맷팅 규칙에 어긋나는 파일이 있습니다. 'python Scripts/lint/FormatModified.py'를 실행하여 자동 수정한 뒤 다시 git add 하세요.")
        else:
            print("  - 포맷팅 OK")

    if has_errors:
        print("\n[PreCommitLint] ❌ 검사에 실패했습니다. 오류를 수정한 후 다시 git add 하고 커밋해주세요.")
        return 1
        
    print("\n[PreCommitLint] ✅ 모든 검사를 통과했습니다.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
