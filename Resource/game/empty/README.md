# Empty game pack root.
# 게임 팩 리소스를 여기에 둡니다 (data/, maps/, prefabs/ ...).

# 환경 쇼케이스: maps/envshowcase.scene.xml — 지형 · 호수 · 강 · 풀 · 나무.
#   App.exe "-gv_firstScene=game/empty/maps/envshowcase.scene.xml"
#   원본: heightfields_raw/ · textures_raw/ (Scripts/dev/GenerateTerrainShowcase.py) · models_raw/ (Kenney Nature Kit, CC0)

## 2D 데모 (`maps/twod.scene.xml`)

엔진의 2D 기능을 한 화면에 모은 씬입니다 — 정렬 레이어(하늘 · 언덕 Background, 타일 Default, 주인공 Foreground, 패널 · HP 바 WorldUI),
시차 레이어(하늘 0 · 언덕 0.4 · 되풀이 8), 픽셀 퍼펙트 카메라(PPU 16 · 기준 320 × 180 · 자르기 · 스냅), 규칙 · 애니메이션 타일맵(`maps/twod.tilemap.xml` +
`tilesets/demo.tileset.xml`, 노멀 아틀라스, 병합 충돌), 9-슬라이스 패널, 2D 빛(횃불 · 스폿 · 전역)과 그림자(타일 외곽선 · 상자).

```powershell
build/Ninja-Debug/Bin/App.exe -dx12 "-gv_firstScene=game/empty/maps/twod.scene.xml" "-gv_renderPipeline=engine/pipeline/forward2dpipeline.xml"
```

텍스처(`textures/twod/*.dds`)는 픽셀 아트를 코드로 그려 DDS 로 쓴 것입니다(원본 이미지 없음 — 엔진 시험 텍스처와 같은 자리).

