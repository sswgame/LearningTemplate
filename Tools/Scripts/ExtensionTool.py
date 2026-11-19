#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""저장소 안 VS Code 확장(`Tools/launch-args` · `Tools/comment-fold`)의 테스트 · 패키징 도구입니다. Node.js · npm · vsce 없이 돕니다.

VS Code 안에 든 Node 런타임(`ELECTRON_RUN_AS_NODE=1 Code.exe`)으로 테스트를 돌리고, VSIX(zip + 매니페스트)는 파이썬이 직접 씁니다.
확장 폴더를 여럿 주면 차례로 하고, 하나라도 지면 0 이 아닌 코드로 끝납니다.

  py -3 Tools/Scripts/ExtensionTool.py test Tools/launch-args Tools/comment-fold   # 단위 테스트(node:test, Test/*Test.js)
  py -3 Tools/Scripts/ExtensionTool.py test --integration Tools/comment-fold       # + 격리된 VS Code 확장 호스트 통합 테스트
  py -3 Tools/Scripts/ExtensionTool.py package Tools/comment-fold                  # 테스트 뒤 dist/<이름>-<판>.vsix
  py -3 Tools/Scripts/ExtensionTool.py install Tools/comment-fold                  # 만든 VSIX 를 지금 VS Code 에 설치

확장마다 다른 것은 확장 폴더의 데이터가 정합니다(vsce 와 같은 자리):
- VSIX 에 담을 것: package.json `files`(폴더 · 파일 이름)와 package.json. README.md 는 있으면 담습니다.
- 단위 테스트: `Test/*Test.js` 전부.
- 통합 테스트: `Test/Integration/Runner.js`. 작업 폴더는 `Test/Integration/Fixture/` 사본(없으면 빈 폴더),
  함께 띄울 사용자 확장은 `Test/Integration/RequiredExtensions.json`(확장 폴더 이름 접두사 배열, 파일이 없으면 없음).

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

_kRepositoryRoot = Path(__file__).resolve().parents[2]
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
    """VS Code 실행 파일(Electron)을 찾습니다. 없으면 sys.exit 로 끝냅니다."""
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
    sys.exit("VS Code executable not found - pass --code <path to Code.exe> or set VSCODE_EXECUTABLE")


def makeCleanEnvironmentInternal() -> dict[str, str]:
    """부모 VS Code 가 물려준 변수(`ELECTRON_RUN_AS_NODE` · `VSCODE_*`)를 뺀 환경입니다.

    VS Code 터미널 · 확장에서 이 스크립트를 돌리면 이 변수들이 남아, 띄운 VS Code 가 창 대신 Node 로 돌거나(종료 코드 9)
    부모 창의 IPC 에 붙습니다.
    """
    return {key: value for key, value in os.environ.items() if key != "ELECTRON_RUN_AS_NODE" and key.startswith("VSCODE_") is False}


def readPackageJsonInternal(extensionRoot: Path) -> dict:
    """확장의 package.json 입니다. 없으면 sys.exit 로 끝냅니다."""
    packagePath = extensionRoot / "package.json"
    if packagePath.is_file() is False:
        sys.exit(f"not an extension folder (no package.json): {extensionRoot}")
    return json.loads(packagePath.read_text(encoding="utf-8"))


def runUnitTestInternal(codePath: Path, extensionRoot: Path) -> int:
    """`Test/*Test.js` 를 VS Code 의 Node 로 돌립니다. 테스트가 하나도 없으면 실패입니다."""
    environment = dict(makeCleanEnvironmentInternal(), ELECTRON_RUN_AS_NODE="1")
    listTestPath = [str(path) for path in sorted((extensionRoot / "Test").glob("*Test.js"))]
    if len(listTestPath) == 0:
        print(f"[unit] no Test/*Test.js in {extensionRoot}", file=sys.stderr)
        return 1
    print(f"[unit] {extensionRoot.name}: {codePath} --test ({len(listTestPath)} files)", flush=True)
    # Code.exe 는 GUI 실행 파일이라 콘솔을 물려주면 출력이 사라질 수 있다 — 받아서 직접 찍는다.
    result = subprocess.run([str(codePath), "--test", *listTestPath], env=environment, cwd=extensionRoot, capture_output=True, text=True, encoding="utf-8", errors="replace")
    # 콘솔 코드 페이지(cp949)가 node:test 의 기호(✔)를 못 써 멈췄다 — UTF-8 바이트로 쓴다.
    sys.stdout.buffer.write(result.stdout.encode("utf-8"))
    sys.stderr.buffer.write(result.stderr.encode("utf-8"))
    return result.returncode


def findRequiredExtensionsInternal(integrationRoot: Path) -> list[Path] | None:
    """`RequiredExtensions.json` 의 접두사마다 사용자 확장 폴더에서 가장 높은 판을 찾습니다. 하나라도 없으면 None 입니다.

    launch-args 는 CMake Tools 의 설정(`cmake.debugConfig`)이 등록돼야 통합 테스트가 돕니다.
    """
    listFilePath = integrationRoot / "RequiredExtensions.json"
    if listFilePath.is_file() is False:
        return []
    extensionsRoot = Path.home() / ".vscode" / "extensions"
    listFound: list[Path] = []
    for prefix in json.loads(listFilePath.read_text(encoding="utf-8")):
        listMatch = sorted(path for path in extensionsRoot.iterdir() if path.is_dir() and path.name.startswith(prefix)) if extensionsRoot.is_dir() else []
        if len(listMatch) == 0:
            print(f"[integration] {prefix}* is not installed in ~/.vscode/extensions - cannot run", file=sys.stderr)
            return None
        listFound.append(listMatch[-1])
    return listFound


def runIntegrationTestInternal(codePath: Path, extensionRoot: Path) -> int:
    """임시 작업 폴더 · 사용자 폴더 · 확장 폴더(필요한 확장만)로 VS Code 를 띄워 `Test/Integration/Runner.js` 를 돌립니다."""
    integrationRoot = extensionRoot / "Test" / "Integration"
    if (integrationRoot / "Runner.js").is_file() is False:
        print(f"[integration] no Test/Integration/Runner.js in {extensionRoot}", file=sys.stderr)
        return 1
    listRequired = findRequiredExtensionsInternal(integrationRoot)
    if listRequired is None:
        return 2
    with tempfile.TemporaryDirectory(prefix=f"{extensionRoot.name}-it-") as temporaryText:
        temporaryRoot = Path(temporaryText)
        workspacePath = temporaryRoot / "workspace"
        if (integrationRoot / "Fixture").is_dir():
            shutil.copytree(integrationRoot / "Fixture", workspacePath)
        else:
            workspacePath.mkdir()
        # 픽스처의 C++ · CMake 파일은 `.in` 으로 둔다 — 저장소 게이트(C++ 규칙 · clang-format)가 남의 프로젝트 글을 엔진 코드로 보지 않게.
        for templatePath in sorted(workspacePath.rglob("*.in")):
            templatePath.rename(templatePath.with_suffix(""))
        extensionsPath = temporaryRoot / "extensions"
        extensionsPath.mkdir()
        for requiredPath in listRequired:
            shutil.copytree(requiredPath, extensionsPath / requiredPath.name)
        listCommand = [
            str(codePath),
            f"--extensionDevelopmentPath={extensionRoot}",
            f"--extensionTestsPath={integrationRoot / 'Runner.js'}",
            f"--user-data-dir={temporaryRoot / 'user-data'}",
            f"--extensions-dir={extensionsPath}",
            "--disable-workspace-trust",
            "--skip-welcome",
            "--skip-release-notes",
            "--new-window",
            str(workspacePath),
        ]
        requiredText = ", ".join(path.name for path in listRequired) or "no other extension"
        print(f"[integration] {extensionRoot.name}: {codePath.name} with {requiredText} on {workspacePath}", flush=True)
        try:
            returnCode = subprocess.run(listCommand, env=makeCleanEnvironmentInternal(), timeout=_kIntegrationTimeoutSeconds).returncode
        except subprocess.TimeoutExpired:
            print(f"[integration] timed out after {_kIntegrationTimeoutSeconds} s", file=sys.stderr)
            return 1
        bPassed = (workspacePath / "integration-result.txt").is_file()
        print(f"[integration] exit code {returnCode}, result file {'present' if bPassed else 'missing'}", flush=True)
        return 0 if returnCode == 0 and bPassed else 1


def makeManifestInternal(packageJson: dict, bHasReadme: bool) -> str:
    """`extension.vsixmanifest` 글입니다(vsce 가 쓰는 것과 같은 모양)."""
    keywordText = escape(",".join(packageJson.get("keywords", [])))
    categoryText = escape(",".join(packageJson.get("categories", [])))
    readmeAssetText = '\n    <Asset Type="Microsoft.VisualStudio.Services.Content.Details" Path="extension/README.md" Addressable="true" />' if bHasReadme else ""
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
    <Asset Type="Microsoft.VisualStudio.Code.Manifest" Path="extension/package.json" Addressable="true" />{readmeAssetText}
  </Assets>
</PackageManifest>
"""


def collectPackagedFilesInternal(extensionRoot: Path, packageJson: dict) -> list[Path]:
    """VSIX 에 담을 파일입니다 — package.json · README.md(있으면) · `files` 의 폴더와 파일. 테스트 · 스크립트 · 산출물은 담지 않습니다."""
    if "files" not in packageJson:
        sys.exit(f"package.json has no 'files' (folders and files to package): {extensionRoot}")
    listFile = [extensionRoot / "package.json"]
    if (extensionRoot / "README.md").is_file():
        listFile.append(extensionRoot / "README.md")
    for entryName in packageJson["files"]:
        entryPath = extensionRoot / entryName
        if entryPath.is_dir():
            listFile += sorted(path for path in entryPath.rglob("*") if path.is_file())
        elif entryPath.is_file():
            listFile.append(entryPath)
        else:
            sys.exit(f"missing entry of package.json 'files': {entryPath}")
    return listFile


def findVsixPathInternal(extensionRoot: Path) -> Path:
    """`dist/<name>-<version>.vsix` 자리입니다."""
    packageJson = readPackageJsonInternal(extensionRoot)
    return extensionRoot / "dist" / f"{packageJson['name']}-{packageJson['version']}.vsix"


def packageVsix(extensionRoot: Path) -> Path:
    """`dist/<name>-<version>.vsix` 를 씁니다."""
    packageJson = readPackageJsonInternal(extensionRoot)
    listFile = collectPackagedFilesInternal(extensionRoot, packageJson)
    uniqueExtension = {".vsixmanifest", ".json"} | {filePath.suffix.lower() for filePath in listFile}
    contentTypeText = "".join(
        f'<Default Extension="{extension}" ContentType="{_kMapContentType.get(extension, "application/octet-stream")}"/>' for extension in sorted(uniqueExtension)
    )
    outputPath = findVsixPathInternal(extensionRoot)
    outputPath.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(outputPath, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("extension.vsixmanifest", makeManifestInternal(packageJson, extensionRoot / "README.md" in listFile))
        archive.writestr("[Content_Types].xml", f'<?xml version="1.0" encoding="utf-8"?>\n<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">{contentTypeText}</Types>')
        for filePath in listFile:
            archive.write(filePath, "extension/" + filePath.relative_to(extensionRoot).as_posix())
    print(f"[package] {outputPath} ({len(listFile)} files)")
    return outputPath


def installVsixInternal(codePath: Path, vsixPath: Path) -> int:
    """`code --install-extension` 으로 설치합니다(CLI 는 Code.exe 옆 bin/code)."""
    launcher = codePath.parent / "bin" / ("code.cmd" if sys.platform == "win32" else "code")
    if launcher.is_file() is False:
        launcher = Path(shutil.which("code") or "")
    if launcher.is_file() is False:
        sys.exit("VS Code command-line launcher (bin/code) not found")
    return subprocess.run([str(launcher), "--install-extension", str(vsixPath), "--force"], env=makeCleanEnvironmentInternal()).returncode


def runCommandInternal(command: str, codePath: Path, extensionRoot: Path, bIntegration: bool, bSkipTests: bool) -> int:
    """확장 폴더 하나에 명령 하나를 합니다."""
    if command == "test":
        returnCode = runUnitTestInternal(codePath, extensionRoot)
        if returnCode == 0 and bIntegration:
            returnCode = runIntegrationTestInternal(codePath, extensionRoot)
        return returnCode
    if command == "package":
        if bSkipTests is False and runUnitTestInternal(codePath, extensionRoot) != 0:
            print(f"[package] {extensionRoot.name}: unit tests failed - not packaging (use --skip-tests to force)", file=sys.stderr)
            return 1
        packageVsix(extensionRoot)
        return 0
    vsixPath = findVsixPathInternal(extensionRoot)
    if vsixPath.is_file() is False:
        vsixPath = packageVsix(extensionRoot)
    return installVsixInternal(codePath, vsixPath)


def main(listArgument: list[str] | None = None) -> int:
    """명령줄 진입점입니다."""
    parser = argparse.ArgumentParser(description="Test, package and install the VS Code extensions under Tools/")
    parser.add_argument("command", choices=("test", "package", "install"))
    parser.add_argument("extension", nargs="+", help="extension folder, absolute or relative to the repository root (e.g. Tools/comment-fold)")
    parser.add_argument("--code", help="path to Code.exe (VS Code executable)")
    parser.add_argument("--integration", action="store_true", help="test: also run the extension-host integration test")
    parser.add_argument("--skip-tests", action="store_true", help="package: do not run unit tests first")
    args = parser.parse_args(listArgument)

    codePath = findCodeExecutable(args.code)
    listReturnCode = [runCommandInternal(args.command, codePath, (_kRepositoryRoot / extensionText).resolve(), args.integration, args.skip_tests) for extensionText in args.extension]
    return next((returnCode for returnCode in listReturnCode if returnCode != 0), 0)


if __name__ == "__main__":
    sys.exit(main())
