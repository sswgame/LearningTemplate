"""
리눅스 서버 묶음(`server_image.py`) 회귀 검사 — systemd 유닛의 멈춤 시한 · 묶음 안의 파일 자리.

실행: python -X utf8 Scripts/package/tests/test_server_image.py (CTest `Scripts.ServerImage`). 임시 폴더에 묶는다.
"""

from __future__ import annotations

import sys
import tarfile
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.host_platform import is_windows_host  # noqa: E402
from package.server_image import SERVER_EXECUTABLE, ServerImageError, make_unit_text, read_stop_seconds, to_wsl_path, write_archive  # noqa: E402


class ServerImageTest(unittest.TestCase):
    def test_unit_and_archive(self) -> None:
        unit = make_unit_text("Arena", read_stop_seconds({"_drainWaitSeconds": 60, "_shutdownGraceSeconds": 15}))
        assert "TimeoutStopSec=105" in unit, "시한 = 드레인 + 저장 + 여유"
        assert "ExecStart=/opt/swserver/Arena/Server -server-config=/etc/swserver/Arena.json" in unit
        assert "EnvironmentFile=/etc/swserver/Arena.env" in unit and "User=swserver" in unit
        with tempfile.TemporaryDirectory(prefix="swserverimage-") as folder:
            package_dir = Path(folder) / "package"
            package_dir.mkdir()
            (package_dir / SERVER_EXECUTABLE).write_bytes(b"")
            config_path = Path(folder) / "server.json"
            config_path.write_text("{}", encoding="utf-8")
            archive_path = Path(folder) / "out.tar.gz"
            write_archive(archive_path, package_dir, config_path, "Arena", unit)
            with tarfile.open(archive_path) as archive:
                names = set(archive.getnames())
                assert {"etc/systemd/system/swserver-Arena.service", "etc/sysusers.d/swserver.conf", "etc/tmpfiles.d/swserver-Arena.conf", "etc/swserver/Arena.json"} <= names, names
                assert archive.getmember("opt/swserver/Arena/Server").mode == 0o755
                fragment = archive.extractfile("etc/tmpfiles.d/swserver-Arena.conf")
                assert fragment is not None and fragment.read() == b"d /opt/swserver/Arena/Saved 0750 swserver swserver -\n"
        try:
            read_stop_seconds({"_drainWaitSeconds": 60})
            raise AssertionError("빠진 칸은 멈춘다")
        except ServerImageError:
            pass
        assert is_windows_host() is False or to_wsl_path(Path("C:/a/b")) == "/mnt/c/a/b"


if __name__ == "__main__":
    unittest.main()
