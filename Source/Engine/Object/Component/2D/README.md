# Object/Component/2D — 2D 컴포넌트

스프라이트 · 2D 충돌 · 2D 렌더 기능의 컴포넌트입니다. 렌더 데이터(정렬 레이어 표 등)는 [Graphics/2D](../../../Graphics/2D/README.md) 에 있습니다.

| 파일 | 역할 |
|------|------|
| `SpriteComponent` | 텍스처를 입힌 사각형(아틀라스 프레임 · 색 · 정렬 레이어 · 레이어 안 순서 · 그리기 방식 Simple/Sliced/Tiled) |
| `SpriteAnimatorComponent` | 클립(`.sprite.json`)의 프레임을 넘김 |
| `PixelPerfectCameraComponent` | 픽셀 아트 카메라 — 정수 배율 · 직교 높이 · 그리는 눈의 화면 픽셀 스냅 · 스프라이트의 자산 픽셀 스냅 · 레터박스 띠 |
| `TileMapRendererComponent` | 타일 레이어(맵 XML `<tileLayer>` + `.tileset.xml`) — 칸마다 스프라이트 · 규칙 · 애니메이션 타일 · 병합 바디 · 외곽선 · 이동 비용 |
| `ParallaxLayerComponent` | 시차 레이어 — 게임 카메라에 대한 배율로 오브젝트(자식 포함)를 옮기고 되풀이 길이로 감는다 |
| `SpriteRenderUtil` | 스프라이트 머티리얼 경로 · (머티리얼, 텍스처) 인스턴스 공유 · 정렬 키 풀기 |
| `PointLight2DComponent` · `GlobalLight2DComponent` (`Light2DComponent.h`) | 2D 빛 — 점 · 스폿(안/바깥 반경 · 각 · 감쇠 지수 · 노멀 맵 높이 · 그림자) · 전역 바탕 빛 |
| `ShadowCaster2DComponent` | 2D 그림자 가림막 — 상자, 또는 같은 오브젝트의 타일맵 외곽선 |
| `BoxCollider2DComponent` | 2D 충돌 |

## 정렬 — 유니티 · Godot 와 견줘

| 기능 | 유니티 | Godot | 여기 |
|------|--------|-------|------|
| 정렬 레이어 표 | Tags & Layers › Sorting Layers | CanvasLayer 순서 | `render2d.xml` `<SortingLayers>` (게임 팩이 통째로 대신할 수 있음) |
| 레이어 안 순서 | Order in Layer (−32768..32767) | `z_index` | `SpriteComponent::_orderInLayer` (−32767..32767) |
| 같은 순서의 앞뒤 | Transparency Sort Mode (Default · Perspective · Orthographic · Custom Axis) | 트리 순서 · `y_sort_enabled` | `TransparencySortMode` Auto · Distance · ViewAxis · CustomAxis, 그다음 등록 순서 |
| 월드 공간 UI | Canvas 의 Sorting Layer | CanvasLayer | `SpriteInstanceBatch::setSorting` — HP 바 · 데미지 숫자는 `WorldUI` |

## 그리기 방식 — 9-슬라이스 · 타일

| 기능 | 유니티 | Godot | 여기 |
|------|--------|-------|------|
| 그리기 방식 | `SpriteRenderer.drawMode` Simple · Sliced · Tiled | `NinePatchRect`(UI) · `Sprite2D` region | `SpriteComponent::_drawMode` Simple · Sliced · Tiled |
| 테두리 | Sprite Editor 의 Border(픽셀) | patch margin(픽셀) | 클립 프레임의 `"border"`(프레임 비율), 클립 없는 텍스처 스프라이트는 `_sliceBorder` |
| 크기 | `SpriteRenderer.size` | `size` | `_size`(로컬 월드 크기). Simple 은 트랜스폼 스케일 |
| 타일 방식 | Continuous · Adaptive | Stretch · Tile · Tile Fit | 자연 크기 칸 되풀이 + 마지막 칸 자르기(Continuous) |

빠진 것: 테두리를 픽셀로 적는 길(아틀라스 픽셀 크기를 런타임이 모른다), 유니티의 Adaptive 타일, 가운데를 비우는 Fill Center 끄기.

## 픽셀 퍼펙트 카메라

| 기능 | 유니티 Pixel Perfect Camera | Godot | 여기 |
|------|-----------------------------|-------|------|
| 정수 배율 · 직교 크기 | Assets PPU · Reference Resolution | stretch mode `viewport` + integer scale | `_pixelsPerUnit` · `_referenceResolution` → 배율 = 두 축이 들어가는 가장 큰 정수, 직교 높이 = 뷰포트 높이 / (배율 × PPU) |
| 카메라 스냅 | 렌더 직전 반올림 | `snap_2d_transforms_to_pixel` | 그리는 눈만(`CameraComponent::setViewOffset`) 화면 픽셀 격자에 — 트랜스폼은 그대로 |
| 스프라이트 스냅 | Pixel Snapping(자산 픽셀 격자) | `snap_2d_vertices_to_pixel` | `_bPixelSnapping` → 인스턴스 칸 `_pixelSnap` = 1 / PPU, sprite2d.hlsl 이 원점을 붙인다 |
| 점 필터 | 텍스처 Filter Mode Point | `TEXTURE_FILTER_NEAREST` | `sprite2dpixel.material`(`pointFilter`) — UV 를 텍셀 중심에 붙인다(GL 결합 샘플러 때문에 샘플러가 아니라 UV) |
| 자르기 | Crop Frame X/Y | — | `_bCropX` · `_bCropY` → 검은 띠 넷(`Overlay` 레이어 맨 위) |
| 저해상도로 그린 뒤 키우기 | Upscale Render Texture | viewport stretch | **없음** — 렌더러에 저해상도 타깃 · 업스케일 패스가 필요하다 |

뷰포트 크기는 엔진 루프가 틱 전에 카메라 등록부에 적는다(`CameraRegistry::setViewportSize`). 틱 그룹은 PostPhysics(카메라를 따라가게 하는 컴포넌트 뒤, 시차 레이어 앞)입니다.
스냅 단위가 바뀌면 씬의 메시 모두에 틱 뒤로 미뤄 알리고, 나중에 등록되는 메시는 등록부에서 읽습니다.

## 타일맵

| 기능 | 유니티 | Godot | 여기 |
|------|--------|-------|------|
| 타일셋 | Tile Palette · Tile asset | TileSet(atlas source) | `.tileset.xml` — 아틀라스 칸 격자 · `<Tile>` · `<RuleTile>` (`Utility/TileMap/TileSetAsset`) |
| 자동 타일 | Rule Tile(이웃 This/NotThis, 첫 규칙이 이김) | terrain set(peering bits) | `<Rule pattern=".x. xoo .o." cell="n"/>` — 이웃 여덟 칸 `o` 같음 · `x` 다름 · `.` 상관없음, 첫 규칙이 이김, `outside="same"` 으로 맵 밖을 같은 타일로 |
| 애니메이션 타일 | Animated Tile | 타일 animation frames | `frames="12 13 14 15" fps="4"` |
| 칠하기 | Tile Palette 브러시 | TileMap 에디터 | 에디터 TileMapPanel 의 Tile 레이어 — 칸에 브러시 이름을 칠하고 규칙이 고른 칸 번호를 보인다. 런타임 `setTileBrush` |
| 맵 저장 | 칸마다 타일 참조 | 칸마다 (source, atlas coords) | 맵 XML `<tileLayer tileSet=...>` 팔레트(브러시 이름) + 칸 번호 — 모습은 저장하지 않고 읽을 때 규칙으로 고른다 |
| 충돌 | TilemapCollider2D + CompositeCollider2D | physics layer | 단단한 브러시 → 줄 병합 사각형 바디(`PhysicsWorld`) + 바깥쪽 방향이 있는 외곽선(2D 그림자 가림막) |
| 내비게이션 | NavMeshPlus 등 외부 | navigation layer | `computeNavCosts` — GameFramework `NavGrid` 와 같은 값(10 보통 · 255 막힘 · 브러시 `navCost`) |

빠진 것: 청크(한 레이어 65536 칸 상한 — 넘으면 레이어를 나눈다), 편집 중(플레이 전) 미리보기(배치는 `onBeginPlay` 에서 만든다), 에디터 칸에 아틀라스 그림
대신 칸 번호, 회전 · 뒤집기 규칙, 다각형(비사각) 충돌 모양.

## 2D 빛 · 그림자

| 기능 | 유니티 URP 2D | Godot | 여기 |
|------|---------------|-------|------|
| 빛 받는 스프라이트 | Sprite-Lit-Default | CanvasItem + light_mask | `engine/materials/sprite2dlit.material`(`sprite2dlit.hlsl` + `lighting2d.hlsli`). 2D 빛이 하나도 없으면 빛 없이 그대로 그린다 |
| 점 · 스폿 빛 | Light 2D Point(안/바깥 반경 · 각 · Falloff Intensity) | PointLight2D(텍스처) | `PointLight2DComponent` — 감쇠 ((바깥 − d)/(바깥 − 안))^지수, 원뿔은 로컬 +X |
| 전역 빛 | Global Light 2D | CanvasModulate | `GlobalLight2DComponent` |
| 노멀 맵 | Secondary Texture `_NormalMap` + Normal Map Distance | normal_texture | `SpriteComponent::_normalMapName` · 타일셋 `normalAtlas`, 빛의 `_normalMapHeight` |
| 그림자 | Shadow Caster 2D(+ Composite, Self Shadows) | LightOccluder2D | `ShadowCaster2DComponent`(상자 · 타일맵 외곽선) — 빛을 등진 토막만 가려 자기 그림자 없음 |
| 빛 모양 | Freeform · Sprite · Parametric 빛, 빛 블렌드 스타일, 빛 텍스처(쿠키) | 텍스처 빛 | **없음** |
| 부드러운 그림자 | Shadow Softness | shadow filter(PCF) | **없음** — 단단한 그림자 |

빛 · 가림막 토막은 3D 와 같은 빛 목록(t12)에 종류를 달리해 실립니다(`SW_LIGHT_TYPE_POINT2D` · `GLOBAL2D` · `SHADOW2D`) — 새 슬롯 · 새 패스가 없어
네 백엔드가 같은 버퍼를 같은 자리에 겁니다. 3D 조명 식은 2D 원소를 건너뛰고, 3D 빛이 하나도 없으면 키라이트로 폴백합니다. 픽셀 하나가 원소 수 × 가림막 토막 수를
도므로 토막은 수백 개까지입니다(한 프레임 원소 상한 1024).

**2D 게임은 `engine/pipeline/forward2dpipeline.xml` 로 그린다**(`-gv_renderPipeline`) — 기본 포워드 파이프라인의 톤맵(Reinhard)은 흰색을 0.5 로 누르고
그림자 맵 패스는 2D 에 쓸모가 없다.

## 시차 레이어

| 기능 | 유니티 | Godot | 여기 |
|------|--------|-------|------|
| 카메라에 대한 배율 | 기본 부품 없음(스크립트) | `ParallaxLayer.motion_scale` | `ParallaxLayerComponent::_scrollFactor`(축마다, 1 = 월드 · 0 = 카메라에 붙음) |
| 끝없이 되풀이 | — | `motion_mirroring` | `_repeatSize`(축마다) — 카메라에 대한 상대 자리를 감는다. 내용은 M 마다 같고 화면 폭 + M 을 덮어야 한다(Tiled 스프라이트 폭 3M) |
| 기준점 | — | `ParallaxBackground.scroll_base_offset` | `_referencePoint` — 카메라가 여기 있을 때 저작한 자리 |

틱은 마지막 그룹(PostUpdate)입니다. 카메라를 움직이는 컴포넌트는 그 앞 그룹에서 돌아야 같은 프레임의 카메라를 봅니다(같은 그룹이면 한 프레임 늦거나
레이스입니다 — 틱 순서는 오브젝트 사이에서 정해지지 않습니다). Godot 처럼 레이어마다 자식을 복제해 그리지 않으므로 되풀이는 내용 쪽이 맡습니다.

정렬 레이어는 투명 큐에만 듣습니다(스프라이트 머티리얼은 투명). 모르는 레이어 이름은 오류를 남기고 `Default` 로 그립니다.
