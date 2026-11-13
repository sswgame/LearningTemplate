"""서드파티 라이선스 고지(`THIRD_PARTY_NOTICES.txt`)를 vcpkg 설치 트리의 `share/<포트>/copyright` 에서 모아 씁니다.

배포물(Bin · Shipping 패키지)에 함께 놓는 고지 파일입니다. 대상은 **이 매니페스트(`vcpkg.json`)가 끌어오는 포트 전부**입니다 —
설치 트리는 워크트리끼리 나눠 쓰므로 트리에 깔린 모든 포트가 아니라, 매니페스트 의존(플랫폼 식을 이 트리플릿으로 푼 것)에서
`vcpkg/status` 의 `Depends` 를 따라 닫은 집합만 넣습니다. 포트를 짓는 데만 쓰는 도우미(`vcpkg-*`)는 배포물에 들어가지 않아 뺍니다.
개발 빌드에만 들어가는 라이브러리(ImGui · DXC · Tracy 등)도 넣습니다 — 개발 빌드를 남에게 줄 때도 같은 고지가 필요하다.
저장소에 원문 그대로 둔 코드(`ThirdParty/<이름>/LICENSE.md` 가 있는 폴더 — RenderDoc in-app API 헤더)는 `--vendored-root` 로 받아 뒤에 붙입니다.

내용이 같으면 파일을 다시 쓰지 않습니다(빌드가 다시 돌지 않게).

사용법: py -3 Scripts/generate/third_party_notices.py --manifest vcpkg.json --installed build/vcpkg_installed
        --triplet x64-windows --out build/Ninja-Debug/Bin/THIRD_PARTY_NOTICES.txt [--vendored-root ThirdParty]
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts — common

from generate.generator_output import write_generated_file  # noqa: E402
from common.exit_code import ExitCode  # noqa: E402

#: 포트를 짓는 데만 쓰는 vcpkg 도우미 포트의 이름 접두입니다 — 산출물에 들어가지 않는다.
BUILD_HELPER_PREFIX = "vcpkg-"
#: 고지 머리에 넣는 개발 빌드 안내입니다. Tracy 는 Shipping 에 들어가지 않지만 개발 빌드(TracyClient.dll)를 줄 때 고지가 필요하다.
LIST_PREFACE_LINE = (
    "Third-party software notices",
    "",
    "This program uses the third-party libraries listed below. Each section is the license text that the library ships with",
    "(collected from the vcpkg installed tree). Libraries used only by development builds (editor, profiler, shader compiler)",
    "are listed as well.",
    "",
    "Tracy Profiler (https://github.com/wolfpld/tracy) is linked only into development builds (TracyClient). It is licensed",
    "under the 3-clause BSD license - see the 'tracy' section below.",
)
SECTION_RULE = "=" * 100
#: 저장소에 원문 그대로 둔 코드의 라이선스 파일 이름입니다(`ThirdParty/<이름>/LICENSE.md`).
VENDORED_LICENSE_FILE_NAME = "LICENSE.md"

#: 플랫폼 식의 낱말 → 트리플릿에서 참인지. vcpkg 의 플랫폼 식(`windows & !uwp`, `!linux`, `windows | linux`)에 쓰이는 것만.
MAP_TRIPLET_WORD_TEST = {
    "windows": lambda triplet: "windows" in triplet,
    "linux": lambda triplet: "linux" in triplet,
    "osx": lambda triplet: "osx" in triplet,
    "uwp": lambda triplet: "uwp" in triplet,
    "android": lambda triplet: "android" in triplet,
    "x64": lambda triplet: triplet.startswith("x64"),
    "x86": lambda triplet: triplet.startswith("x86"),
    "arm64": lambda triplet: triplet.startswith("arm64"),
    "static": lambda triplet: "static" in triplet,
    "native": lambda triplet: False,
}


def _evaluate_platform(expression: str, triplet: str) -> bool:
    """vcpkg 플랫폼 식(`!` · `&` · `|` · `,` · 괄호)을 @p triplet 으로 풉니다. 모르는 낱말은 거짓입니다."""
    list_token = re.findall(r"[A-Za-z0-9_-]+|[!&|,()]", expression)
    position = [0]

    def _peek() -> str:
        return list_token[position[0]] if position[0] < len(list_token) else ""

    def _take() -> str:
        token = _peek()
        position[0] += 1
        return token

    def _parse_primary() -> bool:
        token = _take()
        if token == "!":
            return not _parse_primary()
        if token == "(":
            value = _parse_or()
            _take()   # ")"
            return value
        test = MAP_TRIPLET_WORD_TEST.get(token)
        return test(triplet) if test is not None else False

    def _parse_and() -> bool:
        value = _parse_primary()
        while _peek() == "&":
            _take()
            value = _parse_primary() and value
        return value

    def _parse_or() -> bool:
        value = _parse_and()
        while _peek() in ("|", ","):
            _take()
            value = _parse_and() or value
        return value

    return _parse_or() if list_token else True


def _read_manifest_ports(manifest_path: str, triplet: str) -> set[str]:
    """매니페스트의 직접 의존 중 이 트리플릿에 해당하는 포트 이름입니다."""
    with open(manifest_path, "r", encoding="utf-8") as handle:
        manifest = json.load(handle)
    set_port: set[str] = set()
    for dependency in manifest["dependencies"]:
        if isinstance(dependency, str):
            set_port.add(dependency)
        elif _evaluate_platform(dependency.get("platform") or "", triplet):   # platform 은 vcpkg 형식의 선택 칸(없으면 모든 플랫폼)
            set_port.add(dependency["name"])
    return set_port


def _read_status_depends(status_path: Path, triplet: str) -> dict[str, list[str]]:
    """`vcpkg/status` 에서 이 트리플릿에 깔린 포트마다 의존 이름(핵심 + 깔린 기능 전부)을 모읍니다."""
    map_depends: dict[str, list[str]] = {}
    with open(status_path, "r", encoding="utf-8") as handle:
        list_paragraph = handle.read().replace("\r\n", "\n").split("\n\n")
    for paragraph in list_paragraph:
        map_field: dict[str, str] = {}
        for line in paragraph.split("\n"):
            if ": " in line and line[0] != " ":
                key, value = line.split(": ", 1)
                map_field[key] = value
        if map_field.get("Architecture") != triplet or map_field["Status"].endswith("installed") is False:
            continue
        if "not-installed" in map_field["Status"]:
            continue
        list_depends = map_depends.setdefault(map_field["Package"], [])
        # 의존이 없는 포트에는 Depends 줄이 없다.
        for item in (map_field["Depends"].split(",") if "Depends" in map_field else []):
            name = re.split(r"[\[:\s(]", item.strip(), maxsplit=1)[0]
            if name:
                list_depends.append(name)
    return map_depends


def _collect_ports(set_root: set[str], map_depends: dict[str, list[str]]) -> list[str]:
    """직접 의존에서 시작해 깔린 의존을 따라 닫은 포트 이름을 정렬해 돌려줍니다."""
    set_visited: set[str] = set()
    list_pending = sorted(set_root)
    while list_pending:
        name = list_pending.pop()
        if name in set_visited or name not in map_depends:
            continue
        set_visited.add(name)
        list_pending.extend(map_depends[name])
    return sorted(name for name in set_visited if name.startswith(BUILD_HELPER_PREFIX) is False)


def _collect_vendored_licenses(vendored_root: str | None) -> list[tuple[str, Path]]:
    """`<vendored_root>/<이름>/LICENSE.md` 가 있는 폴더마다 (이름, 라이선스 경로)입니다. 이름 순서입니다."""
    if vendored_root is None or Path(vendored_root).is_dir() is False:
        return []
    list_vendored: list[tuple[str, Path]] = []
    for name in sorted(path.name for path in Path(vendored_root).iterdir()):
        license_path = Path(vendored_root) / name / VENDORED_LICENSE_FILE_NAME
        if license_path.is_file():
            list_vendored.append((name, license_path))
    return list_vendored


def _make_notice_text(list_port: list[str], share_root: Path, list_vendored: Sequence[tuple[str, Path]] = ()) -> str:
    """포트마다 절 하나(이름 + copyright 전문)를 이어 붙이고, 저장소에 둔 코드의 라이선스를 뒤에 붙입니다. copyright 가 없는 포트는 그 사실을 적습니다."""
    list_line = list(LIST_PREFACE_LINE)
    list_line += ["", "Included: " + ", ".join(list(list_port) + [name for name, _ in list_vendored]), ""]
    for port in list_port:
        list_line += [SECTION_RULE, port, SECTION_RULE, ""]
        copyright_path = share_root / port / "copyright"
        if copyright_path.is_file():
            with open(copyright_path, "r", encoding="utf-8", errors="replace") as handle:
                list_line.append(handle.read().replace("\r\n", "\n").rstrip("\n"))
        else:
            list_line.append("(no license file was installed for this port)")
        list_line.append("")
    for name, license_path in list_vendored:
        list_line += [SECTION_RULE, name + " (source kept in ThirdParty/" + name + ")", SECTION_RULE, ""]
        with open(license_path, "r", encoding="utf-8", errors="replace") as handle:
            list_line.append(handle.read().replace("\r\n", "\n").rstrip("\n"))
        list_line.append("")
    return "\n".join(list_line) + "\n"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="vcpkg 설치 트리의 copyright 를 모아 서드파티 고지 파일을 씁니다.")
    parser.add_argument("--manifest", required=True, help="vcpkg.json 경로")
    parser.add_argument("--installed", required=True, help="vcpkg 설치 루트(VCPKG_INSTALLED_DIR)")
    parser.add_argument("--triplet", required=True, help="대상 트리플릿(VCPKG_TARGET_TRIPLET)")
    parser.add_argument("--out", required=True, help="쓸 고지 파일 경로")
    parser.add_argument("--vendored-root", default=None, help="저장소에 둔 서드파티 코드 폴더(하위 폴더의 LICENSE.md 를 붙인다)")
    args = parser.parse_args(argv)

    status_path = Path(args.installed) / "vcpkg" / "status"
    if status_path.is_file() is False:
        print(f"[ThirdPartyNotices] no vcpkg status file: {status_path}", file=sys.stderr)
        return ExitCode.VIOLATION
    map_depends = _read_status_depends(status_path, args.triplet)
    list_port = _collect_ports(_read_manifest_ports(args.manifest, args.triplet), map_depends)
    list_vendored = _collect_vendored_licenses(args.vendored_root)
    text = _make_notice_text(list_port, Path(args.installed) / args.triplet / "share", list_vendored)

    write_generated_file(Path(args.out), text, tag="ThirdPartyNotices", summary=f"{len(list_port) + len(list_vendored)} libraries", newline="\n",
                       report_unchanged=False)
    return ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
