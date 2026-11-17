"""
사진 앞 정착을 사진 단계로 옮기는 시나리오 전수 이전(DesignBacklog "시나리오 · 테스트 프레임워크 개편 판단" D1 · ST-3).

  py -3 -m Scripts migrate-screenshot-stability          # 바꿀 줄 수만 찍는다(아무것도 쓰지 않는다)
  py -3 -m Scripts migrate-screenshot-stability --fix    # 바꿔 쓴다
  py -3 -m Scripts migrate-screenshot-stability --self-check

옛 꼴: 사진마다 앞에 `<WaitUntil watch="Render.Stable" equals="1" timeoutWallSeconds="30"/>`(합성 0/1 감시 값 — 사라졌다)를 적고, 그 프레임을 바로 찍을 사진은
`<Screenshot waitForStability="false">` 를 적었다. 새 꼴은 사진 단계가 정착을 맡는다(`stability="render|assets|none"`, 기본 `render`). 규칙(동작 보존 — 같은 프레임에 찍힌다):

- 렌더 정착 대기(`Render.Stable` · `Render.StableIgnoringMotion`) 바로 뒤 단계가 `waitForStability` 없는 `<Screenshot>` 이면 대기 줄을 지운다 — 그 사진은 기본 `render` 다.
  (사진 단계가 정착이 선 프레임을 하나 붙잡은 뒤 덮개 · 뒤 프레임을 센다 — 옛 대기 단계가 제 프레임을 혼자 갖고 사진이 다음 프레임에 돌던 것과 같다.)
- 그 밖의 렌더 정착 대기(사진이 아닌 단계 앞 · 프레임이 정해진 사진 앞)는 `<WaitRenderStable/>` 로 바꾼다(같은 조건 · 같은 시한, 줄 끝 주석은 둔다).
- `waitForStability="false"` → `stability="none"`.
- 바로 앞 단계가 렌더 정착 대기가 아니던 `<Screenshot>` → `stability="assets"`(옛 사진 단계가 기다리던 것 — 에셋 일 · 화면 덮개 · 뒤 프레임).

앞 · 뒤 단계는 주석 · 빈 줄을 건너뛴 단계 원소로 본다. 한 줄에 원소 하나인 꼴만 다룬다 — 아니면 멈춘다(짐작해 고치지 않는다).
"""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass

from common.exit_code import ExitCode
from common.git import run_git_or_raise
from common.paths import get_project_root

STABLE_WAIT = re.compile(r'^(?P<indent>\s*)<WaitUntil\s+watch="Render\.Stable(?:IgnoringMotion)?"\s+equals="1"\s+timeoutWallSeconds="30"\s*/>(?P<tail>.*)$')
ANY_OLD_REFERENCE = re.compile(r'Render\.Stable\b|Render\.StableIgnoringMotion|waitForStability')
SCREENSHOT = re.compile(r'^(?P<head>\s*<Screenshot\b)(?P<body>[^>]*?)(?P<close>\s*/>)(?P<tail>.*)$')
ELEMENT_START = re.compile(r'^\s*<(?P<name>[A-Za-z][\w.]*)\b')
WAIT_FOR_STABILITY_FALSE = re.compile(r'\s+waitForStability="false"')
REPLACEMENT_WAIT = "<WaitRenderStable/>"


@dataclass
class MigrationResult:
    text: str
    removed_waits: int = 0
    replaced_waits: int = 0
    none_screenshots: int = 0
    assets_screenshots: int = 0
    render_screenshots: int = 0


def _step_lines(lines: list[str]) -> list[int]:
    """단계 원소가 시작하는 줄 번호(주석 · 빈 줄 · XML 머리 · 닫는 태그를 뺀다)입니다. 여러 줄 주석 안의 줄도 뺀다."""
    list_index: list[int] = []
    in_comment = False
    for index, line in enumerate(lines):
        stripped = line.strip()
        if in_comment:
            if "-->" in stripped:
                in_comment = False
            continue
        if stripped.startswith("<!--"):
            in_comment = "-->" not in stripped
            continue
        if stripped.startswith("<?") or stripped.startswith("</") or not stripped.startswith("<"):
            continue
        list_index.append(index)
    return list_index


def migrate_text(text: str, source_name: str) -> MigrationResult:
    """시나리오 글 하나를 옮깁니다. 다룰 수 없는 꼴(한 줄에 끝나지 않는 대기 · 사진, 30 초가 아닌 정착 대기)이면 ValueError 입니다."""
    lines = text.split("\n")
    result = MigrationResult(text)
    step_indices = _step_lines(lines)
    is_settle_wait = {index: STABLE_WAIT.match(lines[index]) is not None for index in step_indices}
    for index in step_indices:
        if 'watch="Render.Stable' in lines[index].split("<!--")[0] and not is_settle_wait[index]:
            raise ValueError(f"{source_name}:{index + 1}: a Render.Stable wait this tool does not know the shape of: {lines[index].strip()}")
        if lines[index].lstrip().startswith("<Screenshot") and SCREENSHOT.match(lines[index]) is None:
            raise ValueError(f"{source_name}:{index + 1}: a <Screenshot> that does not end on its line")

    remove: set[int] = set()
    for position, index in enumerate(step_indices):
        line = lines[index]
        if is_settle_wait[index]:
            next_index = step_indices[position + 1] if position + 1 < len(step_indices) else None
            next_line = lines[next_index] if next_index is not None else ""
            next_is_settled_screenshot = next_line.lstrip().startswith("<Screenshot") and "waitForStability" not in next_line
            if next_is_settled_screenshot:
                remove.add(index)
                result.removed_waits += 1
            else:
                match = STABLE_WAIT.match(line)
                lines[index] = f"{match.group('indent')}{REPLACEMENT_WAIT}{match.group('tail')}"
                result.replaced_waits += 1
            continue
        match = SCREENSHOT.match(line)
        if match is None:
            continue
        body = match.group("body")
        if 'waitForStability="true"' in body:
            raise ValueError(f"{source_name}:{index + 1}: waitForStability=\"true\" is not expected")
        if WAIT_FOR_STABILITY_FALSE.search(body):
            body = WAIT_FOR_STABILITY_FALSE.sub("", body) + ' stability="none"'
            result.none_screenshots += 1
        else:
            previous_index = step_indices[position - 1] if position > 0 else None
            if previous_index is not None and is_settle_wait[previous_index]:
                result.render_screenshots += 1
                continue
            body = body + ' stability="assets"'
            result.assets_screenshots += 1
        lines[index] = f"{match.group('head')}{body}{match.group('close')}{match.group('tail')}"
    result.text = "\n".join(line for index, line in enumerate(lines) if index not in remove)
    return result


NEW_FORM = re.compile(r'\bstability="|<WaitRenderStable\b')


def is_new_form(text: str) -> bool:
    """새 꼴(`stability=` · `<WaitRenderStable/>`)이 든 글이면 true 입니다 — 이 도구는 옛 꼴에 한 번만 돈다."""
    return NEW_FORM.search(text) is not None


def list_scenario_files(root) -> list[str]:
    output = run_git_or_raise(["ls-files", "*.scenario.xml"], cwd=root)
    return [line for line in output.split("\n") if line]


def self_check() -> None:
    old = "\n".join([
        '<Scenario name="x">',
        '\t<WaitUntil watch="Render.Stable" equals="1" timeoutWallSeconds="30"/> <!-- 사진 앞 -->',
        '\t<!-- 주석은 건너뛴다 -->',
        '\t<Screenshot file="a.ppm"/>',
        '\t<Screenshot file="b.ppm"/>',
        '\t<WaitUntil watch="Render.StableIgnoringMotion" equals="1" timeoutWallSeconds="30"/> <!-- 장치 -->',
        '\t<DevCommand line="x"/>',
        '\t<WaitUntil watch="Render.Stable" equals="1" timeoutWallSeconds="30"/>',
        '\t<Screenshot file="c.ppm" waitForStability="false"/> <!-- 그 프레임 -->',
        '\t<Screenshot file="d.ppm" waitForStability="false"/>',
        '\t<Pass/>',
        '</Scenario>',
    ])
    expected = "\n".join([
        '<Scenario name="x">',
        '\t<!-- 주석은 건너뛴다 -->',
        '\t<Screenshot file="a.ppm"/>',
        '\t<Screenshot file="b.ppm" stability="assets"/>',
        '\t<WaitRenderStable/> <!-- 장치 -->',
        '\t<DevCommand line="x"/>',
        '\t<WaitRenderStable/>',
        '\t<Screenshot file="c.ppm" stability="none"/> <!-- 그 프레임 -->',
        '\t<Screenshot file="d.ppm" stability="none"/>',
        '\t<Pass/>',
        '</Scenario>',
    ])
    result = migrate_text(old, "self-check")
    assert result.text == expected, result.text
    assert (result.removed_waits, result.replaced_waits, result.render_screenshots, result.assets_screenshots, result.none_screenshots) == (1, 2, 1, 1, 2)
    assert is_new_form(expected) and not is_new_form(old)  # 새 꼴에 다시 돌리지 않는다(기본 render 사진이 옛 꼴과 구별되지 않는다)
    try:
        migrate_text('\t<WaitUntil watch="Render.Stable" equals="1" timeoutWallSeconds="60"/>', "self-check")
        raise AssertionError("an unknown Render.Stable shape must stop the tool")
    except ValueError:
        pass
    print("[migrate-screenshot-stability] self-check ok")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts migrate-screenshot-stability",
                                     description="사진 앞 렌더 정착 대기(Render.Stable)를 사진 단계의 stability 로 옮긴다(--fix 로 쓴다)")
    parser.add_argument("--fix", action="store_true", help="바꿔 쓴다(없으면 찍기만 한다)")
    parser.add_argument("--self-check", action="store_true", help="규칙의 자체 검사만 돈다")
    args = parser.parse_args(argv)
    if args.self_check:
        self_check()
        return ExitCode.PASSED

    root = get_project_root()
    totals = MigrationResult("")
    map_rewritten: dict[str, str] = {}
    list_error: list[str] = []
    list_new_form = [path for path in list_scenario_files(root) if is_new_form((root / path).read_bytes().decode("utf-8"))]
    if list_new_form:
        print(f"[migrate-screenshot-stability] 이미 새 꼴인 파일이 있다 — 한 번만 도는 도구다: {', '.join(list_new_form[:5])}")
        return ExitCode.CHECK_INVALID
    for relative_path in list_scenario_files(root):
        text = (root / relative_path).read_bytes().decode("utf-8")
        try:
            result = migrate_text(text, relative_path)
        except ValueError as error:
            list_error.append(str(error))
            continue
        for name in ("removed_waits", "replaced_waits", "none_screenshots", "assets_screenshots", "render_screenshots"):
            setattr(totals, name, getattr(totals, name) + getattr(result, name))
        if result.text != text:
            map_rewritten[relative_path] = result.text
    for line in list_error:
        print(f"  - {line}")
    print(f"[migrate-screenshot-stability] 파일 {len(map_rewritten)} 개 — 정착 대기 지움 {totals.removed_waits} · <WaitRenderStable/> 로 {totals.replaced_waits}, "
          f"사진 render(기본) {totals.render_screenshots} · assets {totals.assets_screenshots} · none {totals.none_screenshots}, 못 다룬 줄 {len(list_error)}")
    if list_error:
        print("[migrate-screenshot-stability] 못 다룬 줄이 있어 아무것도 쓰지 않았다 — 그 줄을 한 줄 꼴로 고친 뒤 다시 돈다")
        return ExitCode.VIOLATION
    if not args.fix:
        print("[migrate-screenshot-stability] --fix 없이 돌았다 — 아무것도 쓰지 않았다")
        return ExitCode.PASSED
    for relative_path, text in map_rewritten.items():
        (root / relative_path).write_bytes(text.encode("utf-8"))
    left = [path for path in list_scenario_files(root) if ANY_OLD_REFERENCE.search((root / path).read_bytes().decode("utf-8"))]
    if left:
        print(f"[migrate-screenshot-stability] 옛 이름이 남은 파일: {', '.join(left)}")
        return ExitCode.VIOLATION
    return ExitCode.PASSED
