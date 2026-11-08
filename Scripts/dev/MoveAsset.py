"""
에셋 파일 · 폴더를 옮기고(이름 바꾸기 포함) 그것을 가리키는 참조를 한 번에 고친다 — 언리얼 Fix Up Redirectors 를 옮기면서 바로 하는 것과 같다.

  py -3 -m Scripts move-asset common/prefabs/props/bow.prefab.xml common/prefabs/weapons/bow.prefab.xml [--dry-run]
  py -3 -m Scripts move-asset game/empty/materials game/empty/surfaces

경로는 리소스 id(`Content/` 뒤 — 앞에 `Content/` 를 붙여도 된다)다. 하는 일:
  1. `git mv`
  2. 참조 파일(Content 의 글 에셋 · 테스트 CMake)에서 경로 칸(`Config/Dev/AssetReferenceFields.json` — 커밋 전 검사 · `App --check-asset-refs` ·
     에디터 이름 바꾸기와 같은 목록)의 값만 새 경로로. 폴더면 그 아래 경로 전부, 원본(`models_raw/x.glb`)이면 그 임포트 산출물 경로(`models/x…`)도.
     칸 밖의 글(주석 · 문서 · 출처 표)은 고치지 않는다 — 끝에 남은 옛 경로를 찍는다.
  3. 작업 트리 전체를 검사해 끊어진 참조가 남았으면 알린다(실패 코드).
임포트 산출물은 커밋하지 않으므로 원본을 옮긴 뒤에는 `App --import-models` 등으로 다시 임포트한다.
"""

from __future__ import annotations

import argparse
import time
from pathlib import Path

from common.ExitCode import ExitCode
from common.Git import runGit
from common.Paths import getProjectRoot
from lint.AssetReferences import FieldConfig, checkWorkingTree, extractReferences, isReferrer, kContentFolder, listIndexFiles, loadConfig

kNameCharacters = set("abcdefghijklmnopqrstuvwxyz0123456789_.-/")
# 옮긴 뒤 옛 경로가 칸 밖에 남았는지 훑는 글 파일 — 경로 칸의 글 에셋 · 문서 · 출처 표 · 테스트 CMake.
kLeftoverSuffix = (".xml", ".json", ".material", ".md", ".txt", ".cpp", ".h")


def toAssetID(argument: str) -> str:
    normalized = argument.replace("\\", "/").strip("/")
    if normalized.lower().startswith(kContentFolder.lower() + "/"):
        normalized = normalized[len(kContentFolder) + 1:]
    return normalized


def makeRules(oldID: str, newID: str, bFolder: bool, config: FieldConfig) -> list[tuple[str, str, bool]]:
    """(옛 접두, 새 접두, 폴더인가) 규칙들 — 옮긴 것 자신과, 원본이면 그 임포트 산출물 자리."""
    listRule = [(oldID.lower(), newID.lower(), bFolder)]
    for imported in config.listImported:
        marker = "/" + imported.sourceFolder + "/"
        if marker not in "/" + oldID or marker not in "/" + newID:
            continue
        oldImported = ("/" + oldID).replace(marker, "/" + imported.folder + "/", 1)[1:]
        newImported = ("/" + newID).replace(marker, "/" + imported.folder + "/", 1)[1:]
        if not bFolder:
            if not oldID.lower().endswith(imported.listSourceExtension) or not newID.lower().endswith(imported.listSourceExtension):
                continue
            oldImported = oldImported.rsplit(".", 1)[0]
            newImported = newImported.rsplit(".", 1)[0]
        listRule.append((oldImported.lower(), newImported.lower(), True))   # 산출물은 `<이름>.mesh` · `<이름>/…` — 접두로 바꾼다
    return listRule


def rewritePath(path: str, listRule: list[tuple[str, str, bool]]) -> str | None:
    for oldPrefix, newPrefix, bPrefix in listRule:
        if path == oldPrefix:
            return newPrefix
        if bPrefix and (path.startswith(oldPrefix + "/") or path.startswith(oldPrefix + ".")):
            return newPrefix + path[len(oldPrefix):]
    return None


def rewriteReferences(relativePath: str, text: str, listRule: list[tuple[str, str, bool]], config: FieldConfig) -> tuple[str, int]:
    """참조 칸의 값 가운데 규칙에 맞는 낱말만 바꿉니다. 뒤에서부터 바꿔 앞 자리가 밀리지 않게 한다."""
    listEdit = []
    for reference in extractReferences(relativePath, text, config):
        if (rewritten := rewritePath(reference.path, listRule)) is not None:
            listEdit.append((reference.start, reference.end, rewritten))
    for start, end, rewritten in sorted(listEdit, reverse=True):
        text = text[:start] + rewritten + text[end:]
    return text, len(listEdit)


def gitMove(root: Path, oldPath: str, newPath: str) -> None:
    (root / newPath).parent.mkdir(parents=True, exist_ok=True)
    for _ in range(20):  # IDE 의 git 이 잠깐 잡는 index.lock 은 기다리면 풀린다 — 반쯤 옮긴 채 멈추지 않게
        result = runGit(["mv", oldPath, newPath], cwd=root)
        if result.bSucceeded or "index.lock" not in result.stderr:
            break
        time.sleep(0.5)
    if not result.bSucceeded:
        raise RuntimeError(f"git mv {oldPath} {newPath} 실패: {result.stderr.strip()}")
    folder = (root / oldPath).parent
    content = root / kContentFolder
    while folder != content and folder.is_dir() and not any(folder.iterdir()):   # 비워진 옛 폴더를 남기지 않는다
        folder.rmdir()
        folder = folder.parent


def reportLeftovers(root: Path, oldID: str) -> None:
    """칸 밖에 옛 경로가 남은 추적 글 파일(주석 · 출처 표 · 문서)을 알립니다. 고치지 않는다 — 경로인지 사람이 읽고 정한다."""
    lowered = oldID.lower()
    listLeftover = []
    for relativePath in listIndexFiles(root):
        if not relativePath.lower().endswith(kLeftoverSuffix) or not (root / relativePath).is_file():
            continue
        if lowered in (root / relativePath).read_bytes().decode("utf-8", "replace").lower():
            listLeftover.append(relativePath)
    for relativePath in listLeftover:
        print(f"  [남은 옛 경로 — 칸 밖] {relativePath}")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts move-asset", description="에셋을 옮기고 경로 칸의 참조를 고친다")
    parser.add_argument("old", help="옛 리소스 id(파일 또는 폴더)")
    parser.add_argument("new", help="새 리소스 id")
    parser.add_argument("--dry-run", action="store_true", help="고칠 파일만 찍는다")
    args = parser.parse_args(argv)

    root = getProjectRoot()
    config = loadConfig(root)
    oldID, newID = toAssetID(args.old), toAssetID(args.new)
    oldPath, newPath = f"{kContentFolder}/{oldID}", f"{kContentFolder}/{newID}"
    if not set(newID) <= kNameCharacters:
        print(f"[move-asset] '{newID}' 는 리소스 이름 규칙(소문자 · 숫자 · '_' · '-' · '.' · '/')에 맞지 않습니다")
        return ExitCode.CheckInvalid
    if not (root / oldPath).exists():
        print(f"[move-asset] '{oldPath}' 가 없습니다")
        return ExitCode.CheckInvalid
    if (root / newPath).exists():
        print(f"[move-asset] '{newPath}' 가 이미 있습니다 — 아무것도 옮기지 않았습니다")
        return ExitCode.CheckInvalid
    bFolder = (root / oldPath).is_dir()
    listRule = makeRules(oldID, newID, bFolder, config)

    if not args.dry_run:
        gitMove(root, oldPath, newPath)
    fixedCount = 0
    for relativePath in listIndexFiles(root):
        if not isReferrer(relativePath, config) or not (root / relativePath).is_file():
            continue
        text = (root / relativePath).read_bytes().decode("utf-8", "surrogateescape")
        rewritten, editCount = rewriteReferences(relativePath, text, listRule, config)
        if editCount == 0:
            continue
        fixedCount += 1
        print(f"  {relativePath}: {editCount}")
        if not args.dry_run:
            (root / relativePath).write_bytes(rewritten.encode("utf-8", "surrogateescape"))
    print(f"[move-asset] {oldID} -> {newID}: 참조 파일 {fixedCount} 개{' (dry run — 아무것도 바꾸지 않았다)' if args.dry_run else ''}")
    if not args.dry_run:
        reportLeftovers(root, oldID)
    if args.dry_run:
        return ExitCode.Passed

    listProblem = checkWorkingTree(root)
    for problem in listProblem:
        print(f"  - {problem}")
    if listProblem:
        print(f"[move-asset] 끊어진 참조 {len(listProblem)}건이 남았습니다 — 목록에 없는 칸이면 그 칸을 읽는 구조체에 PROPERTY( AssetPath ) 로 선언한다(Config/Dev/AssetReferenceFields.generated.json)")
        return ExitCode.Violation
    return ExitCode.Passed
