"""
Scripts/generate/GenerateDocs.py

Doxygen 문서 생성.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import webbrowser
from pathlib import Path
from typing import Optional, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ConfigHelper import getProjectRoot

def generateDocs(*, openBrowser: bool = False) -> int:
    """
    Doxygen을 실행하여 프로젝트의 공식 문서(API 레퍼런스 등)를 생성합니다.
    """
    rootDir = getProjectRoot()
    doxyfilePath = rootDir / "Doxyfile"
    docsDir = rootDir / "Docs" / "Doxygen" / "html"
    indexHtml = docsDir / "index.html"

    if not doxyfilePath.exists():
        print(f"Warning: Doxyfile not found at {doxyfilePath}; skipping documentation generation.")
        return 0

    print("Running Doxygen...")
    try:
        subprocess.run(["doxygen", "Doxyfile"], cwd=rootDir, check=True)
    except FileNotFoundError:
        print(
            "Error: 'doxygen' command not found. Please ensure Doxygen is installed and added to your system PATH."
        )
        return 1
    except subprocess.CalledProcessError as e:
        print(f"Error: Doxygen failed with exit code {e.returncode}")
        return 1

    print("Doxygen documentation generated successfully.")
    if indexHtml.exists():
        if openBrowser:
            print(f"Opening {indexHtml} in the default browser...")
            webbrowser.open(f"file://{indexHtml.as_posix()}")
        else:
            print(f"Docs: {indexHtml}")
    else:
        print(f"Warning: Could not find {indexHtml} after generating documentation.")
    return 0

def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Generate Doxygen HTML docs.")
    parser.add_argument("--open", action="store_true", help="Open index.html in the default browser.")
    args = parser.parse_args(argv)
    return generateDocs(openBrowser=args.open)

if __name__ == "__main__":
    raise SystemExit(main())
