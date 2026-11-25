"""셰이더 글을 보는 규칙이 함께 쓰는 것 — 대상 확장자와 주석 · 전처리 줄 모양."""

from __future__ import annotations

import re

EXTENSIONS = (".hlsl", ".hlsli")
COMMENT_PATTERN = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
PREPROCESSOR_PATTERN = re.compile(r"^[ \t]*#[^\n]*(?:\\\n[^\n]*)*", re.M)
