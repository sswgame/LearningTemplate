"""
약어를 대문자로 — 식별자 안의 `Gpu` · `Json` · `Id` 같은 혼합형을 `GPU` · `JSON` · `ID` 로 바꾼다(docs/agents/NamingCpp.md 의 Acronyms).

  py -3 -m Scripts acronym-case --report        # 바꿀 낱말 표만 찍는다
  py -3 -m Scripts acronym-case                 # 코드 · 셰이더 · 데이터(xml · json · po)에 같은 표를 적용한다

규칙: 약어가 식별자의 **첫 낱말이 아니고**(첫 낱말은 소문자 그대로 — `gpuTime`) 대문자로 시작하는 낱말일 때만 바꾼다.
복수형 `Ids` → `IDs`. 바깥 라이브러리 이름(`kExternal` 접두어 · 낱말)은 건드리지 않는다 — 빌드가 바깥 이름을 찾으면 여기에 더한다.
같은 표를 데이터에도 적용하는 까닭: 리플렉션이 멤버 이름(`_meshId`)을 XML 속성 이름으로 쓴다. 코드만 바꾸면 데이터를 못 읽는다(폴백 없음).
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import Counter
from pathlib import Path

from common.ExitCode import ExitCode
from common.Paths import getProjectRoot
from dev.MoveSource import formatChanged, listTrackedText, readText, writeText

# 약어 목록 — 새 약어는 여기에 더하고 이 명령을 다시 돌린다(NamingCpp: "A new acronym is added to the acronym list and rewritten everywhere in one commit").
kAcronym = """
GPU CPU RHI UI ID UUID GUID JSON XML HTML HTTP HTTPS URL URI UTF TCP UDP TLS SSL SQL RPC API ABI DLL PDB AI NPC HUD FPS LOD VAT BVH AABB OBB
HDR LDR IBL SSAO SSR TAA FXAA MSAA CSM PBR BRDF VFX SFX DSP FFT RNG CRC SHA AES HMAC JWT MMO RPG JRPG SRPG RTS GM VR XR OS IO VFS PSO SRV UAV
CBV RTV DSV SDF MSDF TTF OTF PNG DDS KTX DXC DXIL SPIRV GLSL HLSL EGL WGL GLX FSR TLAS BLAS GI ECS FSM EQS CSV INI TOML YAML ZIP LZ4 OGG WAV
MIDI ASCII ICU RTL LTR IME DPI HID USB CLI GUI POD SIMD SSE AVX IP DNS NAT STUN MTU TTL ALPN QUIC NUMA PCH LTO CRT STL PRNG FNV RGB RGBA HSV
SRGB YUV FOV DOF VSM PCF IK FK CCD FABRIK LRU MRU TTS NDA EULA IAP DLC PVP PVE KDA MVP AOE HP MP XP
""".split()
kMixed = {acronym.capitalize(): acronym for acronym in kAcronym}

# 바깥 이름: 접두어로 시작하는 식별자 전체를 건너뛴다(라이브러리 API · 타입).
kExternalPrefix = ("b2", "Vk", "vk", "PFN_", "VK_", "gl", "GL_", "ImGui", "ImPlot", "ImVec", "ImFont", "ImDraw", "ImU32", "ImTexture", "JPH", "dt", "rc",
                   "du", "hb_", "FT_", "xcb", "wl_", "Xr", "XR_", "stbi", "Tracy", "___tracy", "nlohmann", "ZSTD", "LZ4_", "IDxc", "Dxc", "CLSID_",
                   "WinHttp", "IDXGI", "ID3D", "D3D", "DXGI", "spv", "SSL_", "EVP_", "nghttp2", "pugi", "IXAudio", "XAudio")
# Win32 · POSIX 의 PascalCase 함수 이름(우리 타입 이름과 모양이 같아 접두어로 가를 수 없다).
kExternalName = {"GetCurrentProcessId", "GetCurrentThreadId", "GetProcessId", "GetThreadId", "ProcessIdToSessionId", "GetDpiForWindow", "GetDpiForSystem",
                 "SetProcessDpiAwarenessContext", "GetDpiForMonitor", "SetThreadDpiAwarenessContext", "AdjustWindowRectExForDpi", "GetSystemMetricsForDpi",
                 "EnableNonClientDpiScaling", "GetAwarenessFromDpiAwarenessContext", "GetGuiResources", "GetUserDefaultUILanguage", "GetThreadUILanguage",
                 "SetThreadUILanguage", "GetUserDefaultLocaleName", "IsWow64Process", "OpenProcess", "CoCreateGuid", "UuidCreate", "StringFromGUID2"}

# 식별자 가운데 박힌 바깥 제품 이름 — 낱말로 쪼개지 않는다(`beginImGuiFrame` 이 `beginImGUIFrame` 이 되지 않게).
kProtected = re.compile(r"ImGuizmo|ImGui|ImPlot|ImVec\d|ImFont|ImDraw|ImTexture|ImU32|WinHttp|SheenBidi|HarfBuzz|FreeType|OpenGL|OpenSSL|XAudio\d?|DirectX|Box2D")

# 바깥 헤더(Windows SDK · MSVC CRT · vcpkg · ThirdParty · LLVM)에 있는 이름 가운데 이 규칙이 바꾸는 것 — 그대로 둔다.
# 바깥 라이브러리를 더하거나 올리면 다시 뽑는다(파일 머리말의 수집 방법). 접두어 · 낱말 표가 놓친 것(CreateIoCompletionPort, _CrtMemState …)을 막는다.
kExternalListPath = Path(__file__).with_name("AcronymExternalNames.txt")
kExternalName |= set(kExternalListPath.read_text(encoding="utf-8").split()) if kExternalListPath.is_file() else set()

kIdentifier = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\b")
kWord = re.compile(r"[A-Z][a-z]+s?(?=[A-Z0-9_]|$)")
kCodeSuffix = {".h", ".hpp", ".inl", ".cpp", ".hlsl", ".hlsli", ".xxx", ".natvis", ".tpl", ".cmake", ".txt", ".in"}
kDataSuffix = {".xml", ".json", ".po"}


def convertIdentifier(identifier: str) -> str:
    """첫 낱말이 아닌, 대문자로 시작하는 약어 혼합형만 대문자로."""
    if identifier.startswith(kExternalPrefix) or identifier in kExternalName or identifier.isupper():
        return identifier

    def replace(match: re.Match[str]) -> str:
        word = match.group(0)
        plural = word.endswith("s") and word[:-1] in kMixed
        base = word[:-1] if plural else word
        if base not in kMixed:
            return word
        # 식별자의 첫 낱말(접두어 `_` · `k` · `s_` · `p` · `b` 등 뒤가 아닌 맨 앞)은 PascalCase 타입이면 대문자로, 아니면 그대로 둔다
        return kMixed[base] + ("s" if plural else "")

    # 맨 앞 낱말: 타입(대문자 시작)이면 바꾸고, 함수 · 변수(소문자 시작)는 kWord 가 대문자 시작만 잡으므로 자연히 건너뛴다.
    # 보호 이름은 조각 사이에 두고 조각마다 바꾼다 — 조각 끝을 식별자 끝으로 보아야 `Imgui` 앞 낱말이 경계를 얻는다.
    listPart = kProtected.split(identifier)
    listKeep = kProtected.findall(identifier)
    result = kWord.sub(replace, listPart[0])
    for keep, part in zip(listKeep, listPart[1:]):
        result += keep + kWord.sub(replace, part)
    return result


def buildMap(listFile: list[Path]) -> Counter[tuple[str, str]]:
    mapCount: Counter[tuple[str, str]] = Counter()
    for path in listFile:
        for identifier in kIdentifier.findall(readText(path)):
            if identifier.startswith("gv_"):
                continue  # `-gv_*` 명령줄 · 설정 파일 이름 — 철자를 지킨다(NamingCpp)
            converted = convertIdentifier(identifier)
            if converted != identifier:
                mapCount[(identifier, converted)] += 1
    return mapCount


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts acronym-case", description="약어를 대문자로")
    parser.add_argument("--report", action="store_true", help="바꿀 표만 찍는다")
    args = parser.parse_args(argv)

    root = getProjectRoot()
    listAll = listTrackedText(root)
    def isCode(path: Path) -> bool:
        relative = path.relative_to(root).as_posix()
        if path.suffix.lower() in {".hlsl", ".hlsli"}:
            return True
        return path.suffix.lower() in kCodeSuffix and not relative.startswith("Content/")

    listAll += [path for path in (root / "Tools/ReflectionParser/Templates").glob("*.tpl")]
    listCode = [path for path in listAll if isCode(path)]
    listData = [path for path in listAll if path.suffix.lower() in kDataSuffix]
    mapCount = buildMap(listCode)
    mapRename = {old: new for (old, new) in mapCount}

    existing = set()
    for path in listCode:
        existing.update(kIdentifier.findall(readText(path)))
    listClash = sorted(old for old, new in mapRename.items() if new in existing)
    if args.report:
        words = Counter()
        for (old, new), count in mapCount.items():
            for word in kWord.findall(old):
                base = word[:-1] if word.endswith("s") and word[:-1] in kMixed else word
                if base in kMixed:
                    words[base] += count
        print(f"[acronym-case] 식별자 {len(mapRename)} 개 · 낱말별: " + ", ".join(f"{w}:{c}" for w, c in words.most_common()))
        print(f"[acronym-case] 새 이름이 이미 있는 것 {len(listClash)} 개: " + ", ".join(listClash[:40]))
        return ExitCode.Passed

    pattern = re.compile(r"\b(" + "|".join(re.escape(old) for old in sorted(mapRename, key=len, reverse=True)) + r")\b")
    listChanged: list[Path] = []
    for path in listCode + listData:
        text = readText(path)
        rewritten = pattern.sub(lambda match: mapRename[match.group(0)], text)
        if rewritten != text:
            writeText(path, rewritten)
            listChanged.append(path)
    renamedCount = renamePaths(root, [root / "Source", root / "Tools/ReflectionParser", root / "Tools/OnlineLoadBot"])
    print(f"[acronym-case] 식별자 {len(mapRename)} 개, 파일 {len(listChanged)} 개를 고치고 경로 {renamedCount} 개 이름을 바꿨다(겹친 이름 {len(listClash)} 개 — 빌드로 확인)")
    listChanged = [path if path.exists() else path for path in listChanged]
    return ExitCode.Passed


def convertPathPart(part: str) -> str:
    """`VfxEffectDef.h` · `GF_Rts.module.json` — 점으로 나눈 조각마다 바꾼다(확장자는 소문자 그대로)."""
    return ".".join(convertIdentifier(piece) if index == 0 or piece[:1].isupper() else piece for index, piece in enumerate(part.split(".")))


def renamePaths(root: Path, listRoot: list[Path]) -> int:
    """파일 · 폴더 이름의 약어를 대문자로. 대소문자만 다른 이름은 Windows 가 같은 이름으로 보므로 임시 이름을 거친다. 깊은 것부터."""
    import os

    count = 0
    for base in listRoot:
        for path in sorted(base.rglob("*"), key=lambda item: -len(item.parts)):
            newName = convertPathPart(path.name)
            if newName == path.name:
                continue
            temporary = path.with_name(path.name + ".acronymtmp")
            os.rename(path, temporary)
            os.rename(temporary, path.with_name(newName))
            count += 1
    return count
