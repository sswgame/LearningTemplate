"""
Fileserve — PC 의 쿠킹한 팩 폴더를 읽기 전용 HTTP 로 장치에 내준다(개발 전용). 장치는 APK 를 다시 묶지 않고 다시 구운 팩을 받는다.

  py -3 -m Scripts fileserve --root build/Android-Release/Bin/Packs          # 127.0.0.1:1112 — USB 는 `adb reverse tcp:1112 tcp:1112`
  py -3 -m Scripts fileserve --root <팩 폴더> --bind 0.0.0.0                 # 같은 망의 장치(경고: 같은 망 누구나 이 폴더를 읽는다)

장치 쪽은 `App -fileserve=http://127.0.0.1:1112` 로 띄운다(`FileServeContentSource` — 받은 파일은 앱 전용 폴더의 거울에 두고, 크기 · 수정 시각이 같으면 다시 받지 않는다).
ezEngine Fileserve · 언리얼 쿡 데이터 스트리밍(Zen)과 같은 자리다.

끝점(모두 GET, `path` 는 루트 기준 `/` 구분 상대 경로 — 빈 글은 루트):
  /stat?path=P                    → `file <크기> <수정 시각 ns>` 또는 `folder`, 없으면 404
  /list?path=P                    → 바로 아래 파일 이름을 줄마다(이름 순, 폴더 제외)
  /file?path=P&offset=O&size=S    → 바이트 구간 [O, O+S) — S 는 1 MiB 이하, 끝을 넘으면 416

보안(개발 전용 서버라도 경계는 지킨다): 기본 바인드는 루프백이고, 경로는 상대 · `..` 없음 · 역슬래시 · NUL · 드라이브 없음이어야 하며
실제 경로(심볼릭 링크를 따른 것)가 루트 아래여야 한다. 읽기만 한다. 오류 본문은 짧은 글이고 내부 경로 · 스택을 내지 않는다.
"""

from __future__ import annotations

import argparse
import os
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

DEFAULT_PORT = 1112
DEFAULT_BIND = "127.0.0.1"
MAX_CHUNK_BYTES = 1024 * 1024          # 장치 HTTP 클라이언트의 응답 본문 상한(`HTTPConstant::kDefaultMaxBodyBytes`)과 같다
MAX_LIST_ENTRIES = 100_000
MAX_PATH_CHARS = 1024
# Windows 가 어느 폴더에서나 장치로 여는 이름(확장자가 붙어도) — 파이썬 판마다 realpath 가 다르게 다뤄 이름으로 거절한다.
_WINDOWS_DEVICE_NAMES = frozenset({"CON", "PRN", "AUX", "NUL", *(f"COM{n}" for n in range(1, 10)), *(f"LPT{n}" for n in range(1, 10))})


class FileServeError(Exception):
    """요청을 거절하는 까닭(HTTP 상태와 짧은 글)."""

    def __init__(self, status: int, text: str):
        super().__init__(text)
        self.status = status
        self.text = text


def resolve_request_path(root: Path, relative: str) -> Path:
    """상대 경로 `relative` 를 루트 아래 실제 경로로 바꾼다. 루트 밖 · 모양이 틀린 경로는 FileServeError(400 · 404)."""
    if len(relative) > MAX_PATH_CHARS or "\x00" in relative or "\\" in relative or ":" in relative or relative.startswith("/"):
        raise FileServeError(400, "bad path")
    parts = [part for part in relative.split("/") if part != ""]
    if any(part in (".", "..") or part.split(".")[0].upper() in _WINDOWS_DEVICE_NAMES for part in parts):
        raise FileServeError(400, "bad path")
    real_root = os.path.realpath(root)
    candidate = os.path.realpath(os.path.join(real_root, *parts)) if parts else real_root
    # 심볼릭 링크를 따른 뒤에도 루트 아래여야 한다 — 링크로 루트 밖을 가리키면 없는 것이다.
    # Windows 예약 이름(NUL · CON)은 realpath 가 장치 경로(\\.\NUL)로 바꿔 commonpath 가 ValueError 를 낸다 — 없는 것으로 본다.
    try:
        inside = os.path.commonpath([real_root, candidate]) == real_root
    except ValueError:
        inside = False
    if inside is False:
        raise FileServeError(404, "not found")
    return Path(candidate)


def describe(root: Path, relative: str) -> str:
    """`/stat` 응답 글."""
    path = resolve_request_path(root, relative)
    if path.is_dir():
        return "folder"
    if path.is_file():
        info = path.stat()
        return f"file {info.st_size} {info.st_mtime_ns}"
    raise FileServeError(404, "not found")


def list_files(root: Path, relative: str) -> str:
    """`/list` 응답 글 — 바로 아래 파일 이름(이름 순)."""
    path = resolve_request_path(root, relative)
    if path.is_dir() is False:
        raise FileServeError(404, "not found")
    names = sorted(entry.name for entry in path.iterdir() if entry.is_file())
    if len(names) > MAX_LIST_ENTRIES:
        raise FileServeError(413, "too many files")
    return "".join(f"{name}\n" for name in names)


def read_range(root: Path, relative: str, offset: int, size: int) -> bytes:
    """`/file` 응답 바이트 — 구간 [offset, offset+size)."""
    if offset < 0 or size <= 0 or size > MAX_CHUNK_BYTES:
        raise FileServeError(400, "bad range")
    path = resolve_request_path(root, relative)
    if path.is_file() is False:
        raise FileServeError(404, "not found")
    if offset + size > path.stat().st_size:
        raise FileServeError(416, "range outside the file")
    with path.open("rb") as stream:
        stream.seek(offset)
        return stream.read(size)


def _read_int(query: dict[str, list[str]], name: str) -> int:
    values = query.get(name, [])
    if len(values) != 1 or values[0].isdecimal() is False or values[0].isascii() is False:
        raise FileServeError(400, f"bad {name}")
    return int(values[0])


def make_handler(root: Path) -> type[BaseHTTPRequestHandler]:
    """루트 `root` 를 내주는 요청 처리기 형식."""

    class FileServeHandler(BaseHTTPRequestHandler):
        server_version = "SWFileServe"
        sys_version = ""

        def do_GET(self) -> None:  # noqa: N802 — http.server 의 이름
            split = urlsplit(self.path)
            query = parse_qs(split.query, keep_blank_values=True)
            paths = query.get("path", [""])
            try:
                if len(paths) != 1:
                    raise FileServeError(400, "bad path")
                if split.path == "/stat":
                    self._send(200, describe(root, paths[0]).encode("utf-8"), "text/plain; charset=utf-8")
                elif split.path == "/list":
                    self._send(200, list_files(root, paths[0]).encode("utf-8"), "text/plain; charset=utf-8")
                elif split.path == "/file":
                    body = read_range(root, paths[0], _read_int(query, "offset"), _read_int(query, "size"))
                    self._send(200, body, "application/octet-stream")
                else:
                    raise FileServeError(404, "not found")
            except FileServeError as error:
                self._send(error.status, error.text.encode("utf-8"), "text/plain; charset=utf-8")
            except OSError:
                self._send(500, b"read failed", "text/plain; charset=utf-8")
            except ValueError:
                self._send(400, b"bad request", "text/plain; charset=utf-8")

        def _send(self, status: int, body: bytes, content_type: str) -> None:
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("X-Content-Type-Options", "nosniff")
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, format: str, *args: object) -> None:  # noqa: A002 — http.server 의 서명
            sys.stderr.write(f"[fileserve] {self.address_string()} {format % args}\n")

    return FileServeHandler


def make_server(root: Path, bind: str, port: int) -> ThreadingHTTPServer:
    """서버를 만든다(띄우지 않는다). 루트가 폴더가 아니면 ValueError."""
    if root.is_dir() is False:
        raise ValueError(f"fileserve root is not a folder: {root}")
    server = ThreadingHTTPServer((bind, port), make_handler(root))
    server.daemon_threads = True
    return server


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts fileserve", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", required=True, help="내줄 폴더(장치 대상으로 쿠킹한 팩 폴더)")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--bind", default=DEFAULT_BIND, help="바인드 주소(기본 루프백 — USB 는 adb reverse, 같은 망은 0.0.0.0)")
    args = parser.parse_args(argv)
    root = Path(args.root).resolve()
    try:
        server = make_server(root, args.bind, args.port)
    except (ValueError, OSError) as error:
        print(f"[fileserve] {error}", file=sys.stderr)
        return 2
    if args.bind not in ("127.0.0.1", "::1", "localhost"):
        print(f"[fileserve] warning: bound to {args.bind} - anyone on this network can read {root}", file=sys.stderr)
    print(f"[fileserve] serving {root} on http://{args.bind}:{server.server_address[1]} (read-only, development only)", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
