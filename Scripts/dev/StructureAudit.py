"""구조 전수 감사(py -3 Scripts/dev/StructureAudit.py) — 폴더 크기 · 이름 묶음 · 모호어 · 정적 전용 타입 · 파일명/타입 불일치."""
import os, re, collections, pathlib

os.chdir(pathlib.Path(__file__).resolve().parents[2] / 'Source')
out = []
word = re.compile(r'[A-Z]+(?=[A-Z][a-z])|[A-Z]?[a-z0-9]+|[A-Z]+')

# A. 직속 파일이 많은 폴더와 그 안의 첫 낱말 묶음
out.append('## A. 직속 소스 파일 25+ 폴더 (첫 낱말 묶음 ≥4)')
for root, dirs, files in os.walk('.'):
    if 'graphify-out' in root:
        continue
    stems = sorted({pathlib.Path(f).stem for f in files if f.endswith(('.h', '.cpp'))})
    if len(stems) < 14:  # 헤더+cpp 짝 기준 대략 25 파일
        continue
    fam = collections.Counter()
    for s in stems:
        w = word.findall(s)
        fam[w[0] if w else s] += 1
    groups = [f'{k}({v})' for k, v in fam.most_common() if v >= 4]
    out.append(f'- {root[2:]} : {len(stems)} 단위 — 묶음 {" ".join(groups) or "-"}')

# B. 모호어 이름(파일 · 폴더)
out.append('\n## B. 모호한 이름(파일 · 폴더)')
vague = re.compile(r'(Part|Misc|Stuff|Helper|Helpers|Common|Shared|Base|Core|Data|Info|Impl|Handler|Processor|Wrapper|Object)$')
hits = collections.defaultdict(list)
for root, dirs, files in os.walk('.'):
    if 'graphify-out' in root:
        continue
    for d in dirs:
        if vague.search(d):
            hits['folder'].append(f'{root[2:]}/{d}')
    for f in files:
        stem = pathlib.Path(f).stem
        if f.endswith('.h') and vague.search(stem):
            hits['file'].append(f'{root[2:]}/{stem}')
for k in ('folder', 'file'):
    out.append(f'- {k} {len(hits[k])}: ' + ', '.join(sorted(hits[k])[:80]))

# C. 정적 전용 타입(Util/Internal 이 아닌)
out.append('\n## C. 정적 함수만 있는 타입(Util/Internal 접미사 아님) — 폴더별')
rx = re.compile(r'\b(class|struct)\s+(?:SW_\w+\s+)?(\w+)\s*(?:final\s*)?\{', re.S)
byFolder = collections.defaultdict(list)
for f in pathlib.Path('.').rglob('*.h'):
    if 'graphify-out' in f.parts:
        continue
    t = re.sub(r'//[^\n]*|/\*.*?\*/', '', f.read_text('utf-8', 'ignore'), flags=re.S)
    for m in rx.finditer(t):
        name = m.group(2)
        if name.endswith(('Util', 'Internal')):
            continue
        depth, j = 0, m.end() - 1
        for j in range(m.end() - 1, len(t)):
            if t[j] == '{': depth += 1
            elif t[j] == '}':
                depth -= 1
                if depth == 0: break
        body = t[m.end():j]
        top = body
        for _ in range(5): top = re.sub(r'\{[^{}]*\}', ';', top)
        decls = [d.strip() for d in top.split(';') if d.strip()]
        decls = [re.sub(r'^(public|private|protected)\s*:\s*', '', d) for d in decls]
        decls = [d for d in decls if d and not d.startswith(('using ', 'friend ', 'template', '#', 'enum', 'struct', 'class'))]
        funcs = [d for d in decls if '(' in d and 'static constexpr' not in d and '= delete' not in d]
        if funcs and all('static' in d.split('(')[0] for d in funcs) and not any(('(' not in d and 'static' not in d) for d in decls):
            byFolder[str(f.parent)].append(f'{name}({len(funcs)})')
total = sum(len(v) for v in byFolder.values())
out.append(f'- 합계 {total}')
for k, v in sorted(byFolder.items(), key=lambda x: -len(x[1]))[:40]:
    out.append(f'- {k}: ' + ', '.join(v))

pathlib.Path(r'C:\Users\ssw\AppData\Local\Temp\claude\d--Projects-Personal-LearningTemplate\fe9d9946-d724-47e5-aca1-6c1b1ed0dd2d\scratchpad\audit.md').write_text('\n'.join(out), encoding='utf-8')
print('\n'.join(out)[:9000])
