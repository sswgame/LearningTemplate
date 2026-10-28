"""
Scripts/common — 공용 라이브러리 층(층 0). 위 층(lint · 명령)을 import 하지 않는다.

패키지는 아무 이름도 다시 내보내지 않는다. 쓰는 쪽은 모듈을 이름으로 부른다(`from common.Process import runProcess`) — 어느 모듈에 기대는지가
import 줄에 보이고, 프로세스마다 쓰지 않는 모듈까지 읽지 않는다. 모듈 목록과 한 줄 설명은 `py -3 -m Scripts --list common` 이다.

이 파일이 하는 일은 둘이다: 콘솔 인코딩을 UTF-8 로 맞추고, 파이썬 최소 버전을 확인한다. 진입점은 모듈 수준에서 `common` 을 import 해
이 둘을 받는다.
"""

from __future__ import annotations

import sys as _sys

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
from .Constants import kMinimumPythonVersion as _kMinimumPythonVersion

if _sys.version_info[:2] < _kMinimumPythonVersion:
    _sys.exit(
        f"[Scripts] Python {_kMinimumPythonVersion[0]}.{_kMinimumPythonVersion[1]} 이상이 필요합니다. "
        f"지금 인터프리터는 {_sys.version.split()[0]} 입니다 ({_sys.executable})."
    )
