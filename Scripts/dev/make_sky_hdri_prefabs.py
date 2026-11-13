"""HDRI 하늘 프리팹을 짓는다 — Content/engine/textures_raw/sky/*.hdr 를 SH9 로 투영해 SkyComponent 프리팹(Content/engine/prefabs/sky/<이름>.prefab.xml)에 적는다.

런타임은 그림(BC6H)을 CPU 로 읽지 않는다. 간접 조명(프로브 베이크 · 실시간 프로브)이 쓰는 저주파 빛은 이 계수이고 하늘 패스는 텍스처다.
원본을 바꾸면 다시 돌린다: py -3 Scripts/dev/make_sky_hdri_prefabs.py
방향 식은 sky.hlsl 의 evaluateHdri 와 같고, 기저 순서는 Core/Math/SphericalHarmonics.cpp 의 computeBasis 와 같다.
"""

from __future__ import annotations

import math
from pathlib import Path

import numpy as np

_ROOT = Path(__file__).resolve().parents[2]
_SOURCE = _ROOT / "Content/engine/textures_raw/sky"
_OUTPUT = _ROOT / "Content/engine/prefabs/sky"


def _read_hdr(path: Path) -> np.ndarray:
    """Radiance RGBE(새 RLE 형식)를 (높이, 너비, 3) 선형 float 배열로 읽는다."""
    data = path.read_bytes()
    position = data.index(b"\n\n") + 2
    line_end = data.index(b"\n", position)
    parts = data[position:line_end].split()
    if parts[0] != b"-Y" or parts[2] != b"+X":
        raise ValueError(f"{path}: unsupported orientation {parts}")
    height, width = int(parts[1]), int(parts[3])
    position = line_end + 1
    rgbe = np.zeros((height, width, 4), dtype=np.uint8)
    for row in range(height):
        if data[position] != 2 or data[position + 1] != 2:
            raise ValueError(f"{path}: not new-style RLE")
        position += 4
        for channel in range(4):
            column = 0
            while column < width:
                count = data[position]
                position += 1
                if count > 128:
                    rgbe[row, column : column + count - 128, channel] = data[position]
                    position += 1
                    column += count - 128
                else:
                    rgbe[row, column : column + count, channel] = np.frombuffer(data, np.uint8, count, position)
                    position += count
                    column += count
    scale = np.where(rgbe[..., 3] > 0, np.ldexp(1.0, rgbe[..., 3].astype(int) - (128 + 8)), 0.0)
    return rgbe[..., :3].astype(np.float64) * scale[..., None]


def _project(image: np.ndarray) -> np.ndarray:
    """등장방형 그림의 SH9 계수(9 × 3)다. 행 0 이 +Y, u 0.5 가 +Z 다(sky.hlsl evaluateHdri)."""
    height, width = image.shape[:2]
    theta = (np.arange(height) + 0.5) / height * math.pi
    phi = ((np.arange(width) + 0.5) / width - 0.5) * 2.0 * math.pi
    sin_theta = np.sin(theta)[:, None]
    x = sin_theta * np.sin(phi)[None, :]
    y = np.cos(theta)[:, None] * np.ones_like(x)
    z = sin_theta * np.cos(phi)[None, :]
    weight = sin_theta * (2.0 * math.pi / width) * (math.pi / height)

    band0 = 0.5 / math.sqrt(math.pi)
    band1 = math.sqrt(3.0) * band0
    cross = math.sqrt(15.0) * band0
    zonal = math.sqrt(5.0) * band0 * 0.5
    square = cross * 0.5
    bases = [
        np.full_like(x, band0),
        band1 * y,
        band1 * z,
        band1 * x,
        cross * x * y,
        cross * y * z,
        zonal * (3.0 * z * z - 1.0),
        cross * x * z,
        square * (x * x - y * y),
    ]
    return np.array([[float(np.sum(image[..., channel] * basis * weight)) for channel in range(3)] for basis in bases])


def _write_prefab(name: str, coefficients: np.ndarray) -> None:
    items = "\n".join(f"\t\t\t\t\t<item>{value:.6f}</item>" for value in coefficients.flatten())
    text = f"""<!-- Scripts/dev/make_sky_hdri_prefabs.py 가 지었다 — 손으로 고치지 않는다. 그림은 텍스처, 간접 조명은 원본을 투영한 SH9 계수다. -->
<Prefab formatVersion="0" name="Sky_{name}">
	<GameObject _schemaVersion="0" _name="Sky_{name}" _bActive="true">
		<_listComponent>
			<SkyComponent _intensity="1" _mode="Hdri" _hdriTexture="engine/textures/sky/{name}.dds" _hdriRotation="0">
				<_listHdriAmbient>
{items}
				</_listHdriAmbient>
			</SkyComponent>
		</_listComponent>
	</GameObject>
</Prefab>
"""
    _OUTPUT.mkdir(parents=True, exist_ok=True)
    (_OUTPUT / f"{name}.prefab.xml").write_text(text, encoding="utf-8", newline="\n")


def main() -> None:
    for path in sorted(_SOURCE.glob("*.hdr")):
        _write_prefab(path.stem, _project(_read_hdr(path)))
        print(path.stem)


if __name__ == "__main__":
    main()
