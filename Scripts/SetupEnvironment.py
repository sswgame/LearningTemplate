"""
Scripts/SetupEnvironment.py

프로젝트 빌드 전 개발 PC 환경(Windows / Linux / macOS)을 분석하여
빌드 도구(Clang/LLVM, Windows SDK, DXC, MSVC, vcpkg, Ninja 등) 경로를
`Config/engine_config.json` / `Config/parser_config.json` 에 기록합니다.

네이밍:

- 공개 함수: PascalCase (FindLlvmPath, SetupEnvironment, ...)
- 비공개 헬퍼: _snake_case (_find_first_existing_file, _get_or_find, ...)
- engine_config 키: snake_case (llvm_path, dxc_dll_path, ...)
"""

import json
import os
import platform
import shutil
import subprocess
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

        if (
            (parent / "include").exists()
            or (parent / "bin" / "clang-cl.exe").exists()
            or (parent / "bin" / "clang").exists()
        ):
            return NormalizePath(str(parent))

    # 일반 Windows 설치 경로
    if platform.system() == "Windows":
        for root in (
            Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "LLVM",
            Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "LLVM",
            Path(r"C:\Program Files\LLVM"),
        ):
            if (
                (root / "bin" / "clang-cl.exe").exists()
                or (root / "bin" / "clang.exe").exists()
            ):
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
                cmd = [vswhere, "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools", "-property", "installationPath"]
                vs_path = subprocess.check_output(cmd, text=True, encoding="utf-8", errors="replace").strip()

                if not vs_path:
                    cmd_fallback = [vswhere, "-latest", "-products", "*", "-property", "installationPath"]
                    vs_path = subprocess.check_output(cmd_fallback, text=True, encoding="utf-8", errors="replace").strip()

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
    """libclang 라이브러리 경로를 동적으로 탐색합니다."""
    sys_name = platform.system()

    if sys_name == "Windows":
        lib_names = ["libclang.dll"]
    elif sys_name == "Darwin":
        lib_names = ["libclang.dylib"]
    else:
        lib_names = ["libclang.so", "libclang.so.1"]

    search_dirs: List[Path] = []

    if llvm_path:
        llvm_root = Path(llvm_path)
        search_dirs.extend([llvm_root / "bin", llvm_root / "lib"])

    found = _find_first_existing_file(search_dirs, lib_names)

    if found:
        return NormalizePath(found)

    for lib_name in lib_names:
        found = shutil.which(lib_name)

        if found:
            return NormalizePath(found)

    if llvm_path:
        found = _find_first_existing_file_recursive([Path(llvm_path)], lib_names)

        if found:
            return NormalizePath(found)

    return ""


def _find_first_existing_file(search_dirs: List[Path], file_names: List[str]) -> str:
    """주어진 디렉터리 목록에서 특정 파일을 순차 탐색하여 첫 번째 발견된 경로를 반환합니다."""
    for directory in search_dirs:
        if not directory.exists():
            continue

        for file_name in file_names:
            candidate = directory / file_name

            if candidate.is_file():
                return str(candidate)

    return ""


def _find_first_existing_file_recursive(search_dirs: List[Path], file_names: List[str]) -> str:
    """주어진 디렉터리 목록을 재귀적으로 탐색하여 첫 번째 발견된 파일의 경로를 반환합니다."""
    for directory in search_dirs:
        if not directory.exists() or not directory.is_dir():
            continue

        for file_name in file_names:
            try:
                for candidate in directory.rglob(file_name):
                    if candidate.is_file():
                        return str(candidate)
            except (OSError, PermissionError):
                pass

    return ""


def _find_vcpkg_installed_dirs(project_root: Path) -> List[Path]:
    """프로젝트 구조에서 vcpkg_installed 디렉터리를 탐색합니다."""
    result: List[Path] = []

    if not project_root.exists():
        return result

    try:
        for candidate in project_root.rglob("vcpkg_installed"):
            if candidate.is_dir():
                result.append(candidate)
    except (OSError, PermissionError):
        pass

    return result


def FindDxcDlls(sdk_dir: str, sdk_ver: str, project_root: Path) -> Tuple[str, str]:
    """
    DirectX Shader Compiler(dxcompiler, dxil) 경로를 동적으로 탐색합니다.

    우선순위:
    1. 프로젝트 내부 vcpkg_installed
    2. VULKAN_SDK
    3. Windows SDK
    4. 시스템 PATH

    빌드 디렉터리나 preset 이름은 하드코딩하지 않습니다.
    """
    sys_name = platform.system()

    if sys_name == "Windows":
        dxc_names = ["dxcompiler.dll"]
        dxil_names = ["dxil.dll"]
    elif sys_name == "Darwin":
        dxc_names = ["libdxcompiler.dylib"]
        dxil_names = ["libdxil.dylib"]
    else:
        dxc_names = ["libdxcompiler.so", "libdxcompiler.so.1"]
        dxil_names = ["libdxil.so", "libdxil.so.1"]

    dxc_dll = ""
    dxil_dll = ""

    # 프로젝트 내부 vcpkg
    vcpkg_dirs = _find_vcpkg_installed_dirs(project_root)

    if vcpkg_dirs:
        dxc_dll = _find_first_existing_file_recursive(vcpkg_dirs, dxc_names)
        dxil_dll = _find_first_existing_file_recursive(vcpkg_dirs, dxil_names)

    # Vulkan SDK
    if not dxc_dll or not dxil_dll:
        vulkan_sdk = os.environ.get("VULKAN_SDK")

        if vulkan_sdk:
            vulkan_root = Path(vulkan_sdk)

            if not dxc_dll:
                dxc_dll = _find_first_existing_file_recursive([vulkan_root], dxc_names)

            if not dxil_dll:
                dxil_dll = _find_first_existing_file_recursive([vulkan_root], dxil_names)

    # Windows SDK
    if sys_name == "Windows" and sdk_dir and sdk_ver:
        sdk_root = Path(sdk_dir)

        if not dxc_dll:
            dxc_dll = _find_first_existing_file_recursive([sdk_root], dxc_names)

        if not dxil_dll:
            dxil_dll = _find_first_existing_file_recursive([sdk_root], dxil_names)

    # PATH
    if not dxc_dll:
        for dxc_name in dxc_names:
            found = shutil.which(dxc_name)

            if found:
                dxc_dll = NormalizePath(found)
                break

    if not dxil_dll:
        for dxil_name in dxil_names:
            found = shutil.which(dxil_name)

            if found:
                dxil_dll = NormalizePath(found)
                break

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


def _LoadParserConfigSeed(project_root: Path) -> Dict[str, Any]:
    """
    커밋된 Config/parser_config.defaults.json 을 시드로 읽고,
    없으면 내장 기본값을 사용합니다. (로컬 parser_config.json 은 gitignore)
    """
    defaults_file = project_root / "Config" / "parser_config.defaults.json"
    if defaults_file.exists():
        try:
            with open(defaults_file, "r", encoding="utf-8") as f:
                loaded = json.load(f)
            if isinstance(loaded, dict) and isinstance(loaded.get("default_parser_args"), list):
                return loaded
        except Exception:
            pass

    return {
        "default_parser_args": [
            "-std=c++17",
            "-D__REFLECT_PARSER__",
            "-DSW_API=",
            '-DREFLECT(...)=__attribute__((annotate("REFLECT;" #__VA_ARGS__)))',
            '-DPROPERTY(...)=__attribute__((annotate("PROPERTY;" #__VA_ARGS__)))',
            '-DFUNCTION(...)=__attribute__((annotate("FUNCTION;" #__VA_ARGS__)))',
            '-DENUM(...)=__attribute__((annotate("ENUM;" #__VA_ARGS__)))',
            "-x",
            "c++",
            "-w",
        ],
        "platform_parser_args": {
            "windows": [
                "-fms-compatibility",
                "-fms-compatibility-version=19",
                "-fms-extensions",
            ],
            "linux": [],
            "darwin": [],
        },
    }


def UpdateParserConfig(target_os: str, parser_config_file: Path) -> None:
    """Config/parser_config.json 파일의 parser 인자를 업데이트합니다."""
    project_root = GetProjectRoot()
    parser_data: Dict[str, Any] = _LoadParserConfigSeed(project_root)

    if parser_config_file.exists():
        try:
            with open(parser_config_file, "r", encoding="utf-8") as f:
                loaded = json.load(f)
            if isinstance(loaded, dict):
                # Keep defaults for missing keys so a fresh clone still works.
                if isinstance(loaded.get("default_parser_args"), list):
                    parser_data["default_parser_args"] = loaded["default_parser_args"]
                if isinstance(loaded.get("platform_parser_args"), dict):
                    parser_data["platform_parser_args"] = loaded["platform_parser_args"]
        except Exception:
            pass

    default_args = parser_data.get("default_parser_args", [])
    platform_map = parser_data.get("platform_parser_args", {})
    platform_args = platform_map.get(target_os, [])

    active_args = list(default_args) + list(platform_args)
    parser_data["parser_args"] = active_args

    parser_config_file.parent.mkdir(parents=True, exist_ok=True)
    with open(parser_config_file, "w", encoding="utf-8") as f:
        json.dump(parser_data, f, indent=4)


def SetupEnvironment() -> Dict[str, Any]:
    """시스템을 동적 쿼리하여 engine_config.json과 parser_config.json을 업데이트합니다."""
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

    def _get_or_find(key: str, find_func, *args) -> Any:
        """engine_config 캐시가 유효하면 재사용, 아니면 find_func로 탐색합니다."""
        val = existing_config.get(key)

        if val and (not isinstance(val, str) or os.path.exists(val)):
            return val

        return find_func(*args)

    llvm_path = _get_or_find("llvm_path", FindLlvmPath)

    libclang_dll_path = _get_or_find("libclang_dll_path", FindLibClangDllPath, llvm_path)

    sdk_dir_val = existing_config.get("windows_sdk_dir", "")
    sdk_ver_val = existing_config.get("windows_sdk_version", "")

    if platform.system() == "Windows" and (not sdk_dir_val or not os.path.exists(sdk_dir_val)):
        sdk_dir_val, sdk_ver_val = FindWindowsSdkPath()

    if platform.system() != "Windows":
        sdk_dir_val = ""
        sdk_ver_val = ""

    # 프로젝트 구조 / Vulkan SDK / Windows SDK / PATH 순서로 DXC 탐색
    found_dxc, found_dxil = FindDxcDlls(sdk_dir_val, sdk_ver_val, project_root)

    dxc_dll_path_val = found_dxc
    dxil_dll_path_val = found_dxil

    msvc_path = _get_or_find("msvc_tools_dir", FindMsvcPath)

    def _find_vcpkg_root() -> str:
        try:
            from FindVcpkg import FindOrInstallVcpkg

            vcpkg_path_obj = FindOrInstallVcpkg()
            return NormalizePath(str(vcpkg_path_obj)) if vcpkg_path_obj else ""

        except ImportError:
            return ""

    vcpkg_path = _get_or_find("vcpkg_root", _find_vcpkg_root)

    def _find_ninja_path() -> str:
        try:
            from SetupNinja import SetupNinja
            return NormalizePath(SetupNinja())
        except ImportError:
            return ""

    ninja_path = _get_or_find("ninja_path", _find_ninja_path)

    sys_includes = _get_or_find("system_include_dirs", FindSystemIncludeDirs)

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

    # Linux/WSL: apply home-dir developer fixes (debuginfod/GDB) so Remote and
    # bare-metal Linux both get them on configure without manual shell steps.
    if platform.system() == "Linux":
        try:
            from SetupLinuxDevEnvironment import SetupLinuxDevEnvironment

            SetupLinuxDevEnvironment()
        except Exception as exc:
            print(f"[SetupEnvironment] SetupLinuxDevEnvironment skipped: {exc}")

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
    print(f"  - ninja_path            : {engine_config_data['ninja_path']}")
    print(f"  - system_include_dirs  : {engine_config_data['system_include_dirs']}")
    print(f"  - config_file          : {NormalizePath(str(engine_config_file))} ({'updated' if should_write else 'unchanged'})")

    return engine_config_data


if __name__ == "__main__":
    SetupEnvironment()