"""
Scripts/SetupEnvironment.py

[초심자를 위한 스크립트 역할 설명]
이 스크립트는 프로젝트 빌드 전, 개발자의 컴퓨터 환경(Windows / Linux / macOS)을 분석하여
빌드에 필요한 핵심 도구들(Clang/LLVM, Windows SDK, DXC Shader Compiler, MSVC, vcpkg, Ninja 등)의
위치를 자동으로 탐색하고 `Config/engine_config.json`과 `Config/parser_config.json`에 저장해주는 자동 환경 감지기입니다.

개발자가 일일이 환경 변수나 헤더 경로를 고정 경로로 적지 않아도,
이 스크립트 덕분에 어떤 컴퓨터에서든 `cmake --preset` 한 줄로 빌드 준비가 완료됩니다.
"""

import json
import os
import platform
import shutil
import subprocess
import sys
from functools import lru_cache
from pathlib import Path
from typing import Any, Dict, List, Tuple

from ConfigHelper import GetProjectRoot, NormalizePath

@lru_cache(maxsize=1)
def FindLlvmPath() -> str:
    """
    시스템 PATH 및 환경 변수로부터 LLVM/Clang 설치 경로를 동적으로 탐색합니다.
    (lru_cache를 사용하여 반복 호출 시에도 1회만 계산합니다)
    """
    for env_key in ("LLVM_DIR", "LLVM_HOME", "LLVM_ROOT", "LLVM_PATH"):
        env_llvm = os.environ.get(env_key)
        if env_llvm and os.path.exists(env_llvm):
            return NormalizePath(env_llvm)

    llvm_bin = shutil.which("clang-cl") or shutil.which("clang")
    if llvm_bin:
        parent = Path(llvm_bin).resolve().parent.parent
        if (parent / "include").exists() or (parent / "bin" / "clang-cl.exe").exists():
            return NormalizePath(str(parent))

    # 일반 Windows 설치 경로
    if platform.system() == "Windows":
        for root in (
            Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "LLVM",
            Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "LLVM",
            Path(r"C:\Program Files\LLVM"),
        ):
            if (root / "bin" / "clang-cl.exe").exists():
                return NormalizePath(str(root))

    return ""

@lru_cache(maxsize=1)
def FindWindowsSdkPath() -> Tuple[str, str]:
    """레지스트리 및 환경 변수를 통해 Windows SDK 경로와 버전을 동적 탐색합니다."""
    if platform.system() != "Windows":
        return "", ""

    env_sdk = os.environ.get("WindowsSdkDir")
    if env_sdk and os.path.exists(env_sdk):
        version = os.environ.get("WindowsSDKVersion", "").strip("\\")
        return NormalizePath(env_sdk), version

    try:
        import winreg
        key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Microsoft\Windows Kits\Installed Roots")
        kits_root, _ = winreg.QueryValueEx(key, "KitsRoot10")
        winreg.CloseKey(key)

        if kits_root and os.path.exists(kits_root):
            inc_dir = os.path.join(kits_root, "Include")
            if os.path.exists(inc_dir):
                vers = [d for d in os.listdir(inc_dir) if d.startswith("10.")]
                if vers:
                    vers.sort(reverse=True)
                    return NormalizePath(kits_root), vers[0]
    except Exception:
        pass

    return "", ""

@lru_cache(maxsize=1)
def FindMsvcPath() -> str:
    """vswhere 도구 및 환경 변수를 통해 MSVC 도구 경로를 동적 탐색합니다."""
    if platform.system() != "Windows":
        return ""

    env_vc = os.environ.get("VCToolsInstallDir")
    if env_vc and os.path.exists(env_vc):
        return NormalizePath(env_vc)

    pf = os.environ.get("ProgramFiles(x86)") or os.environ.get("ProgramFiles")
    if pf:
        vswhere = os.path.join(pf, "Microsoft Visual Studio", "Installer", "vswhere.exe")
        if os.path.exists(vswhere):
            try:
                cmd = [
                    vswhere,
                    "-latest",
                    "-products", "*",
                    "-requires", "Microsoft.VisualStudio.Component.VC.Tools",
                    "-property", "installationPath"
                ]
                vs_path = subprocess.check_output(cmd, text=True, encoding='utf-8', errors='replace').strip()
                if not vs_path:
                    cmd_fallback = [
                        vswhere,
                        "-latest",
                        "-products", "*",
                        "-property", "installationPath"
                    ]
                    vs_path = subprocess.check_output(cmd_fallback, text=True, encoding='utf-8', errors='replace').strip()

                if vs_path:
                    msvc_base = os.path.join(vs_path, "VC", "Tools", "MSVC")
                    if os.path.exists(msvc_base):
                        vers = [d for d in os.listdir(msvc_base) if os.path.isdir(os.path.join(msvc_base, d))]
                        if vers:
                            vers.sort(reverse=True)
                            return NormalizePath(os.path.join(msvc_base, vers[0]))
            except Exception:
                pass

    return ""

def FindLibClangDllPath(llvm_path: str) -> str:
    """libclang.dll 라이브러리 경로를 동적 탐색합니다."""
    sys_name = platform.system()
    if sys_name == "Windows":
        lib_name = "libclang.dll"
    elif sys_name == "Darwin":
        lib_name = "libclang.dylib"
    else:
        lib_name = "libclang.so"

    if llvm_path:
        for candidate in [Path(llvm_path) / "bin" / lib_name, Path(llvm_path) / "lib" / lib_name]:
            if candidate.exists():
                return NormalizePath(str(candidate))

    found = shutil.which(lib_name)
    if found:
        return NormalizePath(found)
    return ""

def _find_first_existing_file(search_dirs: List[Path], file_name: str) -> str:
    """주어진 디렉터리 목록에서 특정 파일을 순차 탐색하여 첫 번째 발견된 절대 경로를 반환합니다."""
    for d in search_dirs:
        cand = d / file_name
        if cand.exists():
            return str(cand)
    return ""

def FindDxcDlls(sdk_dir: str, sdk_ver: str, project_root: Path) -> Tuple[str, str]:
    """DirectX Shader Compiler (dxcompiler.dll, dxil.dll) 경로를 동적 탐색합니다.
    SPIR-V CodeGen을 지원하는 vcpkg 및 Vulkan SDK의 DXC를 최우선으로 탐색합니다.
    """
    sys_name = platform.system()
    dxc_name = "dxcompiler.dll" if sys_name == "Windows" else ("libdxcompiler.dylib" if sys_name == "Darwin" else "libdxcompiler.so")
    dxil_name = "dxil.dll" if sys_name == "Windows" else ("libdxil.dylib" if sys_name == "Darwin" else "libdxil.so")

    arch_str = platform.machine().lower()
    if arch_str in ["x86_64", "amd64"]: arch_prefix = "x64"
    elif arch_str in ["arm64", "aarch64"]: arch_prefix = "arm64"
    else: arch_prefix = "x64"

    if sys_name == "Windows": triplet = f"{arch_prefix}-windows"
    elif sys_name == "Darwin": triplet = f"{arch_prefix}-osx"
    else: triplet = f"{arch_prefix}-linux"

    dxc_dll = ""
    dxil_dll = ""

    vcpkg_dirs = [
        project_root / "build" / "bin" / "Ninja-Debug" / "vcpkg_installed" / triplet / "bin",
        project_root / "build" / "bin" / "Ninja-Debug" / "vcpkg_installed" / triplet / "tools" / "directx-dxc",
        project_root / "build" / "vcpkg_installed" / triplet / "bin",
        project_root / "build" / "vcpkg_installed" / triplet / "tools" / "directx-dxc",
        project_root / "vcpkg_installed" / triplet / "bin",
        project_root / "vcpkg_installed" / triplet / "tools" / "directx-dxc"
    ]
    dxc_dll = _find_first_existing_file(vcpkg_dirs, dxc_name)
    dxil_dll = _find_first_existing_file(vcpkg_dirs, dxil_name)

    if not dxc_dll and "VULKAN_SDK" in os.environ:
        vk_sdk_dirs = [Path(os.environ["VULKAN_SDK"]) / "Bin", Path(os.environ["VULKAN_SDK"]) / "lib"]
        dxc_dll = _find_first_existing_file(vk_sdk_dirs, dxc_name)
    if not dxil_dll and "VULKAN_SDK" in os.environ:
        vk_sdk_dirs = [Path(os.environ["VULKAN_SDK"]) / "Bin", Path(os.environ["VULKAN_SDK"]) / "lib"]
        dxil_dll = _find_first_existing_file(vk_sdk_dirs, dxil_name)

    if not dxc_dll and sdk_dir and sdk_ver and sys_name == "Windows":
        win_sdk_dirs = [Path(sdk_dir) / "bin" / sdk_ver / arch_prefix, Path(sdk_dir) / "bin" / arch_prefix]
        dxc_dll = _find_first_existing_file(win_sdk_dirs, dxc_name)

    if not dxil_dll and sdk_dir and sdk_ver and sys_name == "Windows":
        win_sdk_dirs = [Path(sdk_dir) / "bin" / sdk_ver / arch_prefix, Path(sdk_dir) / "bin" / arch_prefix]
        dxil_dll = _find_first_existing_file(win_sdk_dirs, dxil_name)

    if not dxc_dll:
        found = shutil.which(dxc_name)
        if found:
            dxc_dll = found

    if not dxil_dll:
        found = shutil.which(dxil_name)
        if found:
            dxil_dll = found

    return NormalizePath(dxc_dll), NormalizePath(dxil_dll)

def FindSystemIncludeDirs() -> List[str]:
    """플랫폼별 헤더 인클루드 경로를 수집합니다."""
    include_dirs: List[str] = []
    sys_name = platform.system()

    if sys_name == "Darwin":
        try:
            sdk_path = subprocess.check_output(["xcrun", "--show-sdk-path"], text=True).strip()
            if sdk_path and os.path.exists(sdk_path):
                usr_inc = os.path.join(sdk_path, "usr", "include")
                if os.path.exists(usr_inc):
                    include_dirs.append(NormalizePath(usr_inc))
        except Exception:
            pass

    return include_dirs

def UpdateParserConfig(target_os: str, parser_config_file: Path) -> None:
    """Config/parser_config.json 파일의 default_parser_args 및 platform_parser_args를 결합하여 active parser_args를 업데이트합니다."""
    parser_data: Dict[str, Any] = {}
    if parser_config_file.exists():
        try:
            with open(parser_config_file, "r", encoding="utf-8") as f:
                parser_data = json.load(f)
        except Exception:
            parser_data = {}

    default_args = parser_data.get("default_parser_args", [])
    platform_map = parser_data.get("platform_parser_args", {})
    platform_args = platform_map.get(target_os, [])

    active_args = list(default_args) + list(platform_args)
    parser_data["parser_args"] = active_args

    with open(parser_config_file, "w", encoding="utf-8") as f:
        json.dump(parser_data, f, indent=4)

def SetupEnvironment() -> Dict[str, Any]:
    """시스템을 동적 쿼리하여 Config/engine_config.json 및 Config/parser_config.json을 업데이트합니다."""
    project_root = GetProjectRoot()
    config_dir = project_root / "Config"
    config_dir.mkdir(parents=True, exist_ok=True)

    engine_config_file = config_dir / "engine_config.json"
    parser_config_file = config_dir / "parser_config.json"

    existing_config = {}
    if engine_config_file.exists():
        try:
            with open(engine_config_file, "r", encoding="utf-8") as f:
                existing_config = json.load(f)
        except Exception:
            pass

    def get_or_find(key: str, find_func, *args) -> Any:
        """
        @brief get_or_find 처리를 수행합니다.
        """
        val = existing_config.get(key)
        if val and (not isinstance(val, str) or os.path.exists(val)):
            return val
        return find_func(*args)

    llvm_path = get_or_find("llvm_path", FindLlvmPath)
    libclang_dll_path = get_or_find("libclang_dll_path", FindLibClangDllPath, llvm_path)

    sdk_dir_val = existing_config.get("windows_sdk_dir", "")
    sdk_ver_val = existing_config.get("windows_sdk_version", "")
    if not sdk_dir_val or not os.path.exists(sdk_dir_val):
        sdk_dir_val, sdk_ver_val = FindWindowsSdkPath()

    dxc_dll_path_val = existing_config.get("dxc_dll_path", "")
    dxil_dll_path_val = existing_config.get("dxil_dll_path", "")
    # vcpkg/Vulkan/Windows Kit 우선순위를 항상 재적용 (캐시된 Windows Kit 경로가 vcpkg DXC를 가리지 않게)
    found_dxc, found_dxil = FindDxcDlls(sdk_dir_val, sdk_ver_val, project_root)
    if found_dxc:
        dxc_dll_path_val = found_dxc
    elif not dxc_dll_path_val or not os.path.exists(dxc_dll_path_val):
        dxc_dll_path_val = ""
    if found_dxil:
        dxil_dll_path_val = found_dxil
    elif not dxil_dll_path_val or not os.path.exists(dxil_dll_path_val):
        dxil_dll_path_val = ""

    msvc_path = get_or_find("msvc_tools_dir", FindMsvcPath)

    def find_vcpkg_safe():
        """
        @brief find_vcpkg_safe 처리를 수행합니다.
        """
        try:
            from FindVcpkg import FindOrInstallVcpkg
            vcpkg_path_obj = FindOrInstallVcpkg()
            return NormalizePath(str(vcpkg_path_obj)) if vcpkg_path_obj else ""
        except ImportError:
            return ""

    vcpkg_path = get_or_find("vcpkg_root", find_vcpkg_safe)

    def find_ninja_safe():
        """
        @brief find_ninja_safe 처리를 수행합니다.
        """
        try:
            from SetupNinja import SetupNinja
            return NormalizePath(SetupNinja())
        except ImportError:
            return ""

    ninja_path = get_or_find("ninja_path", find_ninja_safe)
    sys_includes = get_or_find("system_include_dirs", FindSystemIncludeDirs)

    target_os = platform.system().lower()

    engine_config_data = {
        "target_platform": target_os,
        "target_arch": platform.machine().lower(),
        "llvm_path": llvm_path,
        "libclang_dll_path": libclang_dll_path,
        "windows_sdk_dir": sdk_dir_val,
        "windows_sdk_version": sdk_ver_val,
        "dxc_dll_path": dxc_dll_path_val,
        "dxil_dll_path": dxil_dll_path_val,
        "msvc_tools_dir": msvc_path,
        "vcpkg_root": vcpkg_path,
        "ninja_path": ninja_path,
        "system_include_dirs": sys_includes
    }

    new_json_str = json.dumps(engine_config_data, indent=4)
    should_write = True
    if engine_config_file.exists():
        try:
            with open(engine_config_file, "r", encoding="utf-8") as f:
                if f.read() == new_json_str:
                    should_write = False
        except Exception:
            pass

    if should_write:
        with open(engine_config_file, "w", encoding="utf-8") as f:
            f.write(new_json_str)

    UpdateParserConfig(target_os, parser_config_file)

    # CMake STATUS와 engine_config.json이 같은 내용을 보이게 전체 키를 출력한다.
    # (기존에는 LLVM/DXC/vcpkg/Ninja만 찍어 SDK/MSVC/dxil 등이 빠진 것처럼 보였음)
    print(f"[SetupEnvironment] Resolved Config/engine_config.json for OS '{platform.system()}':")
    print(f"  - target_platform      : {engine_config_data['target_platform']}")
    print(f"  - target_arch          : {engine_config_data['target_arch']}")
    print(f"  - llvm_path            : {engine_config_data['llvm_path']}")
    print(f"  - libclang_dll_path    : {engine_config_data['libclang_dll_path']}")
    print(f"  - windows_sdk_dir      : {engine_config_data['windows_sdk_dir']}")
    print(f"  - windows_sdk_version  : {engine_config_data['windows_sdk_version']}")
    print(f"  - dxc_dll_path         : {engine_config_data['dxc_dll_path']}")
    print(f"  - dxil_dll_path        : {engine_config_data['dxil_dll_path']}")
    print(f"  - msvc_tools_dir       : {engine_config_data['msvc_tools_dir']}")
    print(f"  - vcpkg_root           : {engine_config_data['vcpkg_root']}")
    print(f"  - ninja_path           : {engine_config_data['ninja_path']}")
    print(f"  - system_include_dirs  : {engine_config_data['system_include_dirs']}")
    print(f"  - config_file          : {NormalizePath(str(engine_config_file))}"
          f" ({'updated' if should_write else 'unchanged'})")

    return engine_config_data

if __name__ == "__main__":
    SetupEnvironment()
