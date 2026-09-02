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

from . import Archive, AssetPipeline, Config, Constants, Host, Paths, Search, ToolLocator
from .Archive import *
from .AssetPipeline import *
from .Config import *
from .Constants import *
from .Host import *
from .Paths import *
from .Search import *
from .ToolLocator import *
