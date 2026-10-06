"""
Scripts/common package

도메인별로 분리된 스크립트 공통 모듈:
  - AppBinary: 빌드된 App.exe 를 찾고 헤드리스로 셰이더를 쿠킹하는 자리 (쿠커·커밋 훅 공용)
  - BuildTree: 빌드 폴더 하나(build/<프리셋>) — 고르기(--preset · --build-dir · .clangd · 프리셋 순서) · 컴파일 DB · CMakeCache · 짓지 않는 소스
  - Constants: 고정 경로, 파일명, C++ 확장자 세트, JSON 스키마 키
  - Paths: 프로젝트 루트, 경로 정규화, 플랫폼 판별
  - Config: JSON 설정 읽기/쓰기/병합
  - Search: 파일/디렉터리 탐색, C++ 소스 파일 수집 및 vcpkg 판별
  - Archive: 네트워크 다운로드, SHA256 검증, 안전한 압축 해제
  - Host: Git 탐색/실행/파일 쿼리 및 clang-format 배치 실행
  - GeneratedFile: 생성 파일 쓰기(바뀌었을 때만) · CMake 값 이스케이프 · 생성기 진입점(runGenerator)
  - Process: 자식 프로세스 한 창구(runProcess — UTF-8 디코딩, 못 띄움 · 시간 초과를 ProcessResult 칸으로)
  - PackFormat: .pack 바이너리 계약(Config/Engine/PackFormat.json)을 읽은 객체(PackFormatSpec) — 쿠커와 헤더 생성기가 공유
  - CookContract: 쿠킹 표(Config/Engine/CookContract.json — RHI 백엔드 · 쿡 접미사)를 읽은 객체(CookContractSpec) — 쿠커와 헤더 생성기가 공유
  - Parallel: 동시 처리 한 자리 — 워커 수 정책과 map/flatMap 패턴 (스레드인 이유도 여기 적혀 있다)
  - TranslationUnits: 컴파일 DB 에서 TU 를 골라 자식 프로세스로 훑는 자리 (RunClangTidy · RunBuildWarnings 공용)
  - ConfigCatalog: 설정 파일 목록(층 · 읽는 곳 · 언제 · 배포본 · 커밋) — docs/Config 생성과 CheckConfigReference 게이트가 같이 읽는다
  - ConfigReference: 설정 참조 문서(docs/Config)를 코드(PROPERTY · ConfigKeyDoc · 전역 변수 · ArgumentList · CMake 옵션)에서 만든다
  - ToolLocator: 선언적 ToolSpec 기반 도구 탐색(환경 변수 · PATH · 검색 루트 · 다운로드)
  - ClangFormat: clang-format 찾기 · 고정 버전 확인 · 설치(포맷하는 쪽과 설치하는 쪽이 같은 규칙)
  - CodeText: C++ · HLSL 글에서 주석 · 리터럴을 같은 길이 공백으로 가리기(게이트 공용)
  - AssetPipeline: 쿠커의 기본 출력 폴더(가장 최근에 구성된 build/*/Bin/<subDir>)

패키지가 다시 내보내지 않는 모듈(쓰는 쪽이 `from common.X import …` 로 직접 부른다 — 위의 ConfigCatalog · ConfigReference 도):
  - HeaderSelfContained: 헤더 하나를 혼자 컴파일해 보는 자리(자립 보고서 · 훅 게이트 · 전방 선언 후보)
  - AppRun: 빌드된 App 한 판 — 출력 모으기 · 못 도는 백엔드 판정 · 프로파일 표 · 밖에서 자원 재기 · QA 공통 인자(qa/ · dev/)
  - ImageMetrics: 스크린샷 비교 — PPM · PNG, 축소, 배경을 뺀 지표 · 허용 오차
  - AssetValidation: 에셋 검증 규칙의 연산자들(Config/Editor/AssetValidationRules.json)
  - XmlAssetMerge: XML 에셋 의미 비교 · 3-way 병합
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
# 스크립트는 이것을 따로 부르지 않는다 — 진입점이 모듈 수준에서 common 을 import 하면 된다(CheckScriptEntryPoints).
# ------------------------------------------------------------------------------
for _stream in (_sys.stdout, _sys.stderr):
    if _stream is not None and hasattr(_stream, "reconfigure"):
        try:
            _stream.reconfigure(encoding="utf-8", errors="replace")
        except (ValueError, OSError):
            pass  # 리다이렉트된 파이프 등 — 그대로 둔다

from . import (AppBinary, Archive, AssetPipeline, BuildTree, ClangFormat, CodeText, Config, Constants, CookContract, GeneratedFile, Host, PackFormat, Parallel,
               Paths, Process, Search, ToolLocator, TranslationUnits)
from .AppBinary import *
from .Archive import *
from .AssetPipeline import *
from .BuildTree import *
from .ClangFormat import *
from .CodeText import *
from .Config import *
from .Constants import *
from .CookContract import *
from .GeneratedFile import *
from .Host import *
from .PackFormat import *
from .Parallel import *
from .Paths import *
from .Process import *
from .Search import *
from .ToolLocator import *
from .TranslationUnits import *
