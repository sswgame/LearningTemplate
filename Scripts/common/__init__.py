"""
Scripts/common — 공용 라이브러리 층(층 0). 위 층(lint · 명령)을 import 하지 않는다.

패키지는 아무 이름도 다시 내보내지 않는다. 쓰는 쪽은 모듈을 이름으로 부른다(`from common.run_process import run_process`) — 어느 모듈에 기대는지가
import 줄에 보이고, 프로세스마다 쓰지 않는 모듈까지 읽지 않는다. 모듈마다 하는 일은 그 파일 머리말에 있다.

이 파일이 하는 일은 넷이다: 콘솔 인코딩을 UTF-8 로 맞추고, 파이썬 최소 버전을 확인하고, 바이트코드 캐시(`__pycache__`)를 쓰지 않게 하고, 물려받은 git 저장소
환경 변수를 떼어 둔다. 진입점은 모듈 수준에서 `common` 을 import 해 이것들을 받는다.
"""

from __future__ import annotations

import sys as _sys

# 스크립트가 만드는 `__pycache__` 가 폴더마다 생기지 않게 한다(진입점이 이 패키지를 먼저 import 하므로 그 뒤 모듈에 적용된다).
_sys.dont_write_bytecode = True

# ------------------------------------------------------------------------------
# git 저장소 환경 변수 — 프로세스 시작에 떼어 둔다
#
# 커밋 훅은 GIT_DIR · GIT_INDEX_FILE · GIT_WORK_TREE … 를 넘긴다. 물려받은 채로 두면 이 프로세스가 띄우는 모든 git(임시 저장소 · 클론 · 다른 워크트리)과
# 그 git 을 부르는 자식(cmake · 셸)이 훅의 저장소를 가리킨다(임시 저장소 `git init` 이 공용 config 에 core.bare 를 썼다). 예전에는 git 을 띄우는 자리마다
# 떼어 냈다(`run_git` 의 환경, 린트 자체 검사의 손 정리) — 한 자리라도 빠뜨리면 같은 결함이라 여기서 한 번 뗀다. 지금 커밋하는 인덱스를 봐야 하는
# 커밋 훅 린트만 `git.run_git_in_hook_repository` 로 떼어 둔 값을 되돌려 쓴다. 이름은 `git rev-parse --local-env-vars`(git 이 서브모듈에 들어갈 때
# 스스로 지우는 목록)와 같다 — 테스트 `common/tests/test_git_environment.py` 가 그 출력과 견준다.
# ------------------------------------------------------------------------------
import os as _os

REPOSITORY_ENVIRONMENT_NAMES = ("GIT_ALTERNATE_OBJECT_DIRECTORIES", "GIT_CONFIG", "GIT_CONFIG_PARAMETERS", "GIT_CONFIG_COUNT", "GIT_OBJECT_DIRECTORY", "GIT_DIR",
                                "GIT_WORK_TREE", "GIT_IMPLICIT_WORK_TREE", "GIT_GRAFT_FILE", "GIT_INDEX_FILE", "GIT_NO_REPLACE_OBJECTS", "GIT_REPLACE_REF_BASE",
                                "GIT_PREFIX", "GIT_SHALLOW_FILE", "GIT_COMMON_DIR")
HOOK_GIT_ENVIRONMENT: dict[str, str] = {_name: _os.environ.pop(_name) for _name in REPOSITORY_ENVIRONMENT_NAMES if _name in _os.environ}

# ------------------------------------------------------------------------------
# 콘솔 인코딩 — 이 저장소의 스크립트 메시지는 한국어다
#
# Windows 는 stdout 인코딩이 시스템 코드페이지(cp1252·cp437 등)로 잡힌다. 한글을 찍는 순간
# `UnicodeEncodeError` 로 스크립트가 죽고, **빌드 단계에서 돌면 빌드가 통째로 선다**(CI Windows 러너에서
# 쿠킹 첫 줄에 죽고 컴파일은 한 건도 시작되지 않는 모양). 한국어 Windows(cp949)나 리눅스(UTF-8)에서는
# 재현되지 않아 찾기 어렵다.
#
# 더 고약한 것은 **오류 메시지가 먼저 죽는다**는 점이다. "셰이더를 다시 구우십시오" 같은
# 해결 안내가 그 자리에서 트레이스백으로 바뀐다.
#
# 그래서 여기서 한 번 막는다. `errors="replace"` 라 터미널이 글자를 못 그려도 예외는 나지 않는다.
# 스크립트는 이것을 따로 부르지 않는다 — 진입점이 모듈 수준에서 common 을 import 하면 된다.
# ------------------------------------------------------------------------------
for _stream in (_sys.stdout, _sys.stderr):
    if _stream is not None and hasattr(_stream, "reconfigure"):
        try:
            _stream.reconfigure(encoding="utf-8", errors="replace")
        except (ValueError, OSError):
            pass  # 리다이렉트된 파이프 등 — 그대로 둔다

# ------------------------------------------------------------------------------
# 파이썬 최소 버전 — 정본은 Constants.kMinimumPythonVersion
#
# 낮은 인터프리터에서는 스크립트가 엉뚱한 자리(없는 API 의 AttributeError, 구문 오류)에서 죽어 원인이 가려진다.
# 진입점이 모두 common 을 import 하므로 여기서 한 번 검사하고 멈춘다.
# ------------------------------------------------------------------------------
from .project_constants import MINIMUM_PYTHON_VERSION as _MINIMUM_PYTHON_VERSION

if _sys.version_info[:2] < _MINIMUM_PYTHON_VERSION:
    _sys.exit(
        f"[Scripts] Python {_MINIMUM_PYTHON_VERSION[0]}.{_MINIMUM_PYTHON_VERSION[1]} 이상이 필요합니다. "
        f"지금 인터프리터는 {_sys.version.split()[0]} 입니다 ({_sys.executable})."
    )
