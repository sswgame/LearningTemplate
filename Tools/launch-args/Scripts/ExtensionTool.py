#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Launch Args 확장의 시험 · 패키징 도구입니다. Node.js · npm · vsce 없이 돕니다.

VS Code 안에 든 Node 런타임(`ELECTRON_RUN_AS_NODE=1 Code.exe`)으로 시험을 돌리고, VSIX(zip + 매니페스트)는 파이썬이 직접 씁니다.

  py -3 Tools/launch-args/Scripts/ExtensionTool.py test                 # 단위 시험(node:test)
  py -3 Tools/launch-args/Scripts/ExtensionTool.py test --integration   # + 격리된 VS Code 확장 호스트 통합 시험
  py -3 Tools/launch-args/Scripts/ExtensionTool.py package              # 시험 뒤 dist/<이름>-<판>.vsix
  py -3 Tools/launch-args/Scripts/ExtensionTool.py install              # 만든 VSIX 를 지금 VS Code 에 설치

VS Code 위치는 `--code <Code.exe>` · 환경 변수 `VSCODE_EXECUTABLE` · 흔한 설치 경로 · PATH 의 `code` 순으로 찾습니다.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path
from xml.sax.saxutils import escape

_kExtensionRoot = Path(__file__).resolve().parents[1]
_kListPackagedFolder = ("Source", "Webview", "Profiles")
_kListPackagedFile = ("package.json", "README.md")
_kListUnitTest = ("CppTextUtilTest.js", "CatalogScannerTest.js", "LaunchArgumentUtilTest.js")
_kCmakeToolsExtensionPrefix = "ms-vscode.cmake-tools-"
_kIntegrationTimeoutSeconds = 180
_kMapContentType = {
    ".css": "text/css",
    ".html": "text/html",
    ".js": "application/javascript",
    ".json": "application/json",
    ".md": "text/markdown",
    ".svg": "image/svg+xml",
    ".png": "image/png",
    ".vsixmanifest": "text/xml",
}


def findCodeExecutable(explicitPath: str | None) -> Path:
    """VS Code 실행 파일(Electron)을 찾습니다. 없으면 SystemExit 입니다."""
    listCandidate: list[Path] = []
    if explicitPath:
        listCandidate.append(Path(explicitPath))
    if os.environ.get("VSCODE_EXECUTABLE"):
        listCandidate.append(Path(os.environ["VSCODE_EXECUTABLE"]))
    if sys.platform == "win32":
        for baseText in (os.environ.get("LOCALAPPDATA", "") + "/Programs", os.environ.get("ProgramFiles", ""), os.environ.get("ProgramFiles(x86)", "")):
            if baseText:
                listCandidate.append(Path(baseText) / "Microsoft VS Code" / "Code.exe")
    else:
        listCandidate += [Path("/usr/share/code/code"), Path("/usr/lib/code/code"), Path("/opt/visual-studio-code/code"), Path("/snap/code/current/usr/share/code/code")]
    launcher = shutil.which("code")
    if launcher:
        launcherPath = Path(launcher).resolve()
        listCandidate += [launcherPath.parent.parent / "Code.exe", launcherPath.parent.parent / "code"]
    for candidate in listCandidate:
        if candidate.is_file():
            return candidate
    raise SystemExit("VS Code executable not found - pass --code <path to Code.exe> or set VSCODE_EXECUTABLE")


def makeCleanEnvironmentInternal() -> dict[str, str]:
    """부모 VS Code 가 물려준 변수(`ELECTRON_RUN_AS_NODE` · `VSCODE_*`)를 뺀 환경입니다.

    VS Code 터미널 · 확장에서 이 스크립트를 돌리면 이 변수들이 남아, 띄운 VS Code 가 창 대신 Node 로 돌거나(종료 코드 9)
    부모 창의 IPC 에 붙습니다.
    """
    return {key: value for key, value in os.environ.items() if key != "ELECTRON_RUN_AS_NODE" and key.startswith("VSCODE_") is False}


def runUnitTestInternal(codePath: Path) -> int:
    """단위 시험을 VS Code 의 Node 로 돌립니다."""
    environment = dict(makeCleanEnvironmentInternal(), ELECTRON_RUN_AS_NODE="1")
    listTestPath = [str(_kExtensionRoot / "Test" / name) for name in _kListUnitTest]
    print(f"[unit] {codePath} --test ({len(listTestPath)} files)", flush=True)
    # Code.exe 는 GUI 실행 파일이라 콘솔을 물려주면 출력이 사라질 수 있다 — 받아서 직접 찍는다.
    result = subprocess.run([str(codePath), "--test", *listTestPath], env=environment, cwd=_kExtensionRoot, capture_output=True, text=True, encoding="utf-8", errors="replace")
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    return result.returncode


def findCmakeToolsInternal() -> Path | None:
    """사용자 확장 폴더에서 CMake Tools(가장 높은 판)를 찾습니다. 통합 시험은 그 설정(`cmake.debugConfig`)이 등록돼야 합니다."""
    extensionsRoot = Path.home() / ".vscode" / "extensions"
    if extensionsRoot.is_dir() is False:
        return None
    listFound = sorted(path for path in extensionsRoot.iterdir() if path.is_dir() and path.name.startswith(_kCmakeToolsExtensionPrefix))
    return listFound[-1] if listFound else None


def runIntegrationTestInternal(codePath: Path) -> int:
    """임시 작업 폴더 · 사용자 폴더 · 확장 폴더(CMake Tools 만)로 VS Code 를 띄워 `Test/Integration/Runner.js` 를 돌립니다."""
    cmakeTools = findCmakeToolsInternal()
    if cmakeTools is None:
        print("[integration] CMake Tools is not installed in ~/.vscode/extensions - cannot run", file=sys.stderr)
        return 2
    with tempfile.TemporaryDirectory(prefix="launch-args-it-") as temporaryText:
        temporaryRoot = Path(temporaryText)
        workspacePath = temporaryRoot / "workspace"
        shutil.copytree(_kExtensionRoot / "Test" / "Integration" / "Fixture", workspacePath)
        # 픽스처의 C++ · CMake 파일은 `.in` 으로 둔다 — 저장소 게이트(C++ 규칙 · clang-format)가 남의 프로젝트 글을 엔진 코드로 보지 않게.
        for templatePath in sorted(workspacePath.rglob("*.in")):
            templatePath.rename(templatePath.with_suffix(""))
        extensionsPath = temporaryRoot / "extensions"
        shutil.copytree(cmakeTools, extensionsPath / cmakeTools.name)
        listCommand = [
            str(codePath),
            f"--extensionDevelopmentPath={_kExtensionRoot}",
            f"--extensionTestsPath={_kExtensionRoot / 'Test' / 'Integration' / 'Runner.js'}",
            f"--user-data-dir={temporaryRoot / 'user-data'}",
            f"--extensions-dir={extensionsPath}",
            "--disable-workspace-trust",
            "--skip-welcome",
            "--skip-release-notes",
            "--new-window",
            str(workspacePath),
        ]
        print(f"[integration] {codePath.name} with {cmakeTools.name} on {workspacePath}", flush=True)
        try:
            returnCode = subprocess.run(listCommand, env=makeCleanEnvironmentInternal(), timeout=_kIntegrationTimeoutSeconds).returncode
        except subprocess.TimeoutExpired:
            print(f"[integration] timed out after {_kIntegrationTimeoutSeconds} s", file=sys.stderr)
            return 1
        bPassed = (workspacePath / "integration-result.txt").is_file()
        print(f"[integration] exit code {returnCode}, result file {'present' if bPassed else 'missing'}", flush=True)
        return 0 if returnCode == 0 and bPassed else 1


def makeManifestInternal(packageJson: dict) -> str:
    """`extension.vsixmanifest` 글입니다(vsce 가 쓰는 것과 같은 모양)."""
    keywordText = escape(",".join(packageJson.get("keywords", [])))
    categoryText = escape(",".join(packageJson.get("categories", [])))
    return f"""<?xml version="1.0" encoding="utf-8"?>
<PackageManifest Version="2.0.0" xmlns="http://schemas.microsoft.com/developer/vsx-schema/2011" xmlns:d="http://schemas.microsoft.com/developer/vsx-schema-design/2011">
  <Metadata>
    <Identity Language="en-US" ID="{escape(packageJson['name'])}" Version="{escape(packageJson['version'])}" Publisher="{escape(packageJson['publisher'])}" />
    <DisplayName>{escape(packageJson.get('displayName', packageJson['name']))}</DisplayName>
    <Description xml:space="preserve">{escape(packageJson.get('description', ''))}</Description>
    <Tags>{keywordText}</Tags>
    <Categories>{categoryText}</Categories>
    <GalleryFlags>Public</GalleryFlags>
    <Properties>
      <Property ID="Microsoft.VisualStudio.Code.Engine" Value="{escape(packageJson['engines']['vscode'])}" />
      <Property ID="Microsoft.VisualStudio.Code.ExtensionDependencies" Value="" />
      <Property ID="Microsoft.VisualStudio.Code.ExtensionPack" Value="" />
      <Property ID="Microsoft.VisualStudio.Code.ExtensionKind" Value="workspace" />
      <Property ID="Microsoft.VisualStudio.Code.LocalizedLanguages" Value="" />
      <Property ID="Microsoft.VisualStudio.Code.ExecutesCode" Value="true" />
      <Property ID="Microsoft.VisualStudio.Services.GitHubFlavoredMarkdown" Value="true" />
      <Property ID="Microsoft.VisualStudio.Services.Content.Pricing" Value="Free" />
    </Properties>
  </Metadata>
  <Installation>
    <InstallationTarget ID="Microsoft.VisualStudio.Code" />
  </Installation>
  <Dependencies />
  <Assets>
    <Asset Type="Microsoft.VisualStudio.Code.Manifest" Path="extension/package.json" Addressable="true" />
    <Asset Type="Microsoft.VisualStudio.Services.Content.Details" Path="extension/README.md" Addressable="true" />
  </Assets>
</PackageManifest>
"""


def collectPackagedFilesInternal() -> list[Path]:
    """VSIX 에 담을 파일(확장 루트 기준)입니다. 시험 · 스크립트 · 산출물은 담지 않습니다."""
    listFile = [_kExtensionRoot / name for name in _kListPackagedFile]
    for folderName in _kListPackagedFolder:
        listFile += sorted(path for path in (_kExtensionRoot / folderName).rglob("*") if path.is_file())
    for filePath in listFile:
        if filePath.is_file() is False:
            raise SystemExit(f"missing file for the package: {filePath}")
    return listFile


def packageVsix() -> Path:
    """`dist/<name>-<version>.vsix` 를 씁니다."""
    packageJson = json.loads((_kExtensionRoot / "package.json").read_text(encoding="utf-8"))
    listFile = collectPackagedFilesInternal()
    uniqueExtension = {".vsixmanifest", ".json"} | {filePath.suffix.lower() for filePath in listFile}
    contentTypeText = "".join(
        f'<Default Extension="{extension}" ContentType="{_kMapContentType.get(extension, "application/octet-stream")}"/>' for extension in sorted(uniqueExtension)
    )
    outputPath = _kExtensionRoot / "dist" / f"{packageJson['name']}-{packageJson['version']}.vsix"
    outputPath.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(outputPath, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("extension.vsixmanifest", makeManifestInternal(packageJson))
        archive.writestr("[Content_Types].xml", f'<?xml version="1.0" encoding="utf-8"?>\n<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">{contentTypeText}</Types>')
        for filePath in listFile:
            archive.write(filePath, "extension/" + filePath.relative_to(_kExtensionRoot).as_posix())
    print(f"[package] {outputPath} ({len(listFile)} files)")
    return outputPath


def installVsixInternal(codePath: Path, vsixPath: Path) -> int:
    """`code --install-extension` 으로 설치합니다(CLI 는 Code.exe 옆 bin/code)."""
    launcher = codePath.parent / "bin" / ("code.cmd" if sys.platform == "win32" else "code")
    if launcher.is_file() is False:
        launcher = Path(shutil.which("code") or "")
    if launcher.is_file() is False:
        raise SystemExit("VS Code command-line launcher (bin/code) not found")
    return subprocess.run([str(launcher), "--install-extension", str(vsixPath), "--force"], env=makeCleanEnvironmentInternal()).returncode


def main(listArgument: list[str] | None = None) -> int:
    """명령줄 진입점입니다."""
    parser = argparse.ArgumentParser(description="Test, package and install the Launch Args VS Code extension")
    parser.add_argument("command", choices=("test", "package", "install"))
    parser.add_argument("--code", help="path to Code.exe (VS Code executable)")
    parser.add_argument("--integration", action="store_true", help="test: also run the extension-host integration test")
    parser.add_argument("--skip-tests", action="store_true", help="package: do not run unit tests first")
    args = parser.parse_args(listArgument)

    if args.command == "test":
        codePath = findCodeExecutable(args.code)
        returnCode = runUnitTestInternal(codePath)
        if returnCode == 0 and args.integration:
            returnCode = runIntegrationTestInternal(codePath)
        return returnCode
    if args.command == "package":
        if args.skip_tests is False and runUnitTestInternal(findCodeExecutable(args.code)) != 0:
            print("[package] unit tests failed - not packaging (use --skip-tests to force)", file=sys.stderr)
            return 1
        packageVsix()
        return 0
    packageJson = json.loads((_kExtensionRoot / "package.json").read_text(encoding="utf-8"))
    vsixPath = _kExtensionRoot / "dist" / f"{packageJson['name']}-{packageJson['version']}.vsix"
    if vsixPath.is_file() is False:
        vsixPath = packageVsix()
    return installVsixInternal(findCodeExecutable(args.code), vsixPath)


if __name__ == "__main__":
    sys.exit(main())
