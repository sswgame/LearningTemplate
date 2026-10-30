"""기능 폴더별 테스트 전수표(py -3 Scripts/dev/TestCoverage.py [--uncovered] [접두어…]) — 손으로 적은 목록 대신 이 표가 남은 일의 정본이다.

기능 폴더 = 직속 .cpp 가 있는 폴더(`Tests/` 제외). 칸:
- cpp     : 직속 .cpp 수(테스트 제외)
- unit    : 이 폴더 또는 그 아래 `Tests/` 의 `SW_TEST_CASE` 수 — 같은 이름 형제 `Tests/` 도 센다
- scen    : 이 폴더가 등록한 개발 명령 · 감시 값을 쓰거나 이 폴더의 타입 이름(8 자 이상)이 나오는 시나리오 파일 수(주석 제외)
- 상태    : none(둘 다 없음) · unit · scen · both
시나리오의 이름 대조는 거칠다 — 이름이 나온다고 동작을 검증한다는 뜻은 아니다(패널을 열어 찍기만 하는 감사도 센다).
"""
import collections, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'Source'
args = [a for a in sys.argv[1:] if not a.startswith('--')]
only_uncovered = '--uncovered' in sys.argv


def read(path):
    return path.read_text(encoding='utf-8', errors='ignore')


scenarios = {p: re.sub(r'<!--.*?-->', '', read(p), flags=re.S) for p in (ROOT / 'Content').rglob('*.scenario.xml')}

folders = collections.defaultdict(list)
tests = collections.Counter()
for cpp in SOURCE.rglob('*.cpp'):
    if 'graphify-out' in cpp.parts or 'ThirdParty' in cpp.parts:
        continue
    if 'Tests' in cpp.parts:
        owner = pathlib.Path(*cpp.parts[:cpp.parts.index('Tests')])
        tests[owner] += len(re.findall(r'\bSW_TEST_CASE\s*\(', read(cpp)))
    else:
        folders[cpp.parent].append(cpp)

rows = []
for folder, cpps in folders.items():
    rel = folder.relative_to(SOURCE).as_posix()
    if args and not any(rel.startswith(a) for a in args):
        continue
    unit = sum(n for owner, n in tests.items() if folder == owner or owner in folder.parents)
    names, texts = set(), []
    for f in cpps + list(folder.glob('*.h')):
        text = read(f)
        texts.append(text)
        names |= {m for m in re.findall(r'(?:class|struct)\s+(?:SW_\w+_API\s+)?([A-Z]\w{7,})', text)}
    joined = '\n'.join(texts)
    commands = set(re.findall(r'SW_DEV_COMMAND\(\s*\w+\s*,\s*"([^"]+)"', joined))
    watches = set(re.findall(r'SW_AUTOMATION_WATCH\w*\(\s*\w+\s*,\s*"([^"]+)"', joined))
    scen = 0
    for body in scenarios.values():
        if any(re.search(r'line="' + re.escape(c) + r'[\s"]', body) for c in commands) or any('"' + w + '"' in body for w in watches) \
                or any(re.search(r'\b' + n + r'\b', body) for n in names):
            scen += 1
    state = 'both' if unit and scen else 'unit' if unit else 'scen' if scen else 'none'
    if only_uncovered and state != 'none':
        continue
    rows.append((rel, len(cpps), unit, scen, state))

rows.sort()
print(f'| 기능 폴더 | cpp | unit | scen | 상태 |\n|---|---|---|---|---|')
for r in rows:
    print(f'| {r[0]} | {r[1]} | {r[2]} | {r[3]} | {r[4]} |')
total = collections.Counter(r[4] for r in rows)
print(f'\n폴더 {len(rows)} — ' + ' · '.join(f'{k} {total[k]}' for k in ('none', 'unit', 'scen', 'both')) +
      f' · 테스트 없는 cpp {sum(r[1] for r in rows if r[4] == "none")}')
