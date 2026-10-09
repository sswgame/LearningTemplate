# Empty — 게임 팩

기본 게임 `Empty`(`Source/Games/Empty`)의 리소스입니다. 게임 로직은 거의 없고, 엔진 기능을 하나씩 보여 주는 쇼케이스 씬이 이 팩에 모여 있습니다.

기본으로 열리는 씬은 `maps/editortest.scene.xml` 이고, `data/gamesettings.xml` 의 `startMap` 이 정합니다.
다른 씬을 열려면 `-gv_firstScene=<씬 경로>` 를 줍니다. 경로는 `game/empty/maps/...` 처럼 도메인을 포함한 리소스 경로입니다.

| 씬 | 보여 주는 것 | 설명이 있는 곳 |
|---|---|---|
| `maps/envshowcase.scene.xml` | 지형, 호수, 강, 풀, 나무 | 아래 |
| `maps/toonshowcase.scene.xml` | 셀 셰이딩과 외곽선 | 아래 |
| `maps/twod.scene.xml` | 2D 기능 전체 | 아래 |
| `maps/destructionshowcase.scene.xml` | 파괴 | [Destruction](../../../Source/Engine/Destruction/README.md) |
| `maps/gimmickshowcase.scene.xml` | 발판, 횃불, 스프링 같은 기믹 | [Gimmick](../../../Source/GameFramework/Base/Gameplay/Gimmick/README.md) |
| `maps/spriteui.scene.xml` | 머리 위 HP 바 같은 월드 UI | [GameFramework](../../../Source/GameFramework/README.md) |

## 환경 쇼케이스

```powershell
build/Ninja-Debug/Bin/App.exe "-gv_firstScene=game/empty/maps/envshowcase.scene.xml"
```

지형, 호수, 강, 풀, 나무를 한 씬에서 봅니다.
지형 원본은 `heightfields_raw/` 와 `textures_raw/` 에 있고, `Scripts/dev/MakeTerrainShowcase.py` 가 만듭니다.
나무와 풀 모델의 원본은 `models_raw/` 의 Kenney Nature Kit입니다. 라이선스는 CC0이고 출처는 `credits.md` 에 있습니다.

## 툰 쇼케이스

```powershell
build/Ninja-Debug/Bin/App.exe -dx12 "-gv_firstScene=game/empty/maps/toonshowcase.scene.xml" "-gv_renderPipeline=engine/pipeline/forwardtoonpipeline.xml"
```

셀 셰이딩과 외곽선을 한 화면에서 비교합니다. 씬에는 다음이 있습니다.

- 같은 색의 두 구. 하나는 PBR 머티리얼(`materials/toonshowcase_lit.material`, forwardlit 셰이더)이고, 다른 하나는 툰 머티리얼(`materials/toonshowcase_toon.material`)입니다.
  툰 구는 경계가 날카로운 단계 음영, 보랏빛 그림자 색, 림 라이트, 화면 기준 외곽선을 씁니다.
- 두 구 사이의 VRoid 캐릭터. `models_raw/avatarsample_d.vrm` 을 임포트한 것이고, 머티리얼 구간이 17개입니다. 레퍼런스 포즈로 서 있습니다.
- 왼쪽 앞 위에서 비스듬히 비추는 주 광원과, 그림자를 받는 바닥.

툰 셰이딩은 톤 매핑과 블룸이 없는 파이프라인(`forwardtoonpipeline.xml`)으로 봅니다. 톤 매핑은 흰색을 0.5 정도로 어둡게 만들고, 블룸은 단계 음영의 경계를 번지게 하기 때문입니다.

VRM을 다시 임포트할 때는 `App --import-models` 를 먼저 실행하고 `App --import-textures` 를 실행합니다.
모델 임포트가 VRM 안에 들어 있는 이미지를 `textures_raw/avatarsample_d/` 로 꺼내고, 텍스처 임포트가 그 이미지를 DDS로 만들기 때문입니다.

## 2D 데모

```powershell
build/Ninja-Debug/Bin/App.exe -dx12 "-gv_firstScene=game/empty/maps/twod.scene.xml" "-gv_renderPipeline=engine/pipeline/forward2dpipeline.xml"
```

엔진의 2D 기능을 한 화면에 모은 씬입니다.

- **정렬 레이어.** 하늘과 언덕은 Background, 타일은 Default, 주인공은 Foreground, 패널과 HP 바는 WorldUI 레이어에 있습니다.
- **시차 레이어.** 하늘은 시차 0, 언덕은 0.4이고, 8번 반복해서 그립니다.
- **픽셀 퍼펙트 카메라.** 유닛당 픽셀(PPU)은 16, 기준 해상도는 320 × 180이고, 화면 자르기와 픽셀 스냅을 켰습니다.
- **타일맵.** 규칙 타일과 애니메이션 타일을 씁니다. 맵은 `maps/twod.tilemap.xml`, 타일셋은 `tilesets/demo.tileset.xml` 입니다. 노멀 아틀라스를 쓰고, 충돌체는 병합합니다.
- **9-슬라이스 패널.**
- **2D 라이트와 그림자.** 횃불, 스폿, 전역 라이트가 있고, 타일 외곽선과 상자가 그림자를 만듭니다.

이 씬의 텍스처(`textures/twod/*.dds`)는 원본 이미지 없이 코드로 픽셀 아트를 그려 DDS로 쓴 것입니다. 엔진의 테스트 텍스처와 같은 방식입니다.
