"""`.ninja_log` 로 리플렉션 생성 TU(`generated/**.gen*.cpp`)가 전체 컴파일 CPU 시간에서 차지하는 비중을 낸다(NEXT 5-4 W-R0 · W-R5 목표 8 % 미만).

  py -3 Scripts/dev/build_time_report.py                       # build/Ninja-Debug
  py -3 Scripts/dev/build_time_report.py --preset Ninja-Release --top 20
  py -3 Scripts/dev/build_time_report.py --preset Ninja-Release --last-build   # 마지막 빌드 한 번의 컴파일 · 생성 · 링크 합
  py -3 Scripts/dev/build_time_report.py --self-check

ninja 로그 v5 · v6 의 줄은 `시작ms 끝ms mtime 출력 해시` 다. 같은 출력이 여러 번 지어졌으면 마지막 줄만 센다(ninja 가 로그를 덧붙이므로).
컴파일 단위는 출력이 `.obj` · `.o` 인 줄이다 — 링크 · 생성(파서 실행) 줄은 비중의 분모에 넣지 않는다.
지금 매니페스트(`build.ninja`)에 없는 출력은 세지 않는다 — 이름을 바꾸거나 지운 파일의 `.obj` 가 디스크에 남으면 ninja 가 로그 줄을 지우지 않는다.
11-18 측정의 Debug 19.6 % 는 이 옛 출력(`RigNodeFields.gen.cpp.obj` 등, 지금 이름은 `…Params`)이 분자 · 분모에 섞인 값이었다(빼면 8.6 %).

`--last-build` 는 그 빌드 폴더의 **마지막 ninja 실행 한 번**의 합(컴파일 · PCH · 생성 · 링크 CPU 초와 벽시계)을 낸다 — 묶음 검증이 빌드마다 기록할 값
(`summarize_last_build` 를 부른다, 설계 백로그 CMake ① — 고정 변경 네 가지 기준선은 요청할 때 돌리는 명령이 이 함수를 쓴다).
실행 경계는 ninja 가 시작 · 끝 시각을 실행 시작 기준으로 적는 것으로 가른다 — 끝 시각이 앞 줄보다 작아지면 새 실행이다(ninjatracing 과 같은 판정).
종류는 매니페스트의 규칙 이름으로 가른다: `*_COMPILER__*` 컴파일(출력에 `cmake_pch` 가 들면 PCH), `*_LINKER__*` 링크(정적 라이브러리 묶기 포함),
`CUSTOM_COMMAND` 생성. 출력이 여럿인 단계(파서 · 링크의 .dll + .lib)는 로그에 출력마다 줄이 있어 단계 하나로 한 번만 센다.
"""
from __future__ import annotations

import argparse
import dataclasses
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))   # Scripts
from common.paths import get_build_dir  # noqa: E402

GENERATED_MARKER = '/generated/'
OBJECT_SUFFIXES = ('.obj', '.o')


def parse_ninja_log(text: str) -> dict[str, int]:
    """출력 경로 → 마지막으로 지은 시간(ms). 머리 줄이 v5 · v6 이 아니면 멈춘다(형식이 바뀌면 숫자가 조용히 틀린다)."""
    lines = text.splitlines()
    if not lines or lines[0].strip() not in ('# ninja log v5', '# ninja log v6'):
        sys.exit(f'모르는 ninja 로그 머리: {lines[0] if lines else "(빈 파일)"!r}')
    durations: dict[str, int] = {}
    for line in lines[1:]:
        fields = line.split('\t')
        if len(fields) != 5:
            continue
        start, end, _, output, _ = fields
        durations[output.replace('\\', '/')] = int(end) - int(start)
    return durations


_BUILD_OUTPUT = re.compile(r'^build ((?:\$.|[^ :$\n])+)', re.MULTILINE)


def parse_manifest_outputs(text: str) -> set[str]:
    """`build.ninja` 의 `build` 줄마다 첫 출력(ninja 이스케이프 `$:` · `$ ` 를 풀고 `/` 로 맞춘다). 컴파일 줄은 출력이 하나다."""
    return {m.group(1).replace('$:', ':').replace('$ ', ' ').replace('\\', '/') for m in _BUILD_OUTPUT.finditer(text)}


def summarize(durations: dict[str, int], live_outputs: set[str]) -> tuple[int, int, list[tuple[int, str]]]:
    """(전체 컴파일 ms, 생성 TU ms, 생성 TU 의 (ms, 출력) 내림차순). @p live_outputs 에 없는 출력(옛 빌드의 찌꺼기)은 뺀다."""
    total = 0
    generated: list[tuple[int, str]] = []
    for output, ms in durations.items():
        if not output.endswith(OBJECT_SUFFIXES) or output not in live_outputs:
            continue
        total += ms
        if GENERATED_MARKER in output:
            generated.append((ms, output))
    generated.sort(reverse=True)
    return total, sum(ms for ms, _ in generated), generated


@dataclasses.dataclass(frozen=True)
class BuildTimes:
    """빌드 한 번의 단계 종류별 CPU 합(초)과 벽시계(초). 단계 수는 센 종류(컴파일 · PCH · 생성 · 링크)만이다."""
    compile_seconds: float
    pch_seconds: float
    generate_seconds: float
    link_seconds: float
    wall_seconds: float
    step_count: int

    def format_line(self, name: str) -> str:
        return (f'{name}: 컴파일 {self.compile_seconds:.1f} s · PCH {self.pch_seconds:.1f} s · 생성 {self.generate_seconds:.1f} s · '
                f'링크 {self.link_seconds:.1f} s(CPU 합), 벽시계 {self.wall_seconds:.1f} s, 단계 {self.step_count} 개')


def _split_unescaped(text: str, separator: str) -> list[str]:
    """ninja 이스케이프(`$` 다음 글자는 글자 그대로 — `$:` · `$ ` · `$$`)를 풀며 `separator` 로 가른다."""
    parts: list[str] = ['']
    index = 0
    while index < len(text):
        char = text[index]
        if char == '$' and index + 1 < len(text):
            parts[-1] += text[index + 1]
            index += 2
            continue
        if char == separator:
            parts.append('')
        else:
            parts[-1] += char
        index += 1
    return parts


def parse_manifest_edges(text: str) -> dict[str, tuple[int, str]]:
    """`build.ninja` 의 출력(명시 · 암시, `/` 로 맞춤) → (단계 번호, 규칙 이름). `$` 줄 이음을 먼저 잇는다."""
    joined = re.sub(r'\$\r?\n[ \t]*', '', text)
    edges: dict[str, tuple[int, str]] = {}
    for number, line in enumerate(joined.splitlines()):
        if not line.startswith('build '):
            continue
        # 출력 목록과 규칙을 이스케이프되지 않은 첫 `:` 로 가른다(`D$:` 는 경로의 드라이브 글자). 출력 사이는 이스케이프되지 않은 공백이다.
        head = line[len('build '):]
        colon = 0
        while colon < len(head) and head[colon] != ':':
            colon += 2 if head[colon] == '$' else 1
        if colon >= len(head):
            continue
        rule = head[colon + 1:].strip().split(' ')[0]
        for output in _split_unescaped(head[:colon], ' '):
            if output and output != '|':
                edges[output.replace('\\', '/')] = (number, rule)
    return edges


def split_last_run(text: str) -> list[tuple[int, int, str]]:
    """`.ninja_log` 에서 마지막 ninja 실행의 (시작 ms, 끝 ms, 출력) 줄들. 끝 시각이 앞 줄보다 작아지면 새 실행이 시작된 것이다."""
    lines = text.splitlines()
    if not lines or lines[0].strip() not in ('# ninja log v5', '# ninja log v6'):
        sys.exit(f'모르는 ninja 로그 머리: {lines[0] if lines else "(빈 파일)"!r}')
    run: list[tuple[int, int, str]] = []
    previous_end = -1
    for line in lines[1:]:
        fields = line.split('\t')
        if len(fields) != 5:
            continue
        start, end = int(fields[0]), int(fields[1])
        if end < previous_end:
            run = []
        previous_end = end
        run.append((start, end, fields[3].replace('\\', '/')))
    return run


def summarize_run(run: list[tuple[int, int, str]], edges: dict[str, tuple[int, str]]) -> BuildTimes:
    """실행 한 번의 줄들을 단계 종류별로 더한다. 매니페스트에 없는 출력은 뺀다. 같은 단계의 출력 여러 줄은 한 번만 센다."""
    seen: set[int] = set()
    totals = {'compile': 0, 'pch': 0, 'generate': 0, 'link': 0}
    first_start: int | None = None
    last_end = 0
    for start, end, output in run:
        edge = edges.get(output)
        if edge is None or edge[0] in seen:
            continue
        number, rule = edge
        if '_COMPILER__' in rule:
            kind = 'pch' if 'cmake_pch' in output else 'compile'
        elif '_LINKER__' in rule:
            kind = 'link'
        elif rule == 'CUSTOM_COMMAND':
            kind = 'generate'
        else:
            continue
        seen.add(number)
        totals[kind] += end - start
        first_start = start if first_start is None else min(first_start, start)
        last_end = max(last_end, end)
    wall = (last_end - first_start) if first_start is not None else 0
    return BuildTimes(totals['compile'] / 1000, totals['pch'] / 1000, totals['generate'] / 1000, totals['link'] / 1000, wall / 1000, len(seen))


def summarize_last_build(build_dir: pathlib.Path) -> BuildTimes:
    """빌드 폴더의 마지막 ninja 실행 한 번의 단계 종류별 합 — 묶음 검증(preflight)이 빌드마다 부를 함수. 로그 · 매니페스트가 없으면 멈춘다."""
    log_path = build_dir / '.ninja_log'
    manifest_path = build_dir / 'build.ninja'
    if not log_path.is_file() or not manifest_path.is_file():
        sys.exit(f'{log_path} 나 {manifest_path} 가 없다 — 그 프리셋을 먼저 짓는다')
    edges = parse_manifest_edges(manifest_path.read_text(encoding='utf-8', errors='replace'))
    return summarize_run(split_last_run(log_path.read_text(encoding='utf-8', errors='replace')), edges)


def self_check() -> None:
    log = '\n'.join([
        '# ninja log v6',
        '0\t100\t1\tSource/Engine/CMakeFiles/Engine.dir/Foo.cpp.obj\th',
        '0\t50\t1\tSource/Engine/CMakeFiles/Engine.dir/__/__/generated/Engine/Foo.gen.cpp.obj\th',
        '0\t70\t2\tSource/Engine/CMakeFiles/Engine.dir/__/__/generated/Engine/Foo.gen.cpp.obj\th',   # 다시 지었다 — 마지막 것
        '0\t999\t1\tgenerated/Engine/Foo.gen.cpp\th',                                                # 파서 실행 — 분모 밖
        '0\t30\t1\tbin/Engine.dll\th',                                                               # 링크 — 분모 밖
        '0\t900\t1\tSource/Engine/CMakeFiles/Engine.dir/__/__/generated/Engine/Old.gen.cpp.obj\th',   # 매니페스트에 없다(옛 이름) — 뺀다
    ])
    manifest = '\n'.join([
        'build Source\\Engine\\CMakeFiles\\Engine.dir\\Foo.cpp.obj: CXX D$:\\x\\Foo.cpp',
        'build Source\\Engine\\CMakeFiles\\Engine.dir\\__\\__\\generated\\Engine\\Foo.gen.cpp.obj: CXX D$:\\x\\Foo.gen.cpp',
        'build bin/Engine.dll: LINK',
    ])
    total, generated_ms, list_generated = summarize(parse_ninja_log(log), parse_manifest_outputs(manifest))
    assert (total, generated_ms, len(list_generated)) == (170, 70, 1), (total, generated_ms, list_generated)
    run_log = '\n'.join([
        '# ninja log v6',
        '0\t900\t1\tSource/Engine/CMakeFiles/Engine.dir/Foo.cpp.obj\th',          # 앞 실행 — 세지 않는다
        '0\t40\t1\tSource/Engine/CMakeFiles/Engine.dir/cmake_pch.cxx.obj\th',     # 끝 시각이 줄었다 — 새 실행
        '40\t140\t1\tSource/Engine/CMakeFiles/Engine.dir/Foo.cpp.obj\th',
        '10\t210\t1\tgenerated/Engine/Foo.gen.cpp\th',
        '10\t210\t1\tgenerated/Engine/Foo.gen.h\th',                              # 같은 단계의 둘째 출력
        '210\t260\t1\tBin/Engine.dll\th',
        '210\t260\t1\tLib/Engine.lib\th',
        '260\t270\t1\tD:/x/Bin/Modules/ModuleBuild.stamp\th',
        '270\t280\t1\tgone.obj\th',                                               # 매니페스트에 없다
    ])
    run_manifest = '\n'.join([
        'build Source\\Engine\\CMakeFiles\\Engine.dir\\cmake_pch.cxx.obj Source\\Engine\\CMakeFiles\\Engine.dir\\cmake_pch.cxx.pch: CXX_COMPILER__Engine_Release x',
        'build Source\\Engine\\CMakeFiles\\Engine.dir\\Foo.cpp.obj: CXX_COMPILER__Engine_Release D$:\\x\\Foo.cpp',
        'build generated\\Engine\\Foo.gen.cpp generated\\Engine\\Foo.gen.h | generated\\Engine\\Foo.stamp: CUSTOM_COMMAND $',
        '    D$:\\x\\Foo.h',
        'build Bin\\Engine.dll Lib\\Engine.lib: CXX_SHARED_LIBRARY_LINKER__Engine_Release a.obj',
        'build D$:/x/Bin/Modules/ModuleBuild.stamp: CUSTOM_COMMAND b',
        'build all: phony Bin\\Engine.dll',
    ])
    times = summarize_run(split_last_run(run_log), parse_manifest_edges(run_manifest))
    assert times == BuildTimes(0.1, 0.04, 0.21, 0.05, 0.27, 5), times
    print('self-check ok')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--preset', default='Ninja-Debug', help='build/<preset>/.ninja_log')
    parser.add_argument('--top', type=int, default=10, help='가장 오래 걸린 생성 TU 몇 개를 보일지')
    parser.add_argument('--last-build', action='store_true', help='마지막 ninja 실행 한 번의 컴파일 · PCH · 생성 · 링크 합을 낸다')
    parser.add_argument('--self-check', action='store_true')
    args = parser.parse_args()
    if args.self_check:
        self_check()
        return 0
    if args.last_build:
        print(summarize_last_build(get_build_dir(args.preset)).format_line(args.preset))
        return 0
    log_path = get_build_dir(args.preset) / '.ninja_log'
    manifest_path = log_path.with_name('build.ninja')
    if not log_path.is_file() or not manifest_path.is_file():
        sys.exit(f'{log_path} 나 {manifest_path} 가 없다 — 그 프리셋을 먼저 짓는다')
    live_outputs = parse_manifest_outputs(manifest_path.read_text(encoding='utf-8', errors='replace'))
    total, generated_ms, list_generated = summarize(parse_ninja_log(log_path.read_text(encoding='utf-8')), live_outputs)
    if total == 0:
        sys.exit(f'{log_path} 에 컴파일 줄이 없다')
    print(f'{args.preset}: 컴파일 {total / 1000:.1f} s(CPU 합), 생성 TU {len(list_generated)} 개 {generated_ms / 1000:.1f} s = {100.0 * generated_ms / total:.1f} %')
    for ms, output in list_generated[:args.top]:
        print(f'  {ms / 1000:7.2f} s  {output}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
