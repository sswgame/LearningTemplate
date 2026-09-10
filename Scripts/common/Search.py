"""
Scripts/common/Search.py

파일 및 디렉터리 탐색, 검색 루트 템플릿 확장, vcpkg 루트 검증 유틸리티.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any, Callable, Iterable, Iterator

from .Constants import kCppAllExtensions, kLintTargetRelDirs
from .Paths import expandPathTemplate, platformKey

_kRglobSkipDirNames = {"buildtrees", "downloads", "packages"}


def getLintSearchDirs(repositoryRoot: Path) -> list[Path]:
    """린트 및 포맷팅 대상 루트 디렉터리 목록을 반환합니다 (Source, Test, Tools/ReflectionParser)."""
    return [repositoryRoot / rel for rel in kLintTargetRelDirs]


def collectSourceFiles(roots: Iterable[Path],
                       extensions: set[str] | None = None,
                       *,
                       excludeSubdirs: Iterable[str] | None = None) -> list[Path]:
    """
    지정된 디렉터리들에서 C++ 파일(.cpp, .h 등) 목록을 재귀적으로 수집하여 정렬된 리스트로 반환합니다.

    Args:
        roots: 탐색할 루트 디렉터리 목록
        extensions: 대상 파일 확장자 집합 (기본값: kCppAllExtensions)
        excludeSubdirs: 제외할 경로 서브스트링 목록

    Returns:
        정렬된 Path 객체 리스트
    """
    targetExtensions = extensions if extensions is not None else kCppAllExtensions
    excludePatterns = tuple(excludeSubdirs) if excludeSubdirs else ()
    resultList: list[Path] = []
    for root in roots:
        if not root.is_dir():
            continue
        for path in root.rglob("*"):
            if not path.is_file():
                continue
            if path.suffix.lower() not in targetExtensions:
                continue
            posixPath = path.as_posix()
            if any(exclude in posixPath for exclude in excludePatterns):
                continue
            resultList.append(path)
    return sorted(resultList)


def getOrFindCached(existing: dict[str, Any],
                    key: str,
                    findFunc: Callable[..., Any],
                    *args: Any,
                    validate: Any | None = None) -> Any:
    """
    설정 파일에서 기존에 캐시된 값을 확인하고, 유효하지 않으면 탐색 함수(findFunc)를 실행하여 새 값을 찾아 반환합니다.
    """
    cachedValue = existing.get(key)
    if isinstance(cachedValue, list) and cachedValue:
        return cachedValue
    if cachedValue and isinstance(cachedValue, str):
        isValidValue = validate(cachedValue) if callable(validate) else Path(cachedValue).exists()
        if isValidValue:
            return cachedValue
    elif cachedValue and not isinstance(cachedValue, str):
        return cachedValue
    return findFunc(*args)


def findFirstExistingFile(searchDirs: list[Path], fileNames: list[str]) -> str:
    """
    지정된 디렉터리 목록을 순서대로 확인하여, 후보 파일명 중 가장 먼저 발견된 파일의 절대 경로를 반환합니다.
    """
    for directory in searchDirs:
        if not directory.is_dir():
            continue
        for fileName in fileNames:
            if (candidate := directory / fileName).is_file():
                return str(candidate)
    return ""


def findFirstExistingFileRecursive(searchDirs: list[Path], fileNames: list[str]) -> str:
    """
    지정된 디렉터리들을 재귀적으로 탐색하여 일치하는 파일 경로를 찾습니다.
    (단, vcpkg의 buildtrees, downloads, packages 등 임시 빌드 폴더는 탐색에서 건너뜁니다.)
    """
    for directory in searchDirs:
        if not directory.is_dir():
            continue
        for fileName in fileNames:
            try:
                for candidate in directory.rglob(fileName):
                    if not candidate.is_file():
                        continue
                    if any(part in _kRglobSkipDirNames for part in candidate.parts):
                        continue
                    return str(candidate)
            except (OSError, PermissionError):
                pass
    return ""


def findFirstExistingFileInBinDirs(searchDirs: list[Path], fileNames: list[str]) -> str:
    """
    vcpkg 설치 폴더의 bin 및 debug/bin 디렉터리를 우선적으로 탐색하여 일치하는 실행 파일/DLL 경로를 반환합니다.
    """
    for directory in searchDirs:
        if not directory.is_dir():
            continue
        try:
            tripletRoots = [path for path in directory.iterdir() if path.is_dir()]
        except (OSError, PermissionError):
            tripletRoots = []
        for root in [directory, *tripletRoots]:
            for sub in ("bin", "debug/bin"):
                folder = root / sub
                if not folder.is_dir():
                    continue
                for fileName in fileNames:
                    if (candidate := folder / fileName).is_file():
                        return str(candidate)
    return findFirstExistingFileRecursive(searchDirs, fileNames)


def iterSearchRootTemplates(templates: Iterable[str],
                            *,
                            extras: dict[str, str] | None = None,
                            skip: Path | None = None) -> Iterator[Path]:
    """
    경로 템플릿 목록(${sourceDir}, 환경변수 등)을 실제 절대 경로로 치환하고, 실제로 존재하는 디렉터리만 순회(yield)합니다.
    """
    skipResolved: Path | None = None
    if skip is not None:
        try:
            skipResolved = skip.resolve()
        except OSError:
            skipResolved = None

    for template in templates:
        root = Path(expandPathTemplate(str(template), extras=extras))
        if not root.exists():
            continue
        if skipResolved is not None:
            try:
                if root.resolve() == skipResolved:
                    continue
            except OSError:
                pass
        yield root


def findFirstValidRoot(templates: Iterable[str],
                       isValid: Callable[[Path], bool],
                       *,
                       extras: dict[str, str] | None = None,
                       skip: Path | None = None) -> Path | None:
    """
    경로 템플릿 목록에서 유효성 검사 함수(isValid)를 통과하는 첫 번째 디렉터리의 절대 경로를 반환합니다.
    """
    return next(
        (
            root.resolve()
            for root in iterSearchRootTemplates(templates, extras=extras, skip=skip)
            if isValid(root)
        ),
        None,
    )


def _naturalVersionKeyInternal(text: str) -> list[Any]:
    """`llvm-9` 와 `llvm-21` 을 사전순이 아니라 숫자로 비교하기 위한 정렬 키입니다."""
    parts: list[Any] = []
    digits = ""
    for ch in text:
        if ch.isdigit():
            digits += ch
            continue
        if digits:
            parts.append(int(digits))
            digits = ""
        parts.append(ch)
    if digits:
        parts.append(int(digits))
    # int 와 str 이 섞이면 비교가 터진다 — 종류를 앞에 붙여 같은 종류끼리만 비교되게 한다.
    return [(0, v, "") if isinstance(v, int) else (1, 0, v) for v in parts]


def expandSearchRootGlobsInternal(roots: list[str]) -> list[str]:
    """
    `*` 가 든 절대 경로 항목을 실제로 존재하는 디렉터리들로 펼칩니다.

    손으로 `/usr/lib/llvm-20 … llvm-14` 를 나열해 두면 배포판이 새 메이저를 올릴 때마다 목록
    밖으로 조용히 비켜간다(실제로 llvm-21 이 그랬다). 펼친 결과는 **높은 버전이 먼저** 오도록
    자연순 내림차순으로 둔다 — 먼저 찾은 것을 쓰는 탐색이므로 최신이 앞이어야 한다.

    `${sourceDir}` 같은 템플릿이 든 항목은 여기서 건드리지 않는다 — 확장은 뒷단이 한다.
    """
    expanded: list[str] = []
    for root in roots:
        if "*" not in root or "${" in root:
            expanded.append(root)
            continue
        try:
            matches = [str(m) for m in Path("/").glob(root.lstrip("/")) if m.is_dir()] if root.startswith("/") \
                else [str(m) for m in Path(".").glob(root) if m.is_dir()]
        except OSError:
            matches = []
        expanded.extend(sorted(matches, key=_naturalVersionKeyInternal, reverse=True))
    return expanded


def platformSearchRoots(search: dict[str, Any], rootsKey: str) -> list[str]:
    """
    설정 파일(search_paths.json)에서 현재 OS 플랫폼(windows, linux, darwin)에 해당하는 탐색 경로 목록을 가져옵니다.

    `*` 가 든 항목은 실제 디렉터리로 펼쳐진다(자연순 내림차순).
    """
    raw = search.get(rootsKey, [])
    if isinstance(raw, dict):
        return expandSearchRootGlobsInternal(list(raw.get(platformKey(), []) or []))
    if isinstance(raw, list):
        return expandSearchRootGlobsInternal([str(item) for item in raw])
    return []


def isVcpkgRoot(path: Path | str) -> bool:
    """
    vcpkg.cmake 파일의 존재 여부를 확인하여, 지정된 경로가 올바른 vcpkg 설치 루트인지 검사합니다.
    """
    try:
        return (Path(path) / "scripts" / "buildsystems" / "vcpkg.cmake").is_file()
    except OSError:
        return False
