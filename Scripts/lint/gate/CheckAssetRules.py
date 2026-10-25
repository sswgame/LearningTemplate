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
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from lint.LintGate import GateError, GateResult, LintGate  # noqa: E402

from common.ExitCode import ExitCode  # noqa: E402
from common.AssetValidation import RuleConfigError, kAssetRuleConfigRelative, loadRules, validate  # noqa: E402

#: 규칙 표(저장소 루트 기준).
_kRulePath = kAssetRuleConfigRelative


def selfTestRulesInternal(ruleName: str) -> str:
    """
    셀프 테스트의 규칙 표 — 저장소 규칙 표에서 `ruleName` 한 줄만 고르고 예외(`exclude_*`)를 지운다.
    형식은 저장소 표를 따르므로 손으로 베낀 표처럼 형식이 바뀌는 날 읽기 오류(종료 2)로 죽지 않는다. 표를 통째로 쓰지 않는 까닭은
    조각 트리에 없는 예외 자리가 "낡은 예외" 위반을 내 조각이 노린 위반 없이도 지기 때문이다. 이름이 없으면 빈 표라 조각이 통과해 음성 테스트가 잡는다.
    """
    root = json.loads((Path(__file__).resolve().parents[3] / _kRulePath).read_text(encoding="utf-8"))
    listRule = [{key: value for key, value in rule.items() if not key.startswith("exclude_")} for rule in root["rules"] if rule["name"] == ruleName]
    return json.dumps({"rules": listRule}, ensure_ascii=False)


class CheckAssetRulesGate(LintGate):
    """`selfTestCases` 는 이 게이트가 **반드시 잡아야 하는** 조각이다. 규칙은 저장소 표에서 고른다(`selfTestRulesInternal`)."""

    description = "에셋 검증 규칙(Config/Editor/AssetValidationRules.json)의 오류 심각도"
    buildComment = "Checking Resource/ against the asset validation rules..."
    timeoutSeconds = 60
    preCommitPattern = ("Resource/*", kAssetRuleConfigRelative, "Scripts/common/AssetValidation.py")
    preCommitFileArgument = "--files"
    violationHeader = "에셋 검증 규칙 위반"
    hint = "  `py -3 -m Scripts validate-assets` 로 경고까지 본다. 규칙은 Config/Editor/AssetValidationRules.json 에 있다."
    selfTestCases = [
        {
            "name": "없는 메시를 가리키는 씬",
            "expectedViolation": "Resource/game/probe/maps/a.scene.xml: [references-exist] MeshComponent@_meshID points at 'game/probe/models/missing.mesh', which does not ex",
            "files": {
                _kRulePath: selfTestRulesInternal("references-exist"),
                "Resource/game/probe/maps/a.scene.xml": (
                    '<Scene><entities><entity id="1" name="A"><GameObject><_listComponent>'
                    '<MeshComponent _meshID="game/probe/models/missing.mesh"/></_listComponent></GameObject></entity></entities></Scene>'
                ),
            },
        },
        {
            "name": "겹친 엔티티 id",
            "expectedViolation": "Resource/game/probe/maps/b.scene.xml: [scene-entity-ids] entity id 2 is used twice",
            "files": {
                _kRulePath: selfTestRulesInternal("scene-entity-ids"),
                "Resource/game/probe/maps/b.scene.xml": '<Scene><entities><entity id="2" name="A"/><entity id="2" name="B"/></entities></Scene>',
            },
        },
        {
            "name": "Resource 폴더가 없으면 불성립(통과가 아니다)",
            "expectedViolation": "UNAVAILABLE (Resource 폴더 없음",
            "expectedExitCode": ExitCode.PrerequisiteMissing,
            "files": {_kRulePath: selfTestRulesInternal("references-exist")},
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
            return GateResult.unavailable("Resource 폴더 없음 — 검증할 에셋이 없다(--root 가 저장소인지 본다)")   # 통과가 아니다: 에셋을 하나도 보지 않았다

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
