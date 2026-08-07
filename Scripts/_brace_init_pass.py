#!/usr/bin/env python3
"""Convert ctor member-initializer () to {} and common local () inits in Source/."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "Source"


def find_matching_paren(s: str, open_idx: int) -> int:
	depth = 0
	k = open_idx
	while k < len(s):
		ch = s[k]
		if ch in ('"', "'"):
			quote = ch
			k += 1
			while k < len(s):
				if s[k] == "\\":
					k += 2
					continue
				if s[k] == quote:
					break
				k += 1
		elif ch == "(":
			depth += 1
		elif ch == ")":
			depth -= 1
			if depth == 0:
				return k
		k += 1
	return -1


def convert_mem_inits_on_fragment(rest: str) -> tuple[str, int]:
	"""Convert ident( args ) sequences on a member-init fragment."""
	i = 0
	out: list[str] = []
	n = 0
	while i < len(rest):
		if rest[i].isspace():
			out.append(rest[i])
			i += 1
			continue
		if rest.startswith("[[", i):
			j = rest.find("]]", i)
			if j < 0:
				out.append(rest[i:])
				break
			out.append(rest[i : j + 2])
			i = j + 2
			continue
		if rest.startswith("mutable", i) and (
			i + 7 >= len(rest) or not (rest[i + 7].isalnum() or rest[i + 7] == "_")
		):
			out.append("mutable")
			i += 7
			continue
		m = re.match(r"[A-Za-z_][A-Za-z0-9_]*", rest[i:])
		if not m:
			out.append(rest[i:])
			break
		ident = m.group(0)
		j = i + len(ident)
		while j < len(rest) and rest[j].isspace():
			j += 1
		if j >= len(rest) or rest[j] != "(":
			out.append(rest[i])
			i += 1
			continue
		k = find_matching_paren(rest, j)
		if k < 0:
			out.append(rest[i:])
			break
		inner = rest[j + 1 : k]
		out.append(ident)
		out.append("{")
		out.append(inner)
		out.append("}")
		n += 1
		i = k + 1
	return "".join(out), n


def convert_mem_init_lines(text: str) -> tuple[str, int]:
	total = 0
	out_lines: list[str] = []
	for line in text.splitlines(True):
		raw = line.rstrip("\r\n")
		ending = line[len(raw) :]
		m = re.match(r"^(\s*)([:,])(\s*)(.+)$", raw)
		if not m:
			out_lines.append(line)
			continue
		# Skip range-for / ternary noise: lines like ", true ?" unlikely; skip "for ("
		if "for (" in raw or "for(" in raw:
			out_lines.append(line)
			continue
		prefix = m.group(1) + m.group(2) + m.group(3)
		rest = m.group(4)
		# Only convert if first token looks like a member/ctor init (starts with _ or is TypeName-ish after :)
		first = rest.lstrip()
		if not first or first.startswith("//") or first.startswith("/*"):
			out_lines.append(line)
			continue
		# Skip preprocessor
		if first.startswith("#"):
			out_lines.append(line)
			continue
		new_rest, n = convert_mem_inits_on_fragment(rest)
		total += n
		out_lines.append(prefix + new_rest + ending)
	return "".join(out_lines), total


def convert_inline_ctor_inits(text: str) -> tuple[str, int]:
	total = 0
	out_lines: list[str] = []
	for line in text.splitlines(True):
		raw = line.rstrip("\r\n")
		ending = line[len(raw) :]
		if re.search(r"\bfor\s*\(", raw):
			out_lines.append(line)
			continue
		m = re.search(r"\)\s*:", raw)
		if not m or "{" not in raw[m.end() :]:
			out_lines.append(line)
			continue
		colon = raw.find(":", m.start())
		brace = raw.find("{", colon)
		if brace < 0:
			out_lines.append(line)
			continue
		head = raw[: colon + 1]
		mid = raw[colon + 1 : brace]
		tail = raw[brace:]
		new_mid, n = convert_mem_inits_on_fragment(mid)
		total += n
		out_lines.append(head + new_mid + tail + ending)
	return "".join(out_lines), total


LOCK_PATTERNS = [
	# std::lock_guard<Mutex> name( arg );
	(
		re.compile(
			r"\b(std::(?:lock_guard|unique_lock|shared_lock)\s*<[^>]+>\s+)([A-Za-z_][A-Za-z0-9_]*)\s*\(([^;]*)\)\s*;"
		),
		r"\1\2{ \3 };",
	),
]


def convert_lock_inits(text: str) -> tuple[str, int]:
	total = 0
	for pat, repl in LOCK_PATTERNS:
		text, n = pat.subn(repl, text)
		total += n
	return text, total


def main() -> None:
	files_changed: list[tuple[str, int]] = []
	grand = 0
	for p in sorted(ROOT.rglob("*.h")) + sorted(ROOT.rglob("*.hpp")) + sorted(ROOT.rglob("*.cpp")):
		text = p.read_text(encoding="utf-8")
		t1, n1 = convert_mem_init_lines(text)
		t2, n2 = convert_inline_ctor_inits(t1)
		t3, n3 = convert_lock_inits(t2)
		n = n1 + n2 + n3
		if n:
			p.write_text(t3, encoding="utf-8", newline="\n")
			files_changed.append((str(p.relative_to(ROOT)), n))
			grand += n
	print(f"Total conversions: {grand} in {len(files_changed)} files")
	for f, n in files_changed:
		print(f"  {f}: {n}")


if __name__ == "__main__":
	main()
