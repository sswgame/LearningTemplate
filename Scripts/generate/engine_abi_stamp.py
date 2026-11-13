"""Core · Engine 헤더 내용의 지문을 C++ 헤더로 씁니다 — 핫 리로드의 엔진 ABI 스탬프.

핫 리로드는 모듈 DLL/SO 만 갈아 끼우고 Engine 은 그대로 둡니다. 그런데 Engine 헤더를 고친 빌드에서 `Engine.dll` 은 실행 중이라
다시 링크되지 못해도(잠김) 모듈은 **새 헤더로** 빌드되어 써질 수 있습니다. 그 모듈을 올리면 돌고 있는 엔진과 구조체 배치 · vtable 이
어긋나 조용히 망가집니다. 그래서 Engine 과 모듈이 **같은 헤더 지문**을 바이너리에 박고, 핫 리로드는 올리기 전에 둘을 대조합니다.

지문은 헤더 내용 전부(주석 포함)입니다 — 경로와 파일마다의 내용 SHA-1 을 차례로 넣은 SHA-1 이고, 파일 지문은 빌드 트리의 캐시(`--cache`)에서
(mtime, 크기)가 같으면 다시 읽지 않습니다(`file_digest_cache.py`). 주석만 바꿔도 스탬프가 바뀌는 것은 일부러입니다 — 헤더가 바뀐 빌드는 Engine 도 다시 링크해야
맞으므로, 그 빌드의 모듈은 재시작 전까지 받지 않는 쪽이 안전합니다. 내용이 같으면 파일을 다시 쓰지 않아(재빌드를 부르지 않는다)
Ninja 의 restat 이 그 뒤를 멈춥니다.

사용법: py -3 Scripts/generate/engine_abi_stamp.py --root <repo> --out <path/EngineABIStamp.gen.h> --cache <빌드 트리 안 json>
"""
from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts — common

from generate.file_digest_cache import FileDigestCache  # noqa: E402
from generate.generator_output import write_generated_file  # noqa: E402
from common.exit_code import ExitCode  # noqa: E402

# 핫 리로드가 갈아 끼우지 않는 쪽의 헤더다 — Core · Engine, 모듈과 호스트의 계약(ModuleAPI), 공용 모듈(GameFramework — 섀도 복사 없이 한 번
# 올리고 다시 올리지 않는다). 키트(GameFramework/Kits)는 스스로 리로드되는 모듈이라 뺀다. X 매크로 표(.xxx)도 넣는다 — `PredefinedNameType.xxx` 가
# 예약 이름 색인을 정한다. 주의: 표를 빼면 거기에 줄을 더한 모듈이 같은 스탬프로 옛 엔진에 올라 엉뚱한 이름을 읽는다.
HEADER_ROOTS = ("Source/Core", "Source/Engine", "Source/ModuleAPI", "Source/GameFramework")
EXCLUDED_ROOTS = ("Source/GameFramework/Kits",)
HEADER_EXTENSIONS = (".h", ".hpp", ".inl", ".xxx")


def _collect_header_paths(repository_root: Path) -> list[str]:
    """해시할 헤더를 저장소 기준 경로 순으로 모읍니다. 순서가 스탬프에 들어가므로 정렬합니다."""
    list_path: list[str] = []
    for header_root in HEADER_ROOTS:
        for dir_path, _, list_file_name in os.walk(repository_root / header_root):
            for file_name in list_file_name:
                if not file_name.endswith(HEADER_EXTENSIONS):
                    continue
                relative_path = (Path(dir_path) / file_name).relative_to(repository_root).as_posix()
                if relative_path.startswith(tuple(excluded_root + "/" for excluded_root in EXCLUDED_ROOTS)):
                    continue
                list_path.append(relative_path)
    list_path.sort()
    return list_path


def _compute_stamp(repository_root: Path, cache: FileDigestCache) -> str:
    """헤더 경로와 내용 지문(줄 끝은 LF 로 맞춤)을 차례로 넣은 SHA-1 입니다. 체크아웃마다 다른 CRLF 가 스탬프를 바꾸지 않게 합니다."""
    return cache.tree_digest(repository_root, _collect_header_paths(repository_root)).hex()


def _make_header_text(stamp: str) -> str:
    return (
        "// 생성 파일 - Scripts/generate/engine_abi_stamp.py. 고치지 마십시오.\n"
        "// Core · Engine 헤더 내용의 지문입니다. Engine 과 모듈이 같은 값을 박고, 핫 리로드가 올리기 전에 대조합니다.\n"
        "#pragma once\n"
        "// 머리(swEngineABIStamp:)는 C++ 한 곳(Engine/Module/EngineABIStampMarker.h)이 정한다 — 생성기는 지문만 낸다.\n"
        "#define SW_ENGINE_ABI_STAMP_DIGEST \"" + stamp + "\"\n"
    )


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Core · Engine 헤더 지문(핫 리로드 ABI 도장)을 씁니다.")
    parser.add_argument("--root", required=True, help="저장소 루트")
    parser.add_argument("--out", required=True, help="쓸 헤더 경로")
    parser.add_argument("--cache", required=True, help="파일 지문 캐시(빌드 트리 안 JSON)")
    args = parser.parse_args(argv)

    cache = FileDigestCache(Path(args.cache))
    text = _make_header_text(_compute_stamp(Path(os.path.normpath(Path(args.root).absolute())), cache))
    cache.save()
    # 빌드마다 도는 단계라 바뀐 때만 한 줄 찍는다. 줄끝은 LF(어느 체크아웃에서나 같은 바이트).
    write_generated_file(Path(args.out), text, tag="EngineABIStamp", newline="\n", report_unchanged=False)
    return ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
