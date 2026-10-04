"""Core · Engine 헤더 내용의 지문을 C++ 헤더로 씁니다 — 핫 리로드의 엔진 ABI 도장.

핫 리로드는 모듈 DLL/SO 만 갈아 끼우고 Engine 은 그대로 둡니다. 그런데 Engine 헤더를 고친 빌드에서 `Engine.dll` 은 실행 중이라
다시 링크되지 못해도(잠김) 모듈은 **새 헤더로** 빌드되어 써질 수 있습니다. 그 모듈을 올리면 돌고 있는 엔진과 구조체 배치 · vtable 이
어긋나 조용히 망가집니다. 그래서 Engine 과 모듈이 **같은 헤더 지문**을 바이너리에 박고, 핫 리로드는 올리기 전에 둘을 대조합니다.

지문은 헤더 내용 전부(주석 포함)입니다. 주석만 바꿔도 도장이 바뀌는 것은 일부러입니다 — 헤더가 바뀐 빌드는 Engine 도 다시 링크해야
맞으므로, 그 빌드의 모듈은 재시작 전까지 받지 않는 쪽이 안전합니다. 내용이 같으면 파일을 다시 쓰지 않아(재빌드를 부르지 않는다)
Ninja 의 restat 이 그 뒤를 멈춥니다.

사용법: py -3 Scripts/generate/GenerateEngineAbiStamp.py --root <repo> --out <path/EngineAbiStamp.gen.h>
"""
import argparse
import hashlib
import os
import sys

# 핫 리로드가 갈아 끼우지 않는 쪽의 헤더다 — Core · Engine, 모듈과 호스트의 계약(RuntimeAPI), 공용 모듈(GameFramework — 섀도 복사 없이 한 번
# 올리고 다시 올리지 않는다). 키트(GameFramework/Kits)는 스스로 리로드되는 모듈이라 뺀다. X 매크로 표(.xxx)도 넣는다 — `EngineServiceList.xxx` 가
# 서비스 색인과 `EngineServices` 배치를, `PredefinedNameType.xxx` 가 예약 이름 색인을 정한다. 주의: 표를 빼면 거기에 줄을 더한 모듈이
# 같은 도장으로 옛 엔진에 올라 `getService<T>()` 가 엉뚱한 서비스를 돌려준다.
kHeaderRoots = ("Source/Core", "Source/Engine", "Source/RuntimeAPI", "Source/GameFramework")
kExcludedRoots = ("Source/GameFramework/Kits",)
kHeaderExtensions = (".h", ".hpp", ".inl", ".xxx")
kStampMarker = "swEngineAbiStamp:"


def collectHeaderPathsInternal(repositoryRoot):
    """해시할 헤더를 저장소 기준 경로 순으로 모읍니다. 순서가 도장에 들어가므로 정렬합니다."""
    listPath = []
    for headerRoot in kHeaderRoots:
        absoluteRoot = os.path.join(repositoryRoot, headerRoot)
        for dirPath, _, listFileName in os.walk(absoluteRoot):
            for fileName in listFileName:
                if not fileName.endswith(kHeaderExtensions):
                    continue
                relativePath = os.path.relpath(os.path.join(dirPath, fileName), repositoryRoot).replace("\\", "/")
                if relativePath.startswith(tuple(excludedRoot + "/" for excludedRoot in kExcludedRoots)):
                    continue
                listPath.append(relativePath)
    listPath.sort()
    return listPath


def computeStampInternal(repositoryRoot):
    """헤더 경로와 내용(줄 끝은 LF 로 맞춤)을 차례로 넣은 SHA-1 입니다. 체크아웃마다 다른 CRLF 가 도장을 바꾸지 않게 합니다."""
    digest = hashlib.sha1()
    for relativePath in collectHeaderPathsInternal(repositoryRoot):
        with open(os.path.join(repositoryRoot, relativePath), "rb") as handle:
            content = handle.read().replace(b"\r\n", b"\n")
        digest.update(relativePath.encode("utf-8"))
        digest.update(b"\0")
        digest.update(content)
        digest.update(b"\0")
    return digest.hexdigest()


def makeHeaderTextInternal(stamp):
    return (
        "// 생성 파일 - Scripts/generate/GenerateEngineAbiStamp.py. 고치지 마십시오.\n"
        "// Core · Engine 헤더 내용의 지문입니다. Engine 과 모듈이 같은 값을 박고, 핫 리로드가 올리기 전에 대조합니다.\n"
        "#pragma once\n"
        "#define SW_ENGINE_ABI_STAMP \"" + kStampMarker + stamp + "\"\n"
    )


def main():
    parser = argparse.ArgumentParser(description="Core · Engine 헤더 지문(핫 리로드 ABI 도장)을 씁니다.")
    parser.add_argument("--root", required=True, help="저장소 루트")
    parser.add_argument("--out", required=True, help="쓸 헤더 경로")
    args = parser.parse_args()

    text = makeHeaderTextInternal(computeStampInternal(os.path.abspath(args.root)))
    if os.path.isfile(args.out):
        with open(args.out, "r", encoding="utf-8") as handle:
            if handle.read() == text:
                return 0
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
