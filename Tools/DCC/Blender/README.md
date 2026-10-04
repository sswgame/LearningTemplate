# Blender → SW Engine 내보내기 애드온

`sw_engine_exporter/` 는 Blender 애드온입니다. 고른 오브젝트 · 아마추어 · 애니메이션을 엔진 규약으로 glTF 로 내보내고, 소켓 엠프티를
`*.sockets.xml` 초안으로 쓰고, `App --import-models` 를 띄워 `.mesh` 로 임포트합니다. 언리얼의 FBX 내보내기 규약(`SOCKET_` 엠프티 → 스태틱 메시 소켓)과
Datasmith 의 "DCC 에서 버튼 하나로 엔진까지" 를 이 엔진의 glTF 경로에 맞춘 것입니다.

## 설치

1. Blender 3.6 이상 → Edit → Preferences → Add-ons → Install… 에서 `sw_engine_exporter` 폴더를 zip 으로 묶어 고릅니다
   (또는 그 폴더를 Blender 의 `scripts/addons/` 에 심볼릭 링크합니다 — 저장소를 고치면 바로 반영됩니다).
2. 애드온 설정에서 **Engine repository** 에 저장소 루트(`Resource/` · `build/` 가 있는 폴더)를 적습니다. App 은 `build/Ninja-Debug/Bin/App.exe` 부터
   찾습니다(`Scripts/common/AppBinary.py` 와 같은 순서, Dev 빌드만 — 임포트는 에디터 모듈의 일이라 Shipping App 은 못 한다). 다른 App 을 쓰려면 **App executable** 에 적습니다.

## 쓰기

- File → Export → **SW Engine (.glb)**, 또는 3D 뷰 사이드바(N) → **SW Engine** 탭.
- **Pack**: `Resource/game/<팩>` 의 팩 이름(`shooter3d` …) 또는 `engine`. **Asset name**: 기본은 활성 오브젝트 이름이고 리소스 이름 규칙(소문자 · 숫자 · `_` · `-`)으로 바뀝니다.
- 결과:

| 무엇 | 자리 |
|------|------|
| glTF 원본 | `Resource/<도메인>/models_raw/<이름>.glb` |
| 임포트 결과 | `Resource/<도메인>/models/<이름>.mesh` (`App --import-models` 가 쓴다 — `models_raw/import.stamp` 도 함께 커밋) |
| 소켓 초안 | `Resource/<도메인>/models/<이름>.sockets.xml` — **파일이 없을 때만** 쓴다 |
| 메시 id | `<도메인>/models/<이름>.mesh` — `MeshComponent::_meshId` 에 적는 값 |

## 규약

- **축 · 단위**: Blender(오른손 · +Z 위 · 앞 -Y) → glTF(+Y 위, 내보내기의 `export_yup`) → 엔진(왼손 · +Y 위 · 앞 +Z, 임포터가 X 를 뒤집는다). 메시는 애드온이
  손대지 않는다. 단위는 미터 그대로다.
- **소켓**: 이름이 `SOCKET_<이름>` 인 엠프티(또는 사용자 속성 `sw_socket` 이 있는 엠프티). 본에 부모를 둔 엠프티는 그 본 기준(`parent="<본>"`), 아니면 뿌리 기준이다.
  사용자 속성 `sw_socket_kind`(기본 `Attach` — `engine/character/default.socketkinds.xml` 의 종류) · `sw_socket_preview`(미리보기 메시 id)를 읽는다.
  회전은 엔진이 읽는 꼴(`rotation="피치 요 롤"` 도, `quaternion::createFromYawPitchRoll`)로 쓴다. Blender 의 `.001` 꼬리는 뗀다.
- **소켓은 사람이 고치는 원본이다**: 임포트 산출물(`.mesh`)과 따로 두는 이유가 재임포트가 지우지 않게 하려는 것이므로, 애드온도 있는 파일을 덮어쓰지 않는다
  (엔진 `SocketImportUtil::writeIfMissing` 과 같은 규칙). 다시 뜨려면 지우고 내보낸다.
- **이름**: 파일 이름은 `Config/Editor/AssetValidationRules.json` 의 `file-names` 규칙과 같은 문자 집합이다 — 내보낸 뒤 `py -3 -m Scripts validate-assets` 가 깨끗해야 한다.
- **원점**: 임포터의 규칙(`Config/Editor/ModelImportConfig.json` 의 `recenter`)이 메시를 옮기면 뿌리 기준 소켓도 같이 옮겨야 한다 — 그런 팩은 소켓을 본에 둔다.

## 시험

Blender 는 CI 에 없다. bpy 를 쓰는 것은 `__init__.py`(오퍼레이터 · 패널 · 설정)뿐이고, 규약 · 좌표계 변환 · 소켓 XML · 임포트 명령은 bpy 없는 모듈
(`Conventions.py` · `SocketXml.py` · `EngineImport.py`)에 있어 `Test/PythonTest/TestBlenderExporter.py`(`ctest -R PythonTest_TestBlenderExporter`)가 본다.
좌표계 변환은 엔진 `quaternion::createFromYawPitchRoll` 을 옮긴 식과 대조한다.
