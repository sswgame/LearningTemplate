"""
Scripts/generate/GenerateChangelog.py

Git 커밋 로그 → Documentation/CHANGELOG.md
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

# Scripts/ on path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ConfigHelper import GetProjectRoot


def GenerateChangelog() -> None:
	project_root = GetProjectRoot()
	docs_dir = project_root / "Documentation"
	docs_dir.mkdir(parents=True, exist_ok=True)
	changelog_path = docs_dir / "CHANGELOG.md"

	try:
		subprocess.run(["git", "--version"], capture_output=True, check=True)
		result = subprocess.run(
			["git", "log", "--pretty=format:- `%h` **%s** (*%an*, %ad)", "--date=short"],
			cwd=str(project_root),
			capture_output=True,
			text=True,
			encoding="utf-8",
			check=True,
		)
		git_log = result.stdout
	except Exception as e:
		git_log = f"> Git 커밋 이력을 가져오지 못했습니다. ({str(e)})"

	content = f"""# Project Changelog

이 문서는 Git 커밋 이력을 기반으로 자동 생성되었습니다. (Auto-Generated)

## Recent Commits
{git_log}
"""
	with open(changelog_path, "w", encoding="utf-8") as f:
		f.write(content)
	print(f"[AutoChangelog] Successfully generated {changelog_path}")


if __name__ == "__main__":
	GenerateChangelog()
