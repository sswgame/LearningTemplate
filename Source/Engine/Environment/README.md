# Environment (지형 · 식생 · 물 · 배치 규칙)

월드의 배경 시스템입니다. 모두 **컴포넌트(Object, 티어 6)가 메시 · 머티리얼(Graphics, 티어 5)로 그리는 기능**이라 티어 7(Scene · Sequencer 와 같은 줄)에 둡니다.
씬(`Scene`)을 보지 않고 오브젝트 매니저(`GameObjectManager`)만 봅니다 — 렌더러는 이 폴더를 모르고, 그려지는 것은 모두 프리미티브 등록부의
`MeshInstanceBatch` 로 GpuScene · GPU 컬링 · 멀티 드로우를 메시 컴포넌트와 같은 길로 지납니다(새 패스 · 새 렌더러 코드 없음 — 머티리얼 셰이더만 새로 있다).

상위: [Engine/README.md](../README.md) · 셰이더 규칙: [Graphics/README.md](../Graphics/README.md)

| 폴더 | 무엇 |
|------|------|
| `Placement/` | 규칙 기반 배치 코어 — `PlacementRule`(데이터) · `PlacementScatter`(계산) · `IPlacementSurface`(표면) · `PlacementTileSurface`(2D 타일 표면). 2D · 3D 공용 |
| `Terrain/` | 높이장 에셋(`HeightfieldData`, `.heightfield`) · CPU 질의(`TerrainHeightfield`) · 청크 LOD 메시(`TerrainMeshBuilder`) · `TerrainComponent` |
| `Foliage/` | `FoliageComponent`(규칙 배치 + GPU 인스턴스 + 셀 컬링) · `WindComponent`(전역 바람) · `FoliageInfluencerComponent`(풀을 눕히는 구) |
| `Water/` | `WaterWaveMath`(거스트너 CPU 판) · `WaterBodyComponent`(호수 · 강, 수면 질의, 물속 안개 값) |
| 루트 | `EnvironmentUtil`(보는 카메라 위치 · `-gv_environmentAnimate`) · `EnvironmentMaterial`(캐시에서 잡은 머티리얼 + 컴포넌트 몫 인스턴스) |

## 배치 규칙 — 2D · 3D 공용 코어

`PlacementScatter::scatter( 규칙, 평면 사각형, 표면, 제외 영역, 항목 비중, 결과 )` 하나입니다. 평면 좌표 (u, v) 위에서 돌고, 3D 는 (x, z) + 높이(지형),
2D 는 (x, y) + 타일 레이어입니다. 후보 수 = 밀도 × 면적, 후보마다 **(씨앗, 번호, 칸) 해시**로 난수를 뽑으므로 같은 입력은 비트까지 같은 결과이고,
필터 하나를 바꿔도 남는 자리는 그대로입니다. 순서: 제외 영역(원 · 사각형 기둥) → 표면(구멍 · 맵 밖) → 경사 · 높이 · 레이어 필터 → 밀도 레이어(가중치 확률)
→ 최소 거리(격자 해시 다트 던지기 — 먼저 놓인 것이 이긴다) → 항목 · 크기 · 요 · 노멀 맞춤. 같은 코어를 쓰는 곳: `FoliageComponent`(GPU 인스턴스),
GameFramework `PropScatterComponent` 의 `Rules` 모드(게임플레이 오브젝트), 2D 타일 흩뿌리기(`PlacementTileSurface`).
쿠킹 때 인스턴스를 굽지 않습니다 — 결정적이라 로드 때 다시 계산해도 같은 숲입니다.

## 지형

- **에셋**: 원본은 `<domain>/heightfields_raw/<이름>.png`(16 비트 회색) 또는 `.r16`(작은 엔디언 정사각형) + 곁 `<이름>_holes.png`(128 미만 = 구멍 칸).
  `App --import-heightfields`(확인만: `--check-heightfields`)가 `heightfields/<이름>.heightfield` 로 임포트하고 `import.stamp` 를 남깁니다(텍스처 · 모델과 같은 절차,
  `Editor/Common/Asset/HeightfieldImporter`). 형식은 `HeightfieldData.h` 머리 주석(매직 `SWHF`, 판 1, 높이 N², 구멍 (N−1)²). 높이의 월드 값(최저 · 최고)과 넓이는
  컴포넌트 값입니다.
- **레이어**: 스플랫 맵(RGBA = 레이어 0..3 가중치)은 텍스처(`textures_raw/*_splat.png` → 비압축 RGBA8 DDS, 규칙 `Terrain_Weights`)이고 CPU 도 같은 DDS 를
  읽습니다(`computeLayerWeightsAt` — 셰이더와 같은 쌍선형 + 합 1 정규화). 레이어 모양은 디테일 맵 한 장(채널마다 레이어 하나, 0.5 중립, `Terrain_Detail`)이고,
  레이어마다 색 · 디테일 세기 · 3 축 투영(절벽)을 `TerrainLayer` 로 적습니다. 가파른 면을 한 레이어로 덮는 절벽 덮기(`_cliffLayer`)는 그림에만 있습니다(배치는 경사 필터로).
  머티리얼 텍스처 칸이 넷(t5..t8)이라 레이어 텍스처를 레이어마다 따로 두지 않습니다.
- **LOD**: 지오 밉맵. 청크(한 변 2^k 칸)마다 카메라 거리로 LOD(거리 두 배마다 하나)를 고르고 10 % 히스테리시스를 둡니다. 이웃이 더 거친 변은 고운 쪽이
  정점을 거친 격자로 **접어**(넓이 0 삼각형은 버림) 맞닿은 변의 정점 집합이 비트까지 같습니다 — 칸 크기 · 원점이 이진 소수로 정확해야 비트가 같습니다(1 m · 0.5 m 칸).
  LOD 나 이웃 LOD 가 바뀐 청크만 메시를 다시 만들어 `MeshInstanceBatch::setMesh` 로 갈아 끼웁니다.
- **질의**(물리 · 배치 · 게임): `findHeightAt` · `findNormalAt` · `isHoleAt` · `computeLayerWeightsAt` · `getHeightfield().getHeightSamples()`. 높이는 메시와 같은
  대각선 (0,0)–(1,1) 삼각형 보간이라 그려진 땅과 같습니다.

## 식생

`FoliageLayer` = 배치 규칙 + 메시 목록(`.mesh` 또는 내장 `GrassClump`) + 머티리얼(`foliage.material` 그림자 있음 · `grass.material` 그림자 없음) + 페이드 · 바람 반응 ·
흔들림 높이 · 밝기 흔들기. (레이어, 메시, 셀)마다 배치 하나 — 셀(`_cellSize`)은 CPU 거리 컬링 단위(페이드 끝보다 멀면 배치를 숨긴다)이고 셀 안은 GPU 컬링이 인스턴스마다
거릅니다. 정점 셰이더(`foliage.hlsl`)가 바람(방향 · 세기 · 돌풍, 위상은 인스턴스 위치의 정수 해시), 휘게 하는 구 넷(카메라에 가까운 순), 거리 페이드(뿌리 쪽으로 줄임)를
줍니다. 값은 틱마다 레이어 머티리얼 인스턴스에 싣습니다 — 패스 상수버퍼 · 렌더러를 건드리지 않습니다.
**그림자 · 깊이 프리패스는 흔들리지 않습니다**(`shadowdepth.hlsl` 이 그립니다): 풀은 `MATERIAL_SHADOW_CAST_OFF` 로 그림자를 끄고, 흔드는 머티리얼은
`MATERIAL_VERTEX_DEFORM` 으로 깊이 프리패스에서 빠집니다.

## 물

`WaterBodyComponent` — 호수(오너 중심 사각)와 강(점 목록을 잇는 띠, 점의 y 가 수면). 파도는 월드 (x, z) 의 거스트너 넷(`GerstnerWave`)이고 CPU(`WaterWaveMath`)와
정점 셰이더(`gerstner.hlsli`)가 같은 식 · 같은 순서입니다(`RenderPassGpuTest.WaterWaveShaderMatchesCpu` 가 컴퓨트 프로브로 네 백엔드를 대조). 수면 높이 질의는 그 자리로
옮겨 오는 원점을 고정점 반복으로 찾습니다(부력용). 반투명 패스에는 장면 깊이 · 색 입력이 없어 굴절 · 화면 공간 두께는 없고, 깊이는 메시를 만들 때 지형에서 재어
정점 색 알파에 굽습니다(깊이 색 · 불투명도 · 물가 거품). 프레넬 · 반사광 · 잔물결은 픽셀 셰이더입니다. 물속 안개 값은 `findUnderwaterFog` 가 주고, 그 값을 쓰는 안개 패스는
하늘 · 시간 · 높이 안개 일과 함께 들어옵니다.

## 주의

- **머티리얼은 정점 셰이더만 읽는다**(식생 · 물) — GL(ARB_gl_spirv)은 구조버퍼를 두 단계에서 읽으면 링크를 거절합니다. 픽셀이 필요한 값은 보간 칸으로 넘기고,
  머티리얼 스키마는 정점 스테이지에서 찾습니다(`Material::ensureShaderLayout`).
- **틱은 병렬이다** — 컴포넌트는 틱에서 자기 배치 · 자기 머티리얼 인스턴스만 씁니다. 배치를 등록부에 넣고 빼는 다시 만들기(오너 이동 · 시작 때 지형 다시 찾기)는
  `executeOrDeferPostTick` 으로 틱 뒤에 합니다.
- **스크린샷 비교는 `-gv_environmentAnimate=0`** — 바람 · 파도 시간이 0 에 멈춥니다. 씬은 비동기로 읽히므로 `-gv_screenshotFrame` 은 로드가 끝난 뒤여야 합니다
  (Debug 쇼케이스는 1600 프레임).
- **OpenGL 은 메시를 게임 스레드에서 인라인으로 만든다**(GpuUploadQueue) — 렌더 스레드가 셰이더를 실시간 컴파일하느라 오래 쥐고 있으면 컨텍스트 대기가 시간을 넘겨
  `[Error]` 를 남깁니다. LOD 교체가 런타임에 메시를 만드는 첫 사용자입니다. 쿠킹한 셰이더로는 나지 않습니다(백로그 1-3).

## 쇼케이스

`game/empty/maps/envshowcase.scene.xml` — 256 m 계곡(구릉 · 절벽 · 동굴 구멍) + 호수 + 호수로 흘러드는 강 + 풀 · 꽃 · 덤불 · 나무 · 쓰러진 나무(Kenney Nature Kit, CC0).
`App.exe "-gv_firstScene=game/empty/maps/envshowcase.scene.xml"`. 원본 데이터는 `Scripts/dev/GenerateTerrainShowcase.py` 가 만듭니다.
2026-10-04 Release(1280×720, 다른 빌드 열한 개가 같은 기계를 쓰는 중이라 꼬리가 길다): 식생 19,640 인스턴스 · 셀 배치 283 개를 로드 때 46 ms 에 계산,
씬 instantiate 71–87 ms. DX12 프레임 p50 2.6 ms · p99 3.4–8.4 ms(GPU 가 묶는다 — 그림자 패스 p50 1.4 ms 가 가장 크다: 그림자를 끈 풀도 정점 셰이더는 돈다),
Vulkan p50 3.7 ms.

## 물리(Jolt)에 이을 것

- 지형 → Jolt `HeightFieldShape`: `TerrainHeightfield::getHeightSamples()`(N², 행 우선 z 바깥 · x 안쪽, 월드 높이) · `getResolution()` · `getCellSize()` ·
  `getOrigin()` · 구멍은 `getHoleCells()`((N−1)² — Jolt 는 샘플 단위 `cNoCollisionValue` 라 구멍 칸의 네 샘플 중 하나를 그 값으로). 원점은 샘플 (0, 0) 이다.
- 부력: `WaterBodyComponent::findWaterAt( manager, x, z )` → `computeSurfaceHeight` · `computeSurfaceNormal` · `isUnderwater( 점, 깊이 )`. 시간은 물 컴포넌트의
  `getWaveTime()` 이 기준이다(고정 스텝 물리는 같은 시간을 넘길 것).
