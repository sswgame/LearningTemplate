"""
Scripts/GenerateChangelog.py

Git 커밋 로그를 파싱하여 Documentation/CHANGELOG.md 파일을 자동 생성합니다.
CMake 프리빌드 단계에서 항상 실행되어 프로젝트 문서화가 최신 상태로 유지되도록 돕습니다.
"""

import os
import subprocess
from pathlib import Path

# ==============================================================================
# @function generate_changelog
# @brief 시스템 Git 이력을 조회하고 마크다운 포맷으로 변환하여 CHANGELOG.md 파일을 생성합니다
# ==============================================================================
def generate_changelog():
    """
    @brief generate_changelog 처리를 수행합니다.
    """
    # 현재 스크립트 실행 경로를 기준으로 프로젝트 최상위 루트 디렉토리 산출
    script_dir = Path(__file__).resolve().parent
    project_root = script_dir.parent

    # 문서를 저장할 Documentation 디렉토리가 없을 경우 신규 생성
    docs_dir = project_root / "Documentation"
    docs_dir.mkdir(parents=True, exist_ok=True)

    # 최종적으로 기록될 CHANGELOG.md 파일 경로 지정
    changelog_path = docs_dir / "CHANGELOG.md"

    try:
        # 시스템에 Git 클라이언트가 설치되어 있는지 정상 실행 여부 테스트
        subprocess.run(["git", "--version"], capture_output=True, check=True)

        # Git log 명령어를 통해 해시, 커밋 메시지, 작성자, 작성 날짜를 정형화된 마크다운 리스트 형태로 추출
        result = subprocess.run(
            ["git", "log", "--pretty=format:- `%h` **%s** (*%an*, %ad)", "--date=short"],
            cwd=str(project_root),
            capture_output=True,
            text=True,
            encoding="utf-8",
            check=True
        )
        git_log = result.stdout
    except Exception as e:
        # Git 명령어 실행 오류 발생 시 예외 메시지 삽입
        git_log = f"> Git 커밋 이력을 가져오지 못했습니다. ({str(e)})"

    # 출력할 CHANGELOG.md 파일의 헤더 및 컨텐츠 레이아웃 설정
    content = f"""# Project Changelog

이 문서는 Git 커밋 이력을 기반으로 자동 생성되었습니다. (Auto-Generated)

## Recent Commits
{git_log}
"""

    # UTF-8 포맷으로 추출된 커밋 로그 덮어쓰기 저장
    with open(changelog_path, "w", encoding="utf-8") as f:
        f.write(content)
    print(f"[AutoChangelog] Successfully generated {changelog_path}")

# 스크립트 단독 실행을 위한 진입점 처리
if __name__ == "__main__":
    generate_changelog()
