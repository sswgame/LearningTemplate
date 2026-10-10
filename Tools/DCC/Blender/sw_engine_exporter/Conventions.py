# -*- coding: utf-8 -*-
"""
엔진 규약 — 이름 · 경로 · 좌표계 변환. **bpy 를 쓰지 않는다**(Blender 없이 CI 에서 시험한다: Test/PythonTest/TestBlenderExporter.py).

좌표계: Blender 는 오른손 · +Z 위 · 앞이 -Y, glTF 는 오른손 · +Y 위 · 앞이 +Z, 엔진은 왼손 · +Y 위 · 앞이 +Z 다.
Blender 의 glTF 내보내기(`export_yup=True`)가 Blender → glTF 를, 엔진의 모델 임포터(`ModelImporter::convertToEngineSpace`)가
glTF → 엔진(X 를 뒤집는다)을 한다. 그래서 메시는 이 애드온이 손대지 않는다. 손대는 것은 **소켓 초안**뿐이다 — 소켓은 glTF 가 아니라
`*.sockets.xml` 로 가므로 두 변환을 여기서 한 번에 한다: 엔진 = C · Blender, C = [[-1,0,0],[0,0,1],[0,-1,0]].
단위는 미터 그대로다(Blender 1 = 엔진 1 m).

회전은 엔진 `CharacterDataReader::readRotation` 이 읽는 꼴로 쓴다: "x y z" 도(degree), x = 피치, y = 요, z = 롤이고
`quaternion::makeFromYawPitchRoll` 이 그것을 쓴다(열벡터로 R = Ry(요) · Rx(피치) · Rz(롤)).
"""

from __future__ import annotations

import math
import re

Matrix3 = list[list[float]]
Matrix4 = list[list[float]]

#: Blender 공간 → 엔진 공간의 기저 변환(열벡터).
kBlenderToEngine: Matrix3 = [[-1.0, 0.0, 0.0], [0.0, 0.0, 1.0], [0.0, -1.0, 0.0]]

#: 소켓으로 보는 엠프티 이름 머리 — 언리얼 FBX 임포트의 `SOCKET_` 규약과 같다. 사용자 속성 `sw_socket` 도 소켓이다.
kSocketPrefix = "SOCKET_"

#: 리소스 파일 이름 규칙(`Config/Editor/AssetValidationRules.json` 의 file-names 와 같은 문자 집합).
kResourceNameRe = re.compile(r"^[a-z0-9_.\-]+$")

#: 소켓 이름 규칙 — 엔진은 `MainHand.Muzzle` 처럼 슬롯 접두어를 점으로 붙이므로 소켓 이름에는 점을 쓰지 않는다.
kSocketNameRe = re.compile(r"^[A-Za-z][A-Za-z0-9_]*$")


# ------------------------------------------------------------------------------
# 이름 · 경로
# ------------------------------------------------------------------------------
def makeResourceName(name: str) -> str:
    """Blender 이름("Hero Sword.001")을 리소스 파일 이름(`hero_sword_001`)으로 바꿉니다. 빈 결과면 ValueError 입니다."""
    lowered = re.sub(r"[^a-z0-9_\-]+", "_", name.strip().lower())
    lowered = re.sub(r"_+", "_", lowered).strip("_")
    if not lowered:
        raise ValueError(f"'{name}' has no letters or digits to make a resource name from")
    return lowered


def makeSocketName(name: str) -> str:
    """엠프티 이름에서 소켓 이름을 만듭니다(`SOCKET_` 머리를 떼고 Blender 의 `.001` 꼬리를 뗍니다)."""
    bare = name[len(kSocketPrefix):] if name.startswith(kSocketPrefix) else name
    bare = re.sub(r"\.\d+$", "", bare)
    bare = re.sub(r"[^A-Za-z0-9_]+", "_", bare).strip("_")
    if not bare or not kSocketNameRe.match(bare):
        raise ValueError(f"'{name}' does not make a valid socket name (letters, digits, underscore; starts with a letter)")
    return bare


def isSocketObjectName(name: str, bHasSocketProperty: bool = False) -> bool:
    return bHasSocketProperty or name.startswith(kSocketPrefix)


def makeExportPaths(repositoryRoot: str, packName: str, assetName: str) -> dict[str, str]:
    """
    내보낼 자리들 — 원본 glTF 는 `Resource/game/<팩>/models_raw/<이름>.glb`, 임포트 결과는 `models/<이름>.mesh`,
    소켓 초안은 메시 옆 `models/<이름>.sockets.xml` 이다. 팩 이름이 `engine` 이면 `Resource/engine/` 아래다.
    """
    pack = makeResourceName(packName)
    name = makeResourceName(assetName)
    domain = "engine" if pack == "engine" else f"game/{pack}"
    root = repositoryRoot.rstrip("/\\").replace("\\", "/")
    return {
        "source": f"{root}/Resource/{domain}/models_raw/{name}.glb",
        "mesh": f"{root}/Resource/{domain}/models/{name}.mesh",
        "sockets": f"{root}/Resource/{domain}/models/{name}.sockets.xml",
        "meshID": f"{domain}/models/{name}.mesh",
    }


# ------------------------------------------------------------------------------
# 행렬 · 회전
# ------------------------------------------------------------------------------
def multiply3(left: Matrix3, right: Matrix3) -> Matrix3:
    return [[sum(left[row][k] * right[k][column] for k in range(3)) for column in range(3)] for row in range(3)]


def transpose3(matrix: Matrix3) -> Matrix3:
    return [[matrix[column][row] for column in range(3)] for row in range(3)]


def transformVector3(matrix: Matrix3, vector: list[float]) -> list[float]:
    return [sum(matrix[row][k] * vector[k] for k in range(3)) for row in range(3)]


def decomposeMatrix4(matrix: Matrix4) -> tuple[list[float], Matrix3, list[float]]:
    """열벡터 4x4(Blender `Matrix` 를 리스트로 바꾼 것)를 이동 · 회전 · 스케일로 나눕니다. 거울상(음의 행렬식)은 X 스케일에 싣습니다."""
    translation = [matrix[0][3], matrix[1][3], matrix[2][3]]
    listColumn = [[matrix[row][column] for row in range(3)] for column in range(3)]
    scale = [math.sqrt(sum(value * value for value in column)) for column in listColumn]
    determinant = (listColumn[0][0] * (listColumn[1][1] * listColumn[2][2] - listColumn[2][1] * listColumn[1][2])
                   - listColumn[1][0] * (listColumn[0][1] * listColumn[2][2] - listColumn[2][1] * listColumn[0][2])
                   + listColumn[2][0] * (listColumn[0][1] * listColumn[1][2] - listColumn[1][1] * listColumn[0][2]))
    if determinant < 0.0:
        scale[0] = -scale[0]
    rotation = [[matrix[row][column] / scale[column] if scale[column] != 0.0 else 0.0 for column in range(3)] for row in range(3)]
    return translation, rotation, scale


def convertRotationToEngine(rotation: Matrix3) -> Matrix3:
    """Blender 공간의 회전(열벡터)을 엔진 공간으로 옮깁니다: C · R · Cᵀ."""
    return multiply3(multiply3(kBlenderToEngine, rotation), transpose3(kBlenderToEngine))


def convertVectorToEngine(vector: list[float]) -> list[float]:
    return transformVector3(kBlenderToEngine, vector)


def convertScaleToEngine(scale: list[float]) -> list[float]:
    """축 스케일은 축이 바뀌는 대로 옮긴다(부호 없이): 엔진 (x, y, z) = Blender (x, z, y)."""
    return [abs(scale[0]), abs(scale[2]), abs(scale[1])]


def makeEngineRotationMatrix(pitch: float, yaw: float, roll: float) -> Matrix3:
    """엔진 각(라디안)의 회전 행렬(열벡터) — R = Ry(요) · Rx(피치) · Rz(롤)."""
    cp, sp, cy, sy, cr, sr = math.cos(pitch), math.sin(pitch), math.cos(yaw), math.sin(yaw), math.cos(roll), math.sin(roll)
    rotationX = [[1.0, 0.0, 0.0], [0.0, cp, -sp], [0.0, sp, cp]]
    rotationY = [[cy, 0.0, sy], [0.0, 1.0, 0.0], [-sy, 0.0, cy]]
    rotationZ = [[cr, -sr, 0.0], [sr, cr, 0.0], [0.0, 0.0, 1.0]]
    return multiply3(multiply3(rotationY, rotationX), rotationZ)


def computeEngineEulerDegrees(rotation: Matrix3) -> list[float]:
    """엔진 회전 행렬(열벡터)에서 `readRotation` 이 읽을 "피치 요 롤" 도를 구합니다. 짐벌 잠금(피치 ±90)에서는 롤을 0 으로 둡니다."""
    sinPitch = max(-1.0, min(1.0, -rotation[1][2]))
    pitch = math.asin(sinPitch)
    if abs(sinPitch) < 0.999999:
        yaw = math.atan2(rotation[0][2], rotation[2][2])
        roll = math.atan2(rotation[1][0], rotation[1][1])
    else:
        yaw = math.atan2(-rotation[2][0], rotation[0][0])
        roll = 0.0
    return [math.degrees(pitch), math.degrees(yaw), math.degrees(roll)]


def makeEngineQuaternion(pitch: float, yaw: float, roll: float) -> list[float]:
    """엔진 `quaternion::makeFromYawPitchRoll` 을 그대로 옮긴 것(x, y, z, w) — 시험이 변환을 엔진 식과 맞춰 보는 데 쓴다."""
    halfYaw, halfPitch, halfRoll = yaw * 0.5, pitch * 0.5, roll * 0.5
    sinYaw, cosYaw = math.sin(halfYaw), math.cos(halfYaw)
    sinPitch, cosPitch = math.sin(halfPitch), math.cos(halfPitch)
    sinRoll, cosRoll = math.sin(halfRoll), math.cos(halfRoll)
    return [cosYaw * sinPitch * cosRoll + sinYaw * cosPitch * sinRoll,
            sinYaw * cosPitch * cosRoll - cosYaw * sinPitch * sinRoll,
            cosYaw * cosPitch * sinRoll - sinYaw * sinPitch * cosRoll,
            cosYaw * cosPitch * cosRoll + sinYaw * sinPitch * sinRoll]


def makeRotationMatrixFromQuaternion(quaternion: list[float]) -> Matrix3:
    """단위 쿼터니언(x, y, z, w)의 회전 행렬(열벡터, v' = q v q*)."""
    x, y, z, w = quaternion
    return [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]


def formatFloat3(values: list[float]) -> str:
    """엔진 `formatFloat3` 과 같은 꼴 — 공백으로 나눈 세 수, 불필요한 0 은 뗀다."""
    listText = []
    for value in values:
        rounded = round(value, 5)
        if rounded == 0.0:
            rounded = 0.0  # -0 을 0 으로
        text = f"{rounded:.5f}".rstrip("0").rstrip(".")
        listText.append(text if text not in ("", "-0") else "0")
    return " ".join(listText)
