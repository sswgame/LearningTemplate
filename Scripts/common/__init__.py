"""
Scripts/common package

도메인별로 분리된 스크립트 공통 모듈:
  - Constants: 고정 경로, 파일명, C++ 확장자 세트, JSON 스키마 키
  - Paths: 프로젝트 루트, 경로 정규화, 플랫폼 판별
  - Config: JSON 설정 읽기/쓰기/병합
  - Search: 파일/디렉터리 탐색, C++ 소스 파일 수집 및 vcpkg 판별
  - Archive: 네트워크 다운로드, SHA256 검증, 안전한 압축 해제
  - Host: Git 탐색/실행/파일 쿼리 및 clang-format 배치 실행
"""

from __future__ import annotations

import sys as _sys

# ------------------------------------------------------------------------------
# 콘솔 인코딩 — 이 저장소의 스크립트 메시지는 한국어다
#
# Windows 는 stdout 인코딩이 시스템 코드페이지(cp1252·cp437 등)로 잡힌다. 한글을 찍는 순간
# `UnicodeEncodeError` 로 스크립트가 죽고, **빌드 단계에서 돌면 빌드가 통째로 선다.**
# 실제로 그렇게 CI(Windows Shipping)가 한 번 깨졌다 — 컴파일은 한 건도 시작되지 않았고
# (sccache 0 hits / 0 misses) 쿠킹 첫 줄에서 죽은 것이었다. 한국어 Windows(cp949)나 리눅스
# (UTF-8)에서는 재현되지 않아 더 찾기 어려웠다.
#
# 더 고약한 것은 **오류 메시지가 먼저 죽는다**는 점이다. "셰이더를 다시 구우십시오" 같은
# 해결 안내가 그 자리에서 트레이스백으로 바뀐다.
#
# 그래서 여기서 한 번 막는다. `errors="replace"` 라 터미널이 글자를 못 그려도 예외는 나지 않는다.
# ------------------------------------------------------------------------------
for _stream in (_sys.stdout, _sys.stderr):
    if _stream is not None and hasattr(_stream, "reconfigure"):
        try:
            _stream.reconfigure(encoding="utf-8", errors="replace")
        except (ValueError, OSError):
            pass  # 리다이렉트된 파이프 등 — 그대로 둔다

from . import Archive, AssetPipeline, Config, Constants, Host, Paths, Search, ToolLocator
from .Archive import *
from .AssetPipeline import *
from .Config import *
from .Constants import *
from .Host import *
from .Paths import *
from .Search import *
from .ToolLocator import *
