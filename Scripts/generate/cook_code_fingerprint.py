"""쿠커 코드의 지문과 셰이더 컴파일러 버전을 C++ 헤더로 씁니다 — 쿠킹 재사용 판단의 입력을 사람이 올리지 않고 산출합니다.

쿠킹은 입력이 그대로면 옛 결과를 재사용합니다. 손으로 올리는 쿠커 번호를 키에 넣으면 형식이나 규칙을 고치고 번호를
잊었을 때 옛 쿠킹본이 배포본에 실립니다(프리팹 형식 2 → 3 에서 실제로 났다). 쿠커 코드의 내용을 키에 넣으면 잊을 번호가 없습니다.

쿠커마다 볼 폴더 · 파일은 `Config/Dev/CookCodeInputs.json` 이 정합니다. 없는 경로는 오류입니다(조용히 빠지면 지문이 그 코드를 못 본다).
지문은 내용 전부(주석 포함)입니다 — 주석만 고쳐도 그 쿠커는 다시 굽는데, 쿠커가 어떤 변경이 출력에 닿는지 판단하지 않는 쪽이 안전합니다.
테스트(`Tests/` 폴더)는 쿠커 출력에 닿지 않으므로 뺍니다. 줄 끝은 LF 로 맞춰 어느 체크아웃 · OS 에서나 같은 값입니다.
파일마다의 내용 지문은 빌드 트리의 캐시(`--cache`)에서 (mtime, 크기)가 같으면 다시 읽지 않습니다(`file_digest_cache.py` — 쿠커끼리 겹치는 파일도 한 번).

임포터는 일부러 넣지 않습니다 — 텍스처 BC7 은 장당 수 분이라 소스를 고칠 때마다 모두 다시 임포트할 수 없습니다(`kImporterVersion`).

사용법: py -3 Scripts/generate/cook_code_fingerprint.py --root <repo> --inputs <json> --vcpkg-status <status> --out <path/CookCodeFingerprint.gen.h> --cache <빌드 트리 안 json>
"""
from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts — common

from generate.file_digest_cache import FileDigestCache  # noqa: E402
from generate.generator_output import write_generated_file  # noqa: E402
from common.exit_code import ExitCode  # noqa: E402

SOURCE_EXTENSIONS = (".h", ".hpp", ".cpp", ".inl", ".xxx")
SHADER_COMPILER_PACKAGE = "directx-dxc"


def _collect_files(repository_root: Path, list_input: Sequence[str]) -> list[str]:
    """입력 경로(폴더 · 파일)의 소스를 저장소 기준 경로 순으로 모읍니다. 없는 경로는 오류입니다."""
    list_path: list[str] = []
    for input_path in list_input:
        absolute_path = repository_root / input_path
        if absolute_path.is_file():
            list_path.append(input_path)
            continue
        if not absolute_path.is_dir():
            raise ValueError(f"cook code input does not exist: {input_path}")
        for dir_path, list_dir_name, list_file_name in os.walk(absolute_path):
            list_dir_name[:] = [name for name in list_dir_name if name != "Tests"]
            for file_name in list_file_name:
                if file_name.endswith(SOURCE_EXTENSIONS):
                    list_path.append((Path(dir_path) / file_name).relative_to(repository_root).as_posix())
    return sorted(set(list_path))


def _compute_fingerprint(repository_root: Path, list_input: Sequence[str], cache: FileDigestCache) -> int:
    """경로와 내용 지문(줄 끝은 LF)을 차례로 넣은 SHA-1 의 앞 64 비트입니다. 0 은 "없음" 이라 쓰지 않습니다."""
    return int.from_bytes(cache.tree_digest(repository_root, _collect_files(repository_root, list_input))[:8], "big") or 1


def _read_installed_version(status_path: Path, package_name: str) -> str:
    """vcpkg 설치 기록(`vcpkg/status`)에서 패키지 버전을 읽습니다. 없으면 오류입니다."""
    in_package = False
    for line in status_path.read_text(encoding="utf-8").splitlines():
        if line.startswith("Package: "):
            in_package = line == f"Package: {package_name}"
        elif in_package and line.startswith("Version: "):
            return line[len("Version: "):].strip()
    raise ValueError(f"{package_name} is not in the vcpkg install record: {status_path}")


def _make_header_text(map_fingerprint: dict[str, int], compiler_version: str) -> str:
    list_row = [f'        {{ "{name}", 0x{value:016x}ull }},\n' for name, value in sorted(map_fingerprint.items())]
    return (
        "// 생성 파일 - Scripts/generate/cook_code_fingerprint.py. 고치지 마십시오.\n"
        "#pragma once\n\n"
        "namespace sw::cookcode\n{\n"
        f'    inline constexpr const utf8* kShaderCompilerVersion = "{SHADER_COMPILER_PACKAGE} {compiler_version}";\n\n'
        "    inline constexpr CookCodeFingerprintRow kArrRow[] = {\n"
        + "".join(list_row)
        + "    };\n"
        "} // namespace sw::cookcode\n"
    )


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="쿠커 코드 지문과 셰이더 컴파일러 버전을 C++ 헤더로 씁니다")
    parser.add_argument("--root", required=True, help="저장소 루트")
    parser.add_argument("--inputs", required=True, help="쿠커별 입력 경로 JSON")
    parser.add_argument("--vcpkg-status", required=True, help="vcpkg 설치 기록(<installed>/vcpkg/status)")
    parser.add_argument("--out", required=True, help="쓸 헤더 경로")
    parser.add_argument("--cache", required=True, help="파일 지문 캐시(빌드 트리 안 JSON)")
    args = parser.parse_args(argv)

    repository_root = Path(os.path.normpath(Path(args.root).absolute()))
    try:
        map_input: dict[str, list[str]] = json.loads(Path(args.inputs).read_text(encoding="utf-8"))
        cache = FileDigestCache(Path(args.cache))
        map_fingerprint = {name: _compute_fingerprint(repository_root, list_input, cache) for name, list_input in map_input.items()}
        cache.save()
        compiler_version = _read_installed_version(Path(args.vcpkg_status), SHADER_COMPILER_PACKAGE)
    except (OSError, ValueError, TypeError, AttributeError) as error:
        print(f"[CookCodeFingerprint] {error}", file=sys.stderr)
        return ExitCode.CHECK_INVALID
    # 빌드마다 도는 단계라 바뀐 때만 한 줄 찍는다. 내용이 같으면 파일을 다시 쓰지 않아 Ninja 의 restat 이 그 뒤를 멈춘다.
    write_generated_file(Path(args.out), _make_header_text(map_fingerprint, compiler_version), tag="CookCodeFingerprint", newline="\n", report_unchanged=False)
    return ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
