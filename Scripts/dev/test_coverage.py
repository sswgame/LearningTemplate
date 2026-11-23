"""기능 폴더별 테스트 전수표(py -3 Scripts/dev/test_coverage.py [--uncovered] [접두어…]) — 손으로 적은 목록 대신 이 표가 남은 일의 정본이다.

기능 폴더 = 직속 .cpp 가 있는 폴더(`Tests/` 제외). 칸:
- cpp     : 직속 .cpp 수(테스트 제외)
- unit    : 이 폴더 또는 그 아래 `Tests/` 의 `SW_TEST_CASE` 수 — 같은 이름 형제 `Tests/` 도 센다. 없으면 이 폴더가 정의한 타입 이름(8 자 이상)이 나오는 다른 폴더의 테스트 파일 수
- scen    : 이 폴더가 등록한 개발 명령 · 감시 값을 쓰거나 이 폴더가 정의한 타입 이름(8 자 이상)이 나오는 시나리오 파일 수(주석 제외)
- 상태    : none(둘 다 없음) · unit · scen · both
시나리오의 이름 대조는 거칠다 — 이름이 나온다고 동작을 검증한다는 뜻은 아니다(패널을 열어 찍기만 하는 감사도 센다).
"""
import collections, pathlib, re, sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))   # Scripts
import common  # noqa: F401,E402 — 출력 인코딩을 UTF-8 로 맞춘다(cp949 콘솔에서 표의 '—' 가 UnicodeEncodeError 로 멈췄다)

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'Source'
args = [a for a in sys.argv[1:] if not a.startswith('--')]
only_uncovered = '--uncovered' in sys.argv


def read(path):
    return path.read_text(encoding='utf-8', errors='ignore')


# 시나리오마다 (낱말, 개발 명령 이름, 감시 값 이름)의 집합 — 이름마다 본문을 다시 훑으면 폴더 × 이름 × 시나리오라 몇 분이 걸린다
scenarios = []
for path in (ROOT / 'Content').rglob('*.scenario.xml'):
    body = re.sub(r'<!--.*?-->', '', read(path), flags=re.S)
    scenarios.append((set(re.findall(r'\w+', body)), set(re.findall(r'\bline="([^\s"]+)', body)), set(re.findall(r'\bwatch="([^"]+)"', body))))

folders = collections.defaultdict(list)
tests = collections.Counter()
test_words = []   # 테스트 파일마다 낱말 집합 — 테스트가 다른 폴더에 있는 타입을 "없음" 으로 세지 않는다
for cpp in SOURCE.rglob('*.cpp'):
    if 'ThirdParty' in cpp.parts:
        continue
    if 'Tests' in cpp.parts:
        owner = pathlib.Path(*cpp.parts[:cpp.parts.index('Tests')])
        text = read(cpp)
        tests[owner] += len(re.findall(r'\bSW_TEST_CASE(?:_F|_EACH)?\s*\(', text))
        test_words.append(set(re.findall(r'\w+', text)))
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
        # 정의만 센다 — 앞으로 선언까지 세면 남의 타입(`GameObject`)이 이 폴더의 것이 된다
        names |= set(re.findall(r'(?:class|struct)\s+(?:SW_(?:\w+_)?API\s+)?([A-Z]\w{7,})\s*(?:final\s*)?(?::[^;{}]*)?\{', text))
    joined = '\n'.join(texts)
    commands = set(re.findall(r'SW_(?:\w+_)?DEV_(?:\w+_)?COMMAND\w*\(\s*\w+\s*,\s*"([^"]+)"', joined))
    watches = set(re.findall(r'SW_AUTOMATION_(?:\w+_)?WATCH\w*\(\s*\w+\s*,\s*"([^"]+)"', joined))
    if unit == 0:
        unit = sum(1 for words in test_words if names & words)
    scen = sum(1 for words, lines, values in scenarios if commands & lines or watches & values or names & words)
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
