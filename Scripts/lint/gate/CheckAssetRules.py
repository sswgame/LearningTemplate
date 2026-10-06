#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckAssetRules.py

에셋 검증 규칙(`Config/Editor/AssetValidationRules.json`) 가운데 **오류(error) 심각도**를 게이트로 돌립니다.

규칙과 검사는 `Scripts/common/AssetValidation.py` 한 자리에 있고, 이 게이트 · `py -3 -m Scripts validate-assets` ·
에디터의 저장 · 임포트 직후 검증이 같은 코드를 부른다. 경고(고아 파일 · 죽은 퍼뮤테이션 · 소품 삼각형 예산)는 막지 않고
노트로만 찍는다 — 판단은 사람이 한다. 모든 검사가 텍스트 · 파일 머리 수준이라 빌드 없이 돌아 커밋 훅에서도 돈다.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from LintGate import GateError, GateResult, LintGate  # noqa: E402

from common.AssetValidation import RuleConfigError, loadRules, validate  # noqa: E402

#: 규칙 표(저장소 루트 기준).
_kRulePath = "Config/Editor/AssetValidationRules.json"

#: 자기 시험이 쓰는 최소 규칙 표 — 시험 트리에는 저장소의 규칙 표가 없다.
_kSelfTestRules = (
    '{"rules": ['
    '{"name": "references-exist", "check": "missing_reference", "include_patterns": ["*.xml"]},'
    '{"name": "scene-entity-ids", "check": "entity_id", "include_patterns": ["*.scene.xml"]},'
    '{"name": "orphans", "check": "orphan", "severity": "warning", "include_patterns": ["*.mesh"]}'
    ']}'
)


class CheckAssetRulesGate(LintGate):
    """`selfTestCases` 는 이 게이트가 **반드시 잡아야 하는** 조각이다."""

    description = "에셋 검증 규칙(Config/Editor/AssetValidationRules.json)의 오류 심각도"
    buildComment = "Checking Resource/ against the asset validation rules..."
    timeoutSeconds = 60
    preCommitPattern = ("Resource/*", "Config/Editor/AssetValidationRules.json", "Scripts/common/AssetValidation.py")
    preCommitFileArgument = "--files"
    violationHeader = "에셋 검증 규칙 위반"
    hint = "  `py -3 -m Scripts validate-assets` 로 경고까지 본다. 규칙은 Config/Editor/AssetValidationRules.json 에 있다."
    selfTestCases = [
        {
            "name": "없는 메시를 가리키는 씬",
            "files": {
                _kRulePath: _kSelfTestRules,
                "Resource/game/probe/maps/a.scene.xml": (
                    '<Scene><entities><entity id="1" name="A"><GameObject><_listComponent>'
                    '<MeshComponent _meshId="game/probe/models/missing.mesh"/></_listComponent></GameObject></entity></entities></Scene>'
                ),
            },
        },
        {
            "name": "겹친 엔티티 id",
            "files": {
                _kRulePath: _kSelfTestRules,
                "Resource/game/probe/maps/b.scene.xml": '<Scene><entities><entity id="2" name="A"/><entity id="2" name="B"/></entities></Scene>',
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        try:
            listRule = loadRules(repositoryRoot / _kRulePath)
        except RuleConfigError as error:
            raise GateError(str(error)) from error
        resourceRoot = repositoryRoot / "Resource"
        if not resourceRoot.is_dir():
            return GateResult(summary="no Resource/ folder")

        listTarget = None
        if args.files:
            listTarget = []
            for filePath in args.files:
                path = Path(filePath)
                path = path if path.is_absolute() else repositoryRoot / path
                try:
                    listTarget.append(path.resolve().relative_to(resourceRoot.resolve()).as_posix())
                except ValueError:
                    continue  # 규칙 표 · 검증 코드가 바뀐 커밋 — 리소스가 아닌 파일은 아래에서 트리 전체를 본다
            if not listTarget:
                listTarget = None

        listFinding = validate(listRule, resourceRoot, repositoryRoot, listTarget, "warning")
        listViolation = [f"Resource/{finding.path}: [{finding.rule}] {finding.message}" for finding in listFinding if finding.severity == "error"]
        listNote = [f"Resource/{finding.path}: [{finding.rule}] {finding.message}" for finding in listFinding if finding.severity == "warning"]
        scope = f"{len(listTarget)} file(s)" if listTarget is not None else "Resource/"
        return GateResult(listViolation=listViolation, listNote=listNote, summary=f"{scope}, {len(listRule)} rules, {len(listNote)} warning(s)")


main = CheckAssetRulesGate.run


if __name__ == "__main__":
    sys.exit(main())
