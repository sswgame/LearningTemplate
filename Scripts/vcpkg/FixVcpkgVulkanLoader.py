#!/usr/bin/env python3
r"""
Scripts/vcpkg/FixVcpkgVulkanLoader.py

vcpkg libvulkan.so* 를 시스템 Vulkan loader로 심볼릭 링크합니다.

  python3 Scripts/vcpkg/FixVcpkgVulkanLoader.py --vcpkg-installed-dir <dir> --triplet x64-linux
"""

from __future__ import annotations

import argparse
import ctypes
import ctypes.util
import os
import subprocess
import sys
from pathlib import Path
from typing import Iterable, List, Optional, Sequence

_SCRIPTS = Path(__file__).resolve().parents[1]
if str(_SCRIPTS) not in sys.path:
    sys.path.insert(0, str(_SCRIPTS))

from ConfigHelper import EnsureScriptsOnPath, GetProjectRoot, LoadSearchPaths

EnsureScriptsOnPath()

_WSI_EXTENSIONS = (
    "VK_KHR_xcb_surface",
    "VK_KHR_xlib_surface",
)


def _IsVcpkgPath(path: Path) -> bool:
    return "vcpkg_installed" in path.as_posix()


def _MappedLibraryPath(soname_substr: str) -> Optional[Path]:
    try:
        with open("/proc/self/maps", "r", encoding="utf-8", errors="replace") as maps:
            for line in maps:
                if soname_substr not in line:
                    continue
                parts = line.split()
                if len(parts) < 6:
                    continue
                mapped = Path(parts[-1])
                if mapped.exists() and not _IsVcpkgPath(mapped):
                    return mapped.resolve()
    except OSError:
        pass
    return None


def FindSystemVulkanLoader() -> Optional[Path]:
    try:
        result = subprocess.run(
            ["ldconfig", "-p"],
            check=False,
            capture_output=True,
            text=True,
        )
        if result.returncode == 0:
            for line in result.stdout.splitlines():
                if "libvulkan.so" not in line or "=>" not in line:
                    continue
                path = Path(line.split("=>", 1)[1].strip())
                if path.exists() and not _IsVcpkgPath(path):
                    return path.resolve()
    except FileNotFoundError:
        pass

    name = ctypes.util.find_library("vulkan")
    if name:
        candidate = Path(name)
        if candidate.is_absolute() and candidate.exists() and not _IsVcpkgPath(candidate):
            return candidate.resolve()
        try:
            ctypes.CDLL(name)
        except OSError:
            pass
        else:
            mapped = _MappedLibraryPath("libvulkan.so")
            if mapped is not None:
                return mapped
    return None


def CollectVcpkgLibDirs(
    installed_dir: Optional[Path],
    triplet: Optional[str],
) -> List[Path]:
    dirs: List[Path] = []
    seen = set()

    def add_installed_root(root: Path) -> None:
        if not root.is_dir():
            return
        for sub in ("lib", "debug/lib"):
            lib_dir = root / sub
            key = str(lib_dir.resolve()) if lib_dir.exists() else str(lib_dir)
            if key in seen:
                continue
            if lib_dir.is_dir():
                seen.add(key)
                dirs.append(lib_dir)

    env_installed = os.environ.get("VCPKG_INSTALLED_DIR")
    if env_installed and not installed_dir:
        installed_dir = Path(env_installed)

    if installed_dir and triplet:
        add_installed_root(Path(installed_dir) / triplet)
    elif installed_dir:
        installed = Path(installed_dir)
        if (installed / "lib").is_dir():
            add_installed_root(installed)
        elif triplet:
            add_installed_root(installed / triplet)
        else:
            for child in sorted(installed.iterdir()):
                if child.is_dir() and (child / "lib").is_dir():
                    add_installed_root(child)

    # Optional relative fallback from Config/search_paths.json (not a hardcoded absolute).
    if not dirs:
        rel = LoadSearchPaths().get("vcpkg_installed_rel", "")
        if rel:
            base = GetProjectRoot() / rel
            if base.is_dir():
                if triplet:
                    add_installed_root(base / triplet)
                else:
                    for child in sorted(base.iterdir()):
                        if child.is_dir() and (child / "lib").is_dir():
                            add_installed_root(child)

    return dirs


def ForceSymlink(target: Path, link_path: Path) -> None:
    if link_path.is_symlink() or link_path.exists():
        link_path.unlink()
    link_path.symlink_to(target)


def FixLibDir(lib_dir: Path, system_vulkan: Path) -> bool:
    so1 = lib_dir / "libvulkan.so.1"
    so = lib_dir / "libvulkan.so"
    ForceSymlink(system_vulkan, so1)
    ForceSymlink(Path("libvulkan.so.1"), so)
    print(f"[FixVcpkgVulkanLoader] {lib_dir} -> {system_vulkan}")
    return True


def VerifyWsi(loader_path: Path) -> bool:
    os.environ.setdefault("DISPLAY", ":0")

    class ExtensionProperties(ctypes.Structure):
        _fields_ = [
            ("extensionName", ctypes.c_char * 256),
            ("specVersion", ctypes.c_uint32),
        ]

    try:
        vk = ctypes.CDLL(str(loader_path))
    except OSError as exc:
        print(f"[FixVcpkgVulkanLoader] verify load failed: {exc}", file=sys.stderr)
        return False

    count = ctypes.c_uint32(0)
    vk.vkEnumerateInstanceExtensionProperties(None, ctypes.byref(count), None)
    props = (ExtensionProperties * count.value)()
    vk.vkEnumerateInstanceExtensionProperties(None, ctypes.byref(count), props)

    names = {p.extensionName.decode("utf-8", errors="replace") for p in props}
    print(f"[FixVcpkgVulkanLoader] instance extensions: {count.value}")
    ok = True
    for ext in _WSI_EXTENSIONS:
        present = ext in names
        print(f"[FixVcpkgVulkanLoader]   {ext}: {present}")
        ok = ok and present
    return ok


def ParseArgs(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Point vcpkg libvulkan.so* at the system Vulkan loader (Linux X11 WSI)."
    )
    parser.add_argument("--vcpkg-installed-dir", type=Path, default=None)
    parser.add_argument(
        "--triplet",
        default=os.environ.get("VCPKG_TARGET_TRIPLET", "x64-linux"),
    )
    parser.add_argument("--system-vulkan", type=Path, default=None)
    parser.add_argument("--verify", action="store_true", default=True)
    parser.add_argument("--no-verify", action="store_false", dest="verify")
    parser.add_argument("--verify-only", action="store_true")
    return parser.parse_args(argv)


def Main(argv: Optional[Sequence[str]] = None) -> int:
    if sys.platform != "linux":
        print("[FixVcpkgVulkanLoader] skip (not Linux)")
        return 0

    args = ParseArgs(argv)
    system_vulkan = (
        args.system_vulkan.resolve() if args.system_vulkan else FindSystemVulkanLoader()
    )
    if system_vulkan is None or not system_vulkan.exists():
        print(
            "[FixVcpkgVulkanLoader] system libvulkan not found "
            "(install: sudo apt install libvulkan1)",
            file=sys.stderr,
        )
        return 1

    lib_dirs = CollectVcpkgLibDirs(args.vcpkg_installed_dir, args.triplet)
    if not lib_dirs:
        print(
            "[FixVcpkgVulkanLoader] no vcpkg lib dirs found "
            f"(installed={args.vcpkg_installed_dir}, triplet={args.triplet})",
            file=sys.stderr,
        )
        return 1

    if not args.verify_only:
        for lib_dir in lib_dirs:
            FixLibDir(lib_dir, system_vulkan)

    if not args.verify and not args.verify_only:
        return 0

    verify_candidates: Iterable[Path] = (d / "libvulkan.so.1" for d in reversed(lib_dirs))
    loader: Optional[Path] = None
    for candidate in verify_candidates:
        if candidate.exists():
            loader = candidate
            break
    if loader is None:
        loader = system_vulkan

    if not VerifyWsi(loader):
        print(
            "[FixVcpkgVulkanLoader] WSI still missing — "
            "install mesa-vulkan-drivers / libxcb1-dev / libx11-xcb-dev",
            file=sys.stderr,
        )
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(Main())
