"""
Scripts/GenerateChangelog.py

Git 커밋 로그를 파싱하여 Documentation/CHANGELOG.md 를 생성합니다.

네이밍: 공개 함수는 PascalCase (Scripts 공통 규칙).
"""

import subprocess
from pathlib import Path


def GenerateChangelog() -> None:
	"""Git 이력을 마크다운 CHANGELOG.md 로 기록합니다."""
	script_dir = Path(__file__).resolve().parent
	project_root = script_dir.parent

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
