#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
에셋 검증 — `Config/Editor/AssetValidationRules.json` 의 규칙을 `Resource/` 에 돌려 (파일, 규칙, 심각도, 메시지) 표를 찍는다.

    py -3 -m Scripts validate-assets                         # 트리 전체, 모든 심각도
    py -3 -m Scripts validate-assets --severity error        # 오류만(게이트와 같은 판정)
    py -3 -m Scripts validate-assets --files game/x/maps/a.scene.xml   # 파일 하나(에디터가 저장 · 임포트 뒤에 부른다)
    py -3 -m Scripts validate-assets --json report.json      # 기계가 읽을 결과
    py -3 -m Scripts validate-assets --list-checks           # 규칙 표가 고를 수 있는 검사 이름

종료 코드: 오류(error)가 하나라도 있으면 1, 규칙 표를 못 읽으면 2, 그 밖에는 0 이다. 경고는 막지 않는다.
`--files` 는 리소스 경로(`engine/...`) · 저장소 경로(`Resource/engine/...`) · 절대 경로를 모두 받는다.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from common import getProjectRoot  # noqa: E402
from common.AssetValidation import RuleConfigError, getCheckNames, kSeverityOrder, loadRules, validate  # noqa: E402

#: 규칙 표의 기본 자리(저장소 루트 기준).
kDefaultRulePath = "Config/Editor/AssetValidationRules.json"


def toResourcePathInternal(rawPath: str, repositoryRoot: Path, resourceRoot: Path) -> str:
    """리소스 경로 · 저장소 경로 · 절대 경로를 리소스 경로(`engine/...`)로 맞춥니다."""
    candidate = Path(rawPath)
    if candidate.is_absolute():
        try:
            return candidate.resolve().relative_to(resourceRoot.resolve()).as_posix()
        except ValueError:
            return candidate.as_posix()
    posix = rawPath.replace("\\", "/")
    if posix.startswith("Resource/"):
        return posix[len("Resource/"):]
    return posix


def main(listArgument: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Resource/ 를 Config/Editor/AssetValidationRules.json 의 규칙으로 검증한다")
    parser.add_argument("--root", type=Path, default=None, help="저장소 루트(기본: 자동)")
    parser.add_argument("--rules", type=Path, default=None, help=f"규칙 파일(기본: {kDefaultRulePath})")
    parser.add_argument("--files", nargs="*", default=None, help="이 에셋만")
    parser.add_argument("--severity", choices=kSeverityOrder, default="info", help="보고할 가장 낮은 심각도")
    parser.add_argument("--json", type=Path, default=None, help="찾은 것을 JSON 으로도 쓴다")
    parser.add_argument("--list-checks", action="store_true", help="규칙이 쓸 수 있는 검사 이름을 찍는다")
    args = parser.parse_args(listArgument)

    if args.list_checks:
        print("\n".join(getCheckNames()))
        return 0

    repositoryRoot = (args.root or getProjectRoot()).resolve()
    resourceRoot = repositoryRoot / "Resource"
    try:
        listRule = loadRules(args.rules or repositoryRoot / kDefaultRulePath)
    except RuleConfigError as error:
        print(f"[ValidateAssets] {error}", file=sys.stderr)
        return 2

    listTarget = None
    if args.files is not None:
        listTarget = [toResourcePathInternal(path, repositoryRoot, resourceRoot) for path in args.files]
    listFinding = validate(listRule, resourceRoot, repositoryRoot, listTarget, args.severity)

    for finding in listFinding:
        print(finding.format())
    mapCount = {severity: sum(1 for finding in listFinding if finding.severity == severity) for severity in kSeverityOrder}
    scope = f"{len(listTarget)} file(s)" if listTarget is not None else "Resource/"
    print(f"[ValidateAssets] {scope}: {mapCount['error']} error(s), {mapCount['warning']} warning(s), {mapCount['info']} info, "
          f"{len(listRule)} rule(s)")
    if args.json is not None:
        args.json.write_text(json.dumps([finding.toJSON() for finding in listFinding], indent=2), encoding="utf-8")
    return 1 if mapCount["error"] else 0


if __name__ == "__main__":
    sys.exit(main())
