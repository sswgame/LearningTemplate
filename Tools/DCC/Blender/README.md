# Blender 내보내기 애드온

## 이것은 무엇이고 왜 있나

Blender에서 만든 모델을 엔진에 넣으려면 glTF로 내보내고, 축과 이름 규칙을 맞추고, 임포트 명령을 돌려야 합니다. 이 애드온(`sw_engine_exporter/`)은 그 과정을 버튼 하나로 합니다.

- 고른 오브젝트, 아마추어, 애니메이션을 엔진 규약대로 glTF(`models_raw/<이름>.glb`)로 내보냅니다.
- 이름이 `SOCKET_` 으로 시작하는 엠프티를 소켓 초안 파일(`<이름>.sockets.xml`)로 씁니다.
- `App --import-models` 를 실행해 `.mesh` 로 임포트합니다.

언리얼의 FBX 소켓 규약(`SOCKET_` 엠프티가 스태틱 메시 소켓이 됨)과 Datasmith의 "DCC 툴에서 버튼 하나로 엔진까지"를 이 엔진의 glTF 경로에 맞춘 것입니다.

## 따라 해 보기 — 칼 하나 내보내기

**1단계 — 설치합니다.** Blender 3.6 이상에서 Edit > Preferences > Add-ons > Install… 을 열고, `sw_engine_exporter` 폴더를 zip으로 묶어 고릅니다.
폴더를 Blender의 `scripts/addons/` 에 심볼릭 링크로 걸면 저장소를 고칠 때 바로 반영됩니다.

**2단계 — 저장소 위치를 알려 줍니다.** 애드온 설정의 **Engine repository** 에 저장소 루트(`Resource/` 와 `build/` 가 있는 폴더)를 적습니다.
애드온은 `build/Ninja-Debug/Bin/App.exe` 부터 App을 찾습니다(`Scripts/common/AppBinary.py` 와 같은 순서). 다른 App을 쓰려면 **App executable** 에 적습니다.
임포트는 에디터 모듈이 하므로 Dev 빌드의 App이어야 합니다. Shipping App은 임포트하지 못합니다.

**3단계 — 소켓을 답니다.** 칼 손잡이 위치에 엠프티를 두고 이름을 `SOCKET_Grip` 으로 짓습니다. 아마추어의 본에 부모를 걸면 그 본 기준 소켓이 됩니다.

**4단계 — 내보냅니다.** File > Export > **SW Engine (.glb)** 를 고르거나, 3D 뷰 사이드바(N)의 **SW Engine** 탭을 씁니다.
**Pack** 에는 `Resource/game/<팩>` 의 팩 이름(`shooter3d` 등)이나 `engine` 을, **Asset name** 에는 에셋 이름을 적습니다. 기본은 활성 오브젝트 이름이고, 리소스 이름 규칙(소문자, 숫자, `_`, `-`)으로 바뀝니다.

| 결과 | 위치 |
|---|---|
| glTF 원본 | `Resource/<도메인>/models_raw/<이름>.glb` |
| 임포트 결과 | `Resource/<도메인>/models/<이름>.mesh` |
| 소켓 초안 | `Resource/<도메인>/models/<이름>.sockets.xml`(파일이 없을 때만 씀) |
| 메시 id | `<도메인>/models/<이름>.mesh`. `MeshComponent::_meshId` 에 적는 값 |

**5단계 — 확인하고 커밋합니다.** `py -3 -m Scripts validate-assets` 가 깨끗한지 봅니다. `.glb`, `.mesh`, `.sockets.xml` 과 함께 `models_raw/import.stamp` 도 커밋합니다.

## 작동 원리 — 규약

**축과 단위.** Blender는 오른손 좌표계에 +Z가 위, 앞이 -Y입니다. 내보내기의 `export_yup` 이 glTF(+Y 위)로 바꾸고, 엔진 임포터(`ModelImporter::convertToEngineSpace`)가 X를 뒤집어 엔진 좌표계(왼손, +Y 위, 앞 +Z)로 바꿉니다.
애드온은 메시 정점을 직접 고치지 않습니다. 단위는 미터 그대로입니다.

**소켓.** 이름이 `SOCKET_<이름>` 인 엠프티, 또는 사용자 속성 `sw_socket` 이 있는 엠프티가 소켓입니다. Blender의 `.001` 같은 꼬리는 뗍니다.
본에 부모가 있으면 그 본 기준(`parent="<본>"`), 아니면 뿌리 기준입니다. 사용자 속성 두 개를 더 읽습니다.

- `sw_socket_kind` 는 소켓 종류입니다. 기본은 `Attach` 이고, 종류 목록은 `Resource/engine/character/default.socketkinds.xml` 에 있습니다.
- `sw_socket_preview` 는 에디터에서 보여 줄 미리보기 메시 id입니다.

회전은 엔진이 읽는 형식(`rotation="피치 요 롤"`, 도 단위, `quaternion::createFromYawPitchRoll` 순서)으로 씁니다.

**이름.** 파일 이름은 `Config/Editor/AssetValidationRules.json` 의 `file-names` 규칙과 같은 문자 집합을 씁니다.

**코드 구성.** bpy를 쓰는 코드는 `__init__.py`(오퍼레이터, 패널, 설정)뿐입니다. 규약과 좌표 변환은 `Conventions.py`, 소켓 XML은 `SocketXml.py`, 임포트 명령은 `EngineImport.py` 에 있고 bpy 없이 동작합니다.
그래서 Blender가 없는 CI에서도 `Test/PythonTest/TestBlenderExporter.py`(`ctest -R PythonTest_TestBlenderExporter`)가 이 부분을 테스트합니다.
좌표 변환은 엔진의 `quaternion::createFromYawPitchRoll` 을 옮긴 식과 비교합니다.

## 함정과 주의

- **이미 있는 소켓 파일은 덮어쓰지 않습니다.** 소켓 파일은 사람이 고치는 원본입니다. 임포트 결과(`.mesh`)와 따로 두는 이유가 재임포트가 지우지 않게 하려는 것이므로, 애드온도 엔진(`SocketImportUtil::writeIfMissing`)과 같은 규칙을 따릅니다.
  초안을 다시 만들려면 파일을 지우고 내보냅니다.
- **임포트 규칙이 원점을 옮기면 뿌리 기준 소켓이 어긋납니다.** `Config/Editor/ModelImportConfig.json` 의 `recenter` 가 메시를 옮기는 팩에서는 소켓을 본에 답니다.
- **Shipping App으로는 임포트할 수 없습니다.** 임포트는 에디터 모듈의 기능입니다.

## 더 볼 곳

- 모델 임포트 규칙: `Config/Editor/ModelImportConfig.json`
- 소켓 형식: `Source/Engine/Character/Socket/SocketImportUtil.h`
- 테스트: `Test/PythonTest/TestBlenderExporter.py`
