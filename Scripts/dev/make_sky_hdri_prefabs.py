"""HDRI 하늘 프리팹을 짓는다 — Content/engine/textures_raw/sky/*.hdr 를 SH9 로 투영해 SkyComponent 프리팹(Content/engine/prefabs/sky/<이름>.prefab.xml)에 적고,
옥타헤드럴로 접은 원본(<이름>.radiancemap — C++ OctahedralRadianceMap)을 곁에 쓴다.

런타임은 그림(BC6H)을 CPU 로 읽지 않는다. 간접 조명(프로브 베이크 · 실시간 프로브)이 쓰는 저주파 빛은 SH9 계수이고, 스카이 라이트의 반사는 .radiancemap 을
거칠기 단계마다 거른 것이며(SkyLightData::make), 하늘 패스는 텍스처다.
원본을 바꾸면 다시 돌린다: py -3 Scripts/dev/make_sky_hdri_prefabs.py
방향 식은 sky.hlsl 의 evaluateHdri 와 같고, 기저 순서는 Core/Math/SphericalHarmonics.cpp 의 computeBasis 와,
옥타헤드럴 접기는 ReflectionProbeAsset::encodeOctahedral 과 같다.
"""

from __future__ import annotations

import math
import struct
import sys
from pathlib import Path

import numpy as np

_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(_ROOT / "Scripts"))
from lint.asset_identity import write_keeping_identity  # noqa: E402 — 다시 지어도 있던 프리팹의 정체성(머리 GUID)을 지킨다
_SOURCE = _ROOT / "Content/engine/textures_raw/sky"
_OUTPUT = _ROOT / "Content/engine/prefabs/sky"
# .radiancemap 한 변 텍셀 수 — 2K 등장방형의 해 원반이 텍셀 몇 개에 남고 파일이 약 200 KB 다. 스카이 라이트는 이것을 밉 사슬로 줄여 거른다.
_RADIANCE_RESOLUTION = 128
# OctahedralRadianceMap 파일 머리말(매직 SWRM · 버전 · 한 변) — C++ kMagic(FourCCUtil::make 는 첫 글자가 낮은 바이트) · kVersion 과 같다.
_RADIANCE_MAGIC = b"SWRM"
_RADIANCE_VERSION = 1


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


def _make_pixel_direction(height: int, width: int) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """등장방형 픽셀 중심의 방향(x, y, z)과 입체각이다. 행 0 이 +Y, u 0.5 가 +Z 다(sky.hlsl evaluateHdri)."""
    theta = (np.arange(height) + 0.5) / height * math.pi
    phi = ((np.arange(width) + 0.5) / width - 0.5) * 2.0 * math.pi
    sin_theta = np.sin(theta)[:, None]
    x = sin_theta * np.sin(phi)[None, :]
    y = np.cos(theta)[:, None] * np.ones_like(x)
    z = sin_theta * np.cos(phi)[None, :]
    solid_angle = sin_theta * (2.0 * math.pi / width) * (math.pi / height) * np.ones_like(x)
    return x, y, z, solid_angle


def _project(image: np.ndarray) -> np.ndarray:
    """등장방형 그림의 SH9 계수(9 × 3)다."""
    x, y, z, weight = _make_pixel_direction(*image.shape[:2])

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


def _fold_octahedral(image: np.ndarray, resolution: int) -> np.ndarray:
    """등장방형 그림을 옥타헤드럴 사각형(행 → 열, +Y 가 가운데)으로 접는다. 텍셀은 그 안에 드는 픽셀의 입체각 가중 평균이라 해 원반의 빛이 줄지 않는다."""
    height, width = image.shape[:2]
    x, y, z, solid_angle = _make_pixel_direction(height, width)
    axis_sum = np.maximum(np.abs(x) + np.abs(y) + np.abs(z), 1e-8)
    px, py, pz = x / axis_sum, y / axis_sum, z / axis_sum
    sign_x = np.where(px >= 0.0, 1.0, -1.0)
    sign_z = np.where(pz >= 0.0, 1.0, -1.0)
    u = np.where(py >= 0.0, px, (1.0 - np.abs(pz)) * sign_x) * 0.5 + 0.5
    v = np.where(py >= 0.0, pz, (1.0 - np.abs(px)) * sign_z) * 0.5 + 0.5
    column = np.clip((u * resolution).astype(int), 0, resolution - 1)
    row = np.clip((v * resolution).astype(int), 0, resolution - 1)
    index = (row * resolution + column).ravel()
    weight = np.bincount(index, solid_angle.ravel(), resolution * resolution)
    if np.any(weight <= 0.0):
        raise ValueError(f"octahedral resolution {resolution} has texels no equirectangular pixel reaches - the source is too small")
    folded = np.stack([np.bincount(index, (image[..., channel] * solid_angle).ravel(), resolution * resolution) / weight for channel in range(3)], axis=-1)
    return folded


def _write_radiance_map(name: str, folded: np.ndarray, resolution: int) -> None:
    data = _RADIANCE_MAGIC + struct.pack("<II", _RADIANCE_VERSION, resolution) + folded.astype("<f4").tobytes()
    (_OUTPUT / f"{name}.radiancemap").write_bytes(data)


def _write_prefab(name: str, coefficients: np.ndarray) -> None:
    items = "\n".join(f"\t\t\t\t\t<item>{value:.6f}</item>" for value in coefficients.flatten())
    text = f"""<!-- Scripts/dev/make_sky_hdri_prefabs.py 가 지었다 — 손으로 고치지 않는다. 그림은 텍스처, 간접 조명은 원본을 투영한 SH9 계수, 반사는 곁의 .radiancemap 을 거른 것이다. -->
<Prefab formatVersion="1" name="Sky_{name}">
	<GameObject _schemaVersion="0" _name="Sky_{name}" _bActive="true">
		<_listComponent>
			<SkyComponent _intensity="1" _mode="Hdri" _hdriTexture="engine/textures/sky/{name}.dds" _hdriRotation="0" _hdriRadianceMap="engine/prefabs/sky/{name}.radiancemap">
				<_listHdriAmbient>
{items}
				</_listHdriAmbient>
			</SkyComponent>
		</_listComponent>
	</GameObject>
</Prefab>
"""
    write_keeping_identity(_OUTPUT / f"{name}.prefab.xml", text)


def main() -> None:
    for path in sorted(_SOURCE.glob("*.hdr")):
        image = _read_hdr(path)
        _write_prefab(path.stem, _project(image))
        _write_radiance_map(path.stem, _fold_octahedral(image, _RADIANCE_RESOLUTION), _RADIANCE_RESOLUTION)
        print(path.stem)


if __name__ == "__main__":
    main()
