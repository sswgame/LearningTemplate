"""`.ninja_log` 로 리플렉션 생성 TU(`generated/**.gen*.cpp`)가 전체 컴파일 CPU 시간에서 차지하는 비중을 낸다(NEXT 5-4 W-R0 · W-R5 목표 8 % 미만).

  py -3 Scripts/dev/build_time_report.py                       # build/Ninja-Debug
  py -3 Scripts/dev/build_time_report.py --preset Ninja-Release --top 20
  py -3 Scripts/dev/build_time_report.py --self-check

ninja 로그 v5 · v6 의 줄은 `시작ms 끝ms mtime 출력 해시` 다. 같은 출력이 여러 번 지어졌으면 마지막 줄만 센다(ninja 가 로그를 덧붙이므로).
컴파일 단위는 출력이 `.obj` · `.o` 인 줄이다 — 링크 · 생성(파서 실행) 줄은 비중의 분모에 넣지 않는다.
"""
from __future__ import annotations

import argparse
import pathlib
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
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


def summarize(durations: dict[str, int]) -> tuple[int, int, list[tuple[int, str]]]:
    """(전체 컴파일 ms, 생성 TU ms, 생성 TU 의 (ms, 출력) 내림차순)."""
    total = 0
    generated: list[tuple[int, str]] = []
    for output, ms in durations.items():
        if not output.endswith(OBJECT_SUFFIXES):
            continue
        total += ms
        if GENERATED_MARKER in output:
            generated.append((ms, output))
    generated.sort(reverse=True)
    return total, sum(ms for ms, _ in generated), generated


def self_check() -> None:
    log = '\n'.join([
        '# ninja log v6',
        '0\t100\t1\tSource/Engine/CMakeFiles/Engine.dir/Foo.cpp.obj\th',
        '0\t50\t1\tSource/Engine/CMakeFiles/Engine.dir/__/__/generated/Engine/Foo.gen.cpp.obj\th',
        '0\t70\t2\tSource/Engine/CMakeFiles/Engine.dir/__/__/generated/Engine/Foo.gen.cpp.obj\th',   # 다시 지었다 — 마지막 것
        '0\t999\t1\tgenerated/Engine/Foo.gen.cpp\th',                                                # 파서 실행 — 분모 밖
        '0\t30\t1\tbin/Engine.dll\th',                                                               # 링크 — 분모 밖
    ])
    total, generated_ms, list_generated = summarize(parse_ninja_log(log))
    assert (total, generated_ms, len(list_generated)) == (170, 70, 1), (total, generated_ms, list_generated)
    print('self-check ok')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--preset', default='Ninja-Debug', help='build/<preset>/.ninja_log')
    parser.add_argument('--top', type=int, default=10, help='가장 오래 걸린 생성 TU 몇 개를 보일지')
    parser.add_argument('--self-check', action='store_true')
    args = parser.parse_args()
    if args.self_check:
        self_check()
        return 0
    log_path = REPO / 'build' / args.preset / '.ninja_log'
    if not log_path.is_file():
        sys.exit(f'{log_path} 가 없다 — 그 프리셋을 먼저 짓는다')
    total, generated_ms, list_generated = summarize(parse_ninja_log(log_path.read_text(encoding='utf-8')))
    if total == 0:
        sys.exit(f'{log_path} 에 컴파일 줄이 없다')
    print(f'{args.preset}: 컴파일 {total / 1000:.1f} s(CPU 합), 생성 TU {len(list_generated)} 개 {generated_ms / 1000:.1f} s = {100.0 * generated_ms / total:.1f} %')
    for ms, output in list_generated[:args.top]:
        print(f'  {ms / 1000:7.2f} s  {output}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
