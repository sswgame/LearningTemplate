# Empty — 게임 팩

빈 게임(`Source/Games/Empty`)의 리소스입니다. 엔진 기능을 하나씩 보여 주는 쇼케이스 씬이 여기 있습니다 — 시작 씬 대신 열려면 `-gv_firstScene=<씬 경로>` 를 줍니다.

## 환경 쇼케이스 (`maps/envshowcase.scene.xml`)

지형 · 호수 · 강 · 풀 · 나무. 원본은 `heightfields_raw/` · `textures_raw/`(`Scripts/dev/MakeTerrainShowcase.py` 가 만든다)와 `models_raw/`(Kenney Nature Kit, CC0 — `credits.md`)입니다.

```powershell
build/Ninja-Debug/Bin/App.exe "-gv_firstScene=game/empty/maps/envshowcase.scene.xml"
```

## 툰 쇼케이스 (`maps/toonshowcase.scene.xml`)

셀 셰이딩과 외곽선을 한 화면에서 봅니다 — 같은 색의 PBR 구(`materials/toonshowcase_lit.material`, forwardlit)와 툰 구(`materials/toonshowcase_toon.material` —
칼같은 계단 · 보랏빛 그림자색 · 림 · 화면 기준 외곽선), 그 사이에 VRoid 캐릭터(`models_raw/avatarsample_d.vrm` 을 임포트한 머티리얼 구간 17 개, 레퍼런스 포즈),
왼쪽 앞 위에서 비스듬히 드는 주광과 그림자를 받는 바닥. 툰은 톤맵 · 블룸이 없는 파이프라인으로 봅니다(톤맵은 흰색을 0.5 로 누르고 블룸은 계단을 번지게 한다).

```powershell
build/Ninja-Debug/Bin/App.exe -dx12 "-gv_firstScene=game/empty/maps/toonshowcase.scene.xml" "-gv_renderPipeline=engine/pipeline/forwardtoonpipeline.xml"
```

VRM 을 다시 임포트하면 `App --import-models` 뒤에 `App --import-textures` 를 돌립니다(모델 임포트가 VRM 의 내장 이미지를 `textures_raw/avatarsample_d/` 로 꺼낸다).

## 2D 데모 (`maps/twod.scene.xml`)

엔진의 2D 기능을 한 화면에 모은 씬입니다 — 정렬 레이어(하늘 · 언덕 Background, 타일 Default, 주인공 Foreground, 패널 · HP 바 WorldUI),
시차 레이어(하늘 0 · 언덕 0.4 · 되풀이 8), 픽셀 퍼펙트 카메라(PPU 16 · 기준 320 × 180 · 자르기 · 스냅), 규칙 · 애니메이션 타일맵(`maps/twod.tilemap.xml` +
`tilesets/demo.tileset.xml`, 노멀 아틀라스, 병합 충돌), 9-슬라이스 패널, 2D 빛(횃불 · 스폿 · 전역)과 그림자(타일 외곽선 · 상자).

```powershell
build/Ninja-Debug/Bin/App.exe -dx12 "-gv_firstScene=game/empty/maps/twod.scene.xml" "-gv_renderPipeline=engine/pipeline/forward2dpipeline.xml"
```

텍스처(`textures/twod/*.dds`)는 픽셀 아트를 코드로 그려 DDS 로 쓴 것입니다(원본 이미지 없음 — 엔진 시험 텍스처와 같은 자리).

