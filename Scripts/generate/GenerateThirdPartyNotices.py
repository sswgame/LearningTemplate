"""서드파티 라이선스 고지(`THIRD_PARTY_NOTICES.txt`)를 vcpkg 설치 트리의 `share/<포트>/copyright` 에서 모아 씁니다.

배포물(Bin · Shipping 패키지)에 함께 놓는 고지 파일입니다. 대상은 **이 매니페스트(`vcpkg.json`)가 끌어오는 포트 전부**입니다 —
설치 트리는 워크트리끼리 나눠 쓰므로 트리에 깔린 모든 포트가 아니라, 매니페스트 의존(플랫폼 식을 이 트리플릿으로 푼 것)에서
`vcpkg/status` 의 `Depends` 를 따라 닫은 집합만 넣습니다. 포트를 짓는 데만 쓰는 도우미(`vcpkg-*`)는 배포물에 들어가지 않아 뺍니다.
개발 빌드에만 들어가는 라이브러리(ImGui · DXC · Tracy 등)도 넣습니다 — 개발 빌드를 남에게 줄 때도 같은 고지가 필요하다.

내용이 같으면 파일을 다시 쓰지 않습니다(빌드가 다시 돌지 않게).

사용법: py -3 Scripts/generate/GenerateThirdPartyNotices.py --manifest vcpkg.json --installed build/vcpkg_installed
        --triplet x64-windows --out build/Ninja-Debug/Bin/THIRD_PARTY_NOTICES.txt
"""
import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))   # Scripts — common

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)

#: 포트를 짓는 데만 쓰는 vcpkg 도우미 포트의 이름 접두입니다 — 산출물에 들어가지 않는다.
kBuildHelperPrefix = "vcpkg-"
#: 고지 머리에 넣는 개발 빌드 안내입니다. Tracy 는 Shipping 에 들어가지 않지만 개발 빌드(TracyClient.dll)를 줄 때 고지가 필요하다.
kListPrefaceLine = (
    "Third-party software notices",
    "",
    "This program uses the third-party libraries listed below. Each section is the license text that the library ships with",
    "(collected from the vcpkg installed tree). Libraries used only by development builds (editor, profiler, shader compiler)",
    "are listed as well.",
    "",
    "Tracy Profiler (https://github.com/wolfpld/tracy) is linked only into development builds (TracyClient). It is licensed",
    "under the 3-clause BSD license - see the 'tracy' section below.",
)
kSectionRule = "=" * 100

#: 플랫폼 식의 낱말 → 트리플릿에서 참인지. vcpkg 의 플랫폼 식(`windows & !uwp`, `!linux`, `windows | linux`)에 쓰이는 것만.
kMapTripletWordTest = {
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


def evaluatePlatformInternal(expression, triplet):
    """vcpkg 플랫폼 식(`!` · `&` · `|` · `,` · 괄호)을 @p triplet 으로 풉니다. 모르는 낱말은 거짓입니다."""
    listToken = re.findall(r"[A-Za-z0-9_-]+|[!&|,()]", expression)
    position = [0]

    def peekInternal():
        return listToken[position[0]] if position[0] < len(listToken) else ""

    def takeInternal():
        token = peekInternal()
        position[0] += 1
        return token

    def parsePrimaryInternal():
        token = takeInternal()
        if token == "!":
            return not parsePrimaryInternal()
        if token == "(":
            value = parseOrInternal()
            takeInternal()   # ")"
            return value
        test = kMapTripletWordTest.get(token)
        return test(triplet) if test is not None else False

    def parseAndInternal():
        value = parsePrimaryInternal()
        while peekInternal() == "&":
            takeInternal()
            value = parsePrimaryInternal() and value
        return value

    def parseOrInternal():
        value = parseAndInternal()
        while peekInternal() in ("|", ","):
            takeInternal()
            value = parseAndInternal() or value
        return value

    return parseOrInternal() if listToken else True


def readManifestPortsInternal(manifestPath, triplet):
    """매니페스트의 직접 의존 중 이 트리플릿에 해당하는 포트 이름입니다."""
    with open(manifestPath, "r", encoding="utf-8") as handle:
        manifest = json.load(handle)
    setPort = set()
    for dependency in manifest.get("dependencies", []):
        if isinstance(dependency, str):
            setPort.add(dependency)
        elif evaluatePlatformInternal(dependency.get("platform", ""), triplet):
            setPort.add(dependency["name"])
    return setPort


def readStatusDependsInternal(statusPath, triplet):
    """`vcpkg/status` 에서 이 트리플릿에 깔린 포트마다 의존 이름(핵심 + 깔린 기능 전부)을 모읍니다."""
    mapDepends = {}
    with open(statusPath, "r", encoding="utf-8") as handle:
        listParagraph = handle.read().replace("\r\n", "\n").split("\n\n")
    for paragraph in listParagraph:
        mapField = {}
        for line in paragraph.split("\n"):
            if ": " in line and line[0] != " ":
                key, value = line.split(": ", 1)
                mapField[key] = value
        if mapField.get("Architecture") != triplet or mapField.get("Status", "").endswith("installed") is False:
            continue
        if "not-installed" in mapField.get("Status", ""):
            continue
        listDepends = mapDepends.setdefault(mapField.get("Package", ""), [])
        for item in mapField.get("Depends", "").split(","):
            name = re.split(r"[\[:\s(]", item.strip(), maxsplit=1)[0]
            if name:
                listDepends.append(name)
    return mapDepends


def collectPortsInternal(setRoot, mapDepends):
    """직접 의존에서 시작해 깔린 의존을 따라 닫은 포트 이름을 정렬해 돌려줍니다."""
    setVisited = set()
    listPending = sorted(setRoot)
    while listPending:
        name = listPending.pop()
        if name in setVisited or name not in mapDepends:
            continue
        setVisited.add(name)
        listPending.extend(mapDepends[name])
    return sorted(name for name in setVisited if name.startswith(kBuildHelperPrefix) is False)


def makeNoticeTextInternal(listPort, shareRoot):
    """포트마다 절 하나(이름 + copyright 전문)를 이어 붙입니다. copyright 가 없는 포트는 그 사실을 적습니다."""
    listLine = list(kListPrefaceLine)
    listLine += ["", "Included: " + ", ".join(listPort), ""]
    for port in listPort:
        listLine += [kSectionRule, port, kSectionRule, ""]
        copyrightPath = os.path.join(shareRoot, port, "copyright")
        if os.path.isfile(copyrightPath):
            with open(copyrightPath, "r", encoding="utf-8", errors="replace") as handle:
                listLine.append(handle.read().replace("\r\n", "\n").rstrip("\n"))
        else:
            listLine.append("(no license file was installed for this port)")
        listLine.append("")
    return "\n".join(listLine) + "\n"


def main():
    parser = argparse.ArgumentParser(description="vcpkg 설치 트리의 copyright 를 모아 서드파티 고지 파일을 씁니다.")
    parser.add_argument("--manifest", required=True, help="vcpkg.json 경로")
    parser.add_argument("--installed", required=True, help="vcpkg 설치 루트(VCPKG_INSTALLED_DIR)")
    parser.add_argument("--triplet", required=True, help="대상 트리플릿(VCPKG_TARGET_TRIPLET)")
    parser.add_argument("--out", required=True, help="쓸 고지 파일 경로")
    args = parser.parse_args()

    statusPath = os.path.join(args.installed, "vcpkg", "status")
    if os.path.isfile(statusPath) is False:
        print(f"[ThirdPartyNotices] no vcpkg status file: {statusPath}", file=sys.stderr)
        return 1
    mapDepends = readStatusDependsInternal(statusPath, args.triplet)
    listPort = collectPortsInternal(readManifestPortsInternal(args.manifest, args.triplet), mapDepends)
    text = makeNoticeTextInternal(listPort, os.path.join(args.installed, args.triplet, "share"))

    if os.path.isfile(args.out):
        with open(args.out, "r", encoding="utf-8") as handle:
            if handle.read() == text:
                return 0
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)
    print(f"[ThirdPartyNotices] {len(listPort)} libraries -> {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
