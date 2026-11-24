"""기능 폴더별 테스트 전수표(py -3 Scripts/dev/test_coverage.py [--uncovered] [접두어…]) — 손으로 적은 목록 대신 이 표가 남은 일의 정본이다.

기능 폴더 = 직속 .cpp 가 있는 폴더(`Tests/` 제외). 칸:
- cpp     : 직속 .cpp 수(테스트 제외)
- unit    : 이 폴더 또는 그 아래 `Tests/` 의 `SW_TEST_CASE` 수 — 같은 이름 형제 `Tests/` 도 센다. 없으면 이 폴더가 정의한 타입 이름(8 자 이상)이 나오는 다른 폴더의 테스트 파일 수
- scen    : 이 폴더가 등록한 것(개발 명령 · 감시 값 · 시나리오 단계 · 전역 변수 · 패널 id · 패널 여는 커맨드 · 시각화 토글 표지)을 쓰거나 이 폴더가 정의한 타입 이름(8 자 이상)이 나오는
            시나리오 파일 수(주석 제외). 시나리오가 여는 씬(`….scene.xml`)에 놓인 컴포넌트도 그 시나리오가 돌리는 것으로 센다. `Model` 폴더는 그 모델을 여는 창(부모 폴더)의 것도 센다
- ctest   : 프로세스 진입점 — 이 폴더의 실행 파일(`main.cpp`)을 띄우거나 이 폴더의 전역 변수를 명령줄로 주는 CTest · 실행 스크립트 수, 그리고 `PROCESS_RUN` 의 줄
- 상태    : none(모두 없음) · unit · scen(시나리오 또는 ctest 만) · both
- 근거    : 시나리오 · ctest 로 세었으면 무엇으로 이었는지 첫 줄(`<이름> @ <파일>`) — 단위 테스트가 없는 화면 · 진입점을 무엇이 보는지 표에서 바로 본다
대조는 거칠다 — 이름이 나온다고 동작을 검증한다는 뜻은 아니다(패널을 열어 찍기만 하는 감사도 센다).
"""
import collections, dataclasses, pathlib, re, sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))   # Scripts
import common  # noqa: F401,E402 — 출력 인코딩을 UTF-8 로 맞춘다(cp949 콘솔에서 표의 '—' 가 UnicodeEncodeError 로 멈췄다)

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'Source'
args = [a for a in sys.argv[1:] if not a.startswith('--')]
only_uncovered = '--uncovered' in sys.argv


def read(path):
    return path.read_text(encoding='utf-8', errors='ignore')


# 이름으로 이을 수 없는 진입점(모든 실행이 지나지만 따로 부르는 이름이 없다)의 근거 — 파일에 그 글이 아직 있어야 센다(없으면 이 줄이 낡은 것이다).
PROCESS_RUN = {
    'DevTools/Client': ('Test/CMakeLists.txt', '$<TARGET_FILE:App> --test'),              # 개발 클라이언트 부분 — 개발 App 기동마다 선다
    'Editor': ('Test/Scenario/CMakeLists.txt', '-EnableEditor'),                          # 에디터 모듈의 뿌리(프레임 단계 · 서브시스템 묶음) — 에디터 체인마다 돈다
    'DevTools/Host': ('Scripts/dev/linux_scenarios.py', '-gv_serverExitAfterTicks'),      # 서버 호스트 개발 층
    'Server': ('Scripts/dev/linux_scenarios.py', './Bin/Server '),                        # 전용 서버 기동 · 정상 종료
    'Server/Windows': ('Scripts/net/net_pair_run.py', 'group.require("Server", "App")'),  # 서버 기동이 서비스 아님 길을 지난다
    'Editor/AssetActions': ('Content/engine/automation/editor/contentthumbnails.scenario.xml', 'content.open engine/textures'),   # 그림을 읽기 전의 종류 썸네일(콘텐츠 브라우저 타일)
    'Editor/Common/GUI/Status': ('Content/engine/automation/editor/lightprobebake.scenario.xml', 'editor lighting.bakeProbes'),   # 베이크 진행 상태줄 항목
}
PROCESS_RUN_SUFFIX = ('.txt', '.cmake', '.py')


@dataclasses.dataclass(frozen=True)
class Scenario:
    """시나리오 하나가 부르는 이름들 — 이름마다 본문을 다시 훑으면 폴더 × 이름 × 시나리오라 몇 분이 걸린다."""
    path: str
    words: frozenset[str]        # 본문의 낱말
    line_words: frozenset[str]   # 개발 명령 줄의 낱말(명령 이름 · 패널 id · 에디터 커맨드 id)
    watches: frozenset[str]
    steps: frozenset[str]        # 단계 원소 이름
    marks: frozenset[str]        # 누르는 UI 표지(`mark="…"`)
    scene_elements: frozenset[str]   # 여는 씬에 놓인 원소(컴포넌트) 이름


scene_elements: dict[str, frozenset[str]] = {}


def read_scene_elements(scene_id):
    """씬 파일에 놓인 원소 이름들 — 없는 씬(실행 중 만드는 사본)이면 비었다."""
    if scene_id not in scene_elements:
        scene_path = ROOT / 'Content' / scene_id
        scene_elements[scene_id] = frozenset(re.findall(r'<([A-Z]\w{7,})\b', read(scene_path))) if scene_path.is_file() else frozenset()
    return scene_elements[scene_id]


scenarios = []
for path in (ROOT / 'Content').rglob('*.scenario.xml'):
    body = re.sub(r'<!--.*?-->', '', read(path), flags=re.S)
    scene_ids = set(re.findall(r'\b((?:game|common|engine|editor)/[\w./-]+\.scene\.xml)', body))
    scenarios.append(Scenario(
        path.relative_to(ROOT / 'Content').as_posix(),
        frozenset(re.findall(r'\w+', body)),
        frozenset(word for line in re.findall(r'\bline="([^"]*)', body) for word in line.split()),
        frozenset(re.findall(r'\bwatch="([^"]+)"', body)),
        frozenset(re.findall(r'<([A-Z]\w*)\b', body)),
        frozenset(re.findall(r'\bmark="([^"]+)"', body)),
        frozenset().union(*map(read_scene_elements, scene_ids)) if scene_ids else frozenset()))

runs = []   # (파일, 글) — CTest 등록과 실행 스크립트
for folder in (ROOT / 'Test', ROOT / 'Scripts' / 'net', ROOT / 'Scripts' / 'dev'):
    for path in folder.rglob('*'):
        if path.suffix in PROCESS_RUN_SUFFIX and path != pathlib.Path(__file__).resolve() and '/tests/' not in path.as_posix():
            runs.append((path.relative_to(ROOT).as_posix(), read(path)))

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

@dataclasses.dataclass(frozen=True)
class FolderNames:
    """폴더 하나가 정의 · 등록한 이름들."""
    types: frozenset[str]
    commands: frozenset[str]
    watches: frozenset[str]
    steps: frozenset[str]
    variables: frozenset[str]
    panels: frozenset[str]   # 패널 id 와 패널을 여는 커맨드 id
    marks: frozenset[str]    # 등록이 만드는 UI 표지(뷰포트 시각화의 토글)


folder_names: dict[pathlib.Path, FolderNames] = {}
panel_constants = dict(re.findall(r'\b(k\w+)\{?\s*(?:=|\{)\s*"([a-z][\w.]+)"', read(SOURCE / 'Editor/Common/Workspace/EditorPanelID.h')))


def read_names(folder):
    if folder in folder_names:
        return folder_names[folder]
    texts = [read(f) for f in (*folder.glob('*.cpp'), *folder.glob('*.h'))]
    joined = '\n'.join(texts)
    # 정의만 센다 — 앞으로 선언까지 세면 남의 타입(`GameObject`)이 이 폴더의 것이 된다
    types = re.findall(r'(?:class|struct)\s+(?:SW_(?:\w+_)?API\s+)?([A-Z]\w{7,})\s*(?:final\s*)?(?::[^;{}]*)?\{', joined)
    panels = re.findall(r'\bkPanelID\s*=\s*"([^"]+)"', joined)
    panels += [panel_constants[name] for name in re.findall(r'\bkPanelID\s*=\s*editorpanel::(\w+)', joined) if name in panel_constants]
    panels += re.findall(r'SW_EDITOR_PANEL_OPEN_COMMAND\(\s*\w+\s*,\s*"([^"]+)"', joined)
    folder_names[folder] = FolderNames(
        frozenset(types),
        frozenset(re.findall(r'SW_(?:\w+_)?DEV_(?:\w+_)?COMMAND\w*\(\s*\w+\s*,\s*"([^"]+)"', joined)),
        frozenset(re.findall(r'SW_AUTOMATION_(?:\w+_)?WATCH\w*\(\s*\w+\s*,\s*"([^"]+)"', joined)),
        frozenset(re.findall(r'SW_AUTOMATION_STEP\w*\(\s*\w+\s*,\s*"([^"]+)"', joined)),
        frozenset(re.findall(r'SW_\w*GLOBAL_VARIABLE\(\s*[\w:]+\s*,\s*(gv_\w+)', joined)),
        frozenset(panels),
        frozenset(f'viewport.visualizer.{name}' for name in re.findall(r'SW_EDITOR_VISUALIZER_(?:TOOL|GATHER)\(\s*\w+\s*,\s*"([^"]+)"', joined)))
    return folder_names[folder]


def find_scenario_names(names, scenario):
    """시나리오가 부르는 이 폴더의 이름들 — 또렷한 것(등록한 이름)부터."""
    return (sorted(names.watches & scenario.watches) or sorted(names.commands & scenario.line_words) or sorted(names.steps & scenario.steps)
            or sorted(names.panels & scenario.line_words) or sorted(names.marks & scenario.marks) or sorted(names.variables & scenario.words) or sorted(names.types & scenario.words)
            or sorted(names.types & scenario.scene_elements))


rows = []
for folder, cpps in folders.items():
    rel = folder.relative_to(SOURCE).as_posix()
    if args and not any(rel.startswith(a) for a in args):
        continue
    unit = sum(n for owner, n in tests.items() if folder == owner or owner in folder.parents)
    list_names = [read_names(folder)]
    if folder.name == 'Model':
        list_names.append(read_names(folder.parent))   # 모델을 여는 창
    if unit == 0:
        unit = sum(1 for words in test_words if list_names[0].types & words)
    scen, evidence = 0, ''
    for scenario in scenarios:
        found = next((found for names in list_names if (found := find_scenario_names(names, scenario))), None)
        if found:
            scen += 1
            evidence = evidence or f'{found[0]} @ {scenario.path}'
    ctest = 0
    executable = folder.name if (folder / 'main.cpp').is_file() else None
    if folder.name == 'ReflectionParser':   # 실행 파일 이름이 폴더 이름이다(main 은 다른 파일에 있다)
        executable = folder.name
    for run_path, text in runs:
        name = next((name for name in sorted(list_names[0].variables) if name in text), None)
        if name is None and executable is not None and f'$<TARGET_FILE:{executable}>' in text:
            name = f'$<TARGET_FILE:{executable}>'
        if name is not None:
            ctest += 1
            evidence = evidence or f'{name} @ {run_path}'
    if rel in PROCESS_RUN:
        run_path, token = PROCESS_RUN[rel]
        if token in read(ROOT / run_path):
            ctest += 1
            evidence = evidence or f'{token} @ {run_path}'
        else:
            print(f'[test_coverage] 낡은 근거 — {run_path} 에 {token!r} 가 없다(PROCESS_RUN 의 {rel})', file=sys.stderr)
    state = 'both' if unit and (scen or ctest) else 'unit' if unit else 'scen' if scen or ctest else 'none'
    if only_uncovered and state != 'none':
        continue
    rows.append((rel, len(cpps), unit, scen, ctest, state, evidence))

rows.sort()
print('| 기능 폴더 | cpp | unit | scen | ctest | 상태 | 근거 |\n|---|---|---|---|---|---|---|')
for r in rows:
    print(f'| {r[0]} | {r[1]} | {r[2]} | {r[3]} | {r[4]} | {r[5]} | {r[6]} |')
total = collections.Counter(r[5] for r in rows)
print(f'\n폴더 {len(rows)} — ' + ' · '.join(f'{k} {total[k]}' for k in ('none', 'unit', 'scen', 'both')) +
      f' · 테스트 없는 cpp {sum(r[1] for r in rows if r[5] == "none")}')
