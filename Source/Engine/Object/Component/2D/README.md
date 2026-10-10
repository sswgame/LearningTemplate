# Object/Component/2D — 2D 컴포넌트

## 이것은 무엇이고 왜 있나

2D 게임을 만드는 데 필요한 컴포넌트를 모은 폴더입니다. 스프라이트, 스프라이트 애니메이션, 타일맵, 시차 레이어, 픽셀 퍼펙트 카메라, 2D 빛과 그림자, 2D 충돌이 있습니다.
유니티의 2D 패키지(Sprite Renderer, Tilemap, Pixel Perfect Camera, URP 2D Lights)와 Godot의 2D 노드에 해당합니다.

엔진의 2D는 별도 렌더러가 아니라 3D 렌더러 위에서 돕니다. 스프라이트는 양면 사각형 메시이고, 2D 빛은 3D와 같은 빛 목록에 종류만 달리해 실립니다.
그래서 2D와 3D 물체를 한 장면에 섞을 수 있고, 네 백엔드가 새 패스 없이 같은 결과를 냅니다. 정렬 레이어 설정과 9-슬라이스 메시는 [Graphics/2D 문서](../../../Graphics/2D/README.md)에 있습니다.

## 머릿속 그림

```text
SpriteComponent ─┐
TileMapRenderer ─┼─▶ 인스턴스(프레임, 색, 픽셀 스냅) ─▶ GPUSceneBuilder ─▶ 투명 큐(정렬 키 → 깊이)
                 │
PointLight2D / GlobalLight2D / ShadowCaster2D ─▶ 빛 목록(t12) ─▶ sprite2dlit.hlsl
PixelPerfectCamera ─▶ 카메라의 직교 높이와 화면 픽셀 스냅
ParallaxLayer ─▶ 카메라에 대한 배율로 오브젝트 이동
```

기억할 개념은 세 가지입니다.

**스프라이트는 인스턴스입니다.** 스프라이트의 아틀라스 프레임, 색, 픽셀 스냅은 메시가 아니라 인스턴스 데이터(`GPUSpriteInstanceData`)입니다.
그래서 같은 텍스처를 쓰는 스프라이트는 프레임과 색이 달라도 한 번에 그려집니다.

**정렬 레이어.** 스프라이트는 정렬 레이어 이름과 레이어 안 순서를 가집니다. 투명 큐는 이 둘로 먼저 정렬하고, 같으면 깊이로 정렬합니다.

**틱 그룹.** 카메라를 따라가는 컴포넌트는 순서가 중요합니다. 픽셀 퍼펙트 카메라는 PostPhysics, 시차 레이어는 마지막 그룹인 PostUpdate에서 돕니다.

## 따라 해 보기 — 2D 데모 씬에서 정렬 바꾸기

Empty 게임 팩에는 2D 기능을 한 화면에 모은 데모 씬이 있습니다.

1. 데모를 실행합니다. 2D는 톤 매핑이 없는 `forward2dpipeline.xml` 로 그립니다.

   ```powershell
   build/Ninja-Debug/Bin/App.exe -dx12 "-gv_firstScene=game/empty/maps/twod.scene.xml" "-gv_renderPipeline=engine/pipeline/forward2dpipeline.xml"
   ```

   하늘과 언덕(Background 레이어), 타일맵(Default), 주인공(Foreground), 패널과 HP 바(WorldUI)가 보입니다. 하늘과 언덕은 카메라보다 느리게 움직이는 시차 레이어입니다.
2. `Resource/game/empty/maps/twod.scene.xml` 에서 `Hills` 오브젝트의 `SpriteComponent` 를 찾습니다. `_sortingLayer="Background"` 와 `_orderInLayer="0"` 이 있습니다.
3. `_sortingLayer` 를 `Foreground` 로 바꾸고 다시 실행합니다. 언덕이 주인공 앞에 그려집니다.
   레이어가 레이어 안 순서보다 우선하므로, `_orderInLayer` 만 바꿔서는 Background의 언덕이 Foreground의 주인공을 넘지 못합니다.
4. 확인했으면 원래 값으로 되돌립니다.

코드에서는 `SpriteComponent::setSortingLayer` 와 `setOrderInLayer` 로 같은 값을 바꿉니다.

## 작동 원리

### 컴포넌트

| 컴포넌트 | 하는 일 |
|---|---|
| `SpriteComponent` | 텍스처를 입힌 사각형 |
| `SpriteAnimatorComponent` | 클립(`.sprite.json`)의 프레임을 넘김 |
| `TileMapRendererComponent` | 타일 레이어 그리기, 충돌, 이동 비용 |
| `ParallaxLayerComponent` | 카메라에 대한 시차 이동 |
| `PixelPerfectCameraComponent` | 정수 배율 픽셀 아트 카메라 |
| `PointLight2DComponent`, `GlobalLight2DComponent` | 2D 점, 스폿 빛과 전역 빛 |
| `ShadowCaster2DComponent` | 2D 그림자 가림막 |
| `BoxCollider2DComponent` | 2D 상자 충돌 |

`SpriteRenderUtil` 은 스프라이트 머티리얼 경로를 고르고, 머티리얼과 텍스처 조합마다 인스턴스를 나눠 쓰고, 정렬 키를 풉니다.

### 정렬

| 기능 | 유니티 | Godot | 이 엔진 |
|---|---|---|---|
| 정렬 레이어 목록 | Sorting Layers | CanvasLayer 순서 | `render2d.xml` |
| 레이어 안 순서 | Order in Layer | `z_index` | `_orderInLayer` |
| 같은 순서의 앞뒤 | Transparency Sort Mode | `y_sort_enabled` | `TransparencySortMode` |
| 월드 공간 UI | Canvas의 Sorting Layer | CanvasLayer | `SpriteInstanceBatch::setSorting` |

레이어 안 순서는 −32767..32767입니다. 같은 레이어와 순서 안에서는 `TransparencySortMode`(Auto, Distance, ViewAxis, CustomAxis)의 깊이로, 그다음 등록 순서로 정합니다.
HP 바와 데미지 숫자 같은 월드 공간 UI는 `WorldUI` 레이어에 둡니다. 게임 팩은 `render2d.xml` 을 통째로 대신할 수 있습니다.

### 그리기 방식 — 9-슬라이스와 타일

`SpriteComponent::_drawMode` 는 Simple, Sliced, Tiled 중 하나입니다. 유니티의 `SpriteRenderer.drawMode`, Godot의 `NinePatchRect` 에 해당합니다.

- 테두리는 클립 프레임의 `"border"` 에 프레임 비율로 적습니다. 클립이 없는 텍스처 스프라이트는 `_sliceBorder` 를 씁니다. 유니티와 Godot는 픽셀로 적습니다.
- 크기는 `_size`(로컬 월드 크기)입니다. Simple 방식은 트랜스폼 스케일로 크기를 정합니다.
- 타일 방식은 자연 크기의 타일을 되풀이하고 마지막 타일을 자릅니다. 유니티의 Continuous 방식입니다.

아직 없는 것은 테두리를 픽셀로 적는 방법(런타임이 아틀라스 픽셀 크기를 모른다), 유니티의 Adaptive 타일, 가운데를 비우는 Fill Center 끄기입니다.

### 픽셀 퍼펙트 카메라

픽셀 아트가 흐려지거나 픽셀 크기가 들쭉날쭉하지 않게 카메라를 맞춥니다. 유니티의 Pixel Perfect Camera, Godot의 `viewport` 늘이기와 정수 배율에 해당합니다.

- **정수 배율.** `_pixelsPerUnit` 과 `_referenceResolution` 으로 두 축이 모두 들어가는 가장 큰 정수 배율을 구합니다. 직교 높이는 "뷰포트 높이 / (배율 × PPU)" 입니다.
- **카메라 스냅.** 그리는 시점만(`CameraComponent::setViewOffset`) 화면 픽셀 격자에 맞추고 트랜스폼은 그대로 둡니다.
- **스프라이트 스냅.** `_bPixelSnapping` 을 켜면 인스턴스의 `_pixelSnap` 이 1 / PPU가 되고, `sprite2d.hlsl` 이 원점을 에셋 픽셀 격자에 맞춥니다.
- **점 필터.** `sprite2dpixel.material`(`pointFilter`)은 UV를 텍셀 중심에 맞춥니다. OpenGL은 결합 샘플러라 샘플러를 바꾸지 않고 UV로 처리합니다.
- **자르기.** `_bCropX`, `_bCropY` 를 켜면 `Overlay` 레이어 맨 위에 검은 띠를 그립니다.

저해상도로 그린 뒤 키우는 방식(유니티 Upscale Render Texture)은 없습니다. 렌더러에 저해상도 타깃과 업스케일 패스가 필요합니다.

엔진 루프가 틱 전에 카메라 레지스트리에 뷰포트 크기를 적습니다(`CameraRegistry::setViewportSize`). 스냅 단위가 바뀌면 씬의 모든 메시에 틱 뒤로 미뤄 알리고, 나중에 등록되는 메시는 레지스트리에서 읽습니다.

### 타일맵

타일맵은 맵 XML의 `<tileLayer>` 와 타일셋 파일(`.tileset.xml`, `TileMap/TileSetAsset`)로 이루어집니다.

| 기능 | 유니티 | Godot | 이 엔진 |
|---|---|---|---|
| 타일셋 | Tile Palette, Tile asset | TileSet | `.tileset.xml` |
| 자동 타일 | Rule Tile | terrain set | `<RuleTile>` 과 `<Rule>` |
| 애니메이션 타일 | Animated Tile | animation frames | `frames` 와 `fps` 속성 |
| 충돌 | TilemapCollider2D | physics layer | 병합 사각형 바디 |
| 내비게이션 | 외부 패키지 | navigation layer | `computeNavCosts` |

- **자동 타일.** `<Rule pattern=".x. xoo .o." cell="n"/>` 은 이웃 셀 여덟 개를 봅니다. `o` 는 같은 타일, `x` 는 다른 타일, `.` 은 상관없음입니다. 첫 규칙이 이기고, `outside="same"` 이면 맵 밖을 같은 타일로 봅니다.
- **애니메이션 타일.** `frames="12 13 14 15" fps="4"` 처럼 적습니다.
- **칠하기.** 에디터 TileMapPanel의 Tile 레이어에서 셀에 브러시 이름을 칠하면 규칙이 고른 아틀라스 셀 번호가 보입니다. 런타임에는 `setTileBrush` 를 씁니다.
- **저장.** 맵 XML에는 팔레트(브러시 이름)와 셀마다의 번호만 저장합니다. 실제 모습은 저장하지 않고 읽을 때 규칙으로 고릅니다.
- **충돌.** 단단한 브러시는 줄 단위로 병합한 사각형 바디(`PhysicsWorld`)가 되고, 바깥 방향이 있는 외곽선이 2D 그림자 가림막이 됩니다.
- **이동 비용.** `computeNavCosts` 는 GameFramework `NavGrid` 와 같은 값(10 보통, 255 막힘, 브러시의 `navCost`)을 냅니다.

아직 없는 것은 다음과 같습니다.

- 청크. 한 레이어는 셀 65536개가 상한이라 넘으면 레이어를 나눕니다.
- 플레이 전 편집 중 미리보기. 배치는 `onBeginPlay` 에서 만듭니다.
- 에디터 셀에 번호 대신 아틀라스 이미지 표시, 회전과 뒤집기 규칙, 사각형이 아닌 충돌 모양

### 2D 빛과 그림자

| 기능 | 유니티 URP 2D | Godot | 이 엔진 |
|---|---|---|---|
| 빛 받는 스프라이트 | Sprite-Lit-Default | light_mask | `sprite2dlit.material` |
| 점, 스폿 빛 | Light 2D Point | PointLight2D | `PointLight2DComponent` |
| 전역 빛 | Global Light 2D | CanvasModulate | `GlobalLight2DComponent` |
| 노멀 맵 | Secondary Texture | normal_texture | `_normalMapName`, `normalAtlas` |
| 그림자 | Shadow Caster 2D | LightOccluder2D | `ShadowCaster2DComponent` |

- 빛 받는 스프라이트는 `sprite2dlit.hlsl` 과 `lighting2d.hlsli` 로 그립니다. 2D 빛이 하나도 없으면 빛 없이 그대로 그립니다.
- 점 빛은 안쪽과 바깥 반경, 각도, 감쇠 지수를 가집니다. 감쇠는 `((바깥 − d) / (바깥 − 안))^지수` 이고, 스폿의 원뿔은 로컬 +X 방향입니다. 노멀 맵 높이는 빛의 `_normalMapHeight` 입니다.
- 그림자 가림막은 상자나 같은 오브젝트의 타일맵 외곽선입니다. 빛을 등진 선분만 가리므로 자기 그림자는 생기지 않습니다.

빛과 가림막 선분은 3D와 같은 빛 목록(t12)에 종류를 달리해 실립니다(`SW_LIGHT_TYPE_POINT2D`, `GLOBAL2D`, `SHADOW2D`). 새 슬롯이나 새 패스가 없어 네 백엔드가 같은 버퍼를 같은 슬롯에 바인딩합니다.
3D 조명 식(`swShadeLights`)은 2D 원소를 건너뛰고, 3D 빛이 하나도 없으면 키 라이트로 폴백합니다.
픽셀 하나가 "원소 수 × 가림막 선분 수"만큼 반복하므로 선분은 수백 개까지가 적당합니다. 한 프레임 원소 상한은 1024입니다.

아직 없는 것은 Freeform, Sprite, Parametric 같은 빛 모양, 빛 블렌드 스타일, 빛 텍스처(쿠키), 부드러운 그림자입니다. 그림자는 단단한 그림자뿐입니다.

### 시차 레이어

| 기능 | Godot | 이 엔진 |
|---|---|---|
| 카메라에 대한 배율 | `motion_scale` | `_scrollFactor` |
| 끝없이 되풀이 | `motion_mirroring` | `_repeatSize` |
| 기준점 | `scroll_base_offset` | `_referencePoint` |

유니티에는 기본 컴포넌트가 없고 보통 스크립트로 만듭니다.

- `_scrollFactor` 는 축마다의 배율입니다. 1이면 월드에 고정되고, 0이면 카메라에 붙습니다.
- `_repeatSize` 는 축마다 되풀이 길이 M입니다. 카메라에 대한 상대 위치를 M마다 감습니다. 내용은 M마다 같아야 하고 화면 폭 + M을 덮어야 합니다. Tiled 스프라이트라면 폭을 3M으로 둡니다.
- `_referencePoint` 는 카메라가 그 위치에 있을 때 저작한 배치가 그대로 보이는 기준점입니다.

Godot처럼 레이어마다 자식을 복제해 그리지 않으므로, 되풀이는 내용이 맡습니다.

## 확장하는 법

### 새 2D 컴포넌트 만들기

1. 스프라이트처럼 그려지는 컴포넌트는 `SpriteComponent` 를 쓰거나, 인스턴스를 많이 그린다면 `SpriteInstanceBatch` 로 배치를 만듭니다.
   배치를 보관하는 컴포넌트는 `setOwnerComponent( this )` 를 부르고 활성 상태가 바뀌면 `markAllEntriesDirty` 를 부릅니다([Renderer 문서](../../../Renderer/README.md)의 "씬과 스냅샷").
2. 카메라 위치를 읽는 컴포넌트는 카메라를 움직이는 컴포넌트보다 뒤 틱 그룹에 둡니다.
3. 정렬은 `SpriteRenderUtil` 의 정렬 키를 씁니다. 키를 직접 만들지 않습니다.

## 함정과 주의

**카메라를 움직이는 컴포넌트는 시차 레이어보다 앞 틱 그룹에서 돌아야 합니다.** 시차 레이어는 PostUpdate에서 돕니다.
같은 그룹에 두면 오브젝트 사이의 틱 순서가 정해지지 않아 한 프레임 늦거나 데이터 경합이 생깁니다. 픽셀 퍼펙트 카메라(PostPhysics)는 카메라를 따라가게 하는 컴포넌트 뒤, 시차 레이어 앞입니다.

**정렬 레이어는 투명 큐에만 적용됩니다.** 스프라이트 머티리얼은 투명입니다. 알파 테스트로 그리는 불투명 스프라이트는 깊이 버퍼로 가려집니다.

**2D 게임은 `engine/pipeline/forward2dpipeline.xml` 로 그리세요**(`-gv_renderPipeline`). 기본 포워드 파이프라인의 톤 매핑(Reinhard)은 흰색을 0.5로 누르고, 그림자 맵 패스는 2D에 쓸모가 없습니다.

**스프라이트 클립의 `transformKeys` 는 클립 타임라인의 초 단위이고, 루트(primary) 스프라이트에는 적용하지 않습니다.** 루트에 걸면 경고가 납니다. 움직일 스프라이트는 루트 아래에 둡니다.

## 더 볼 곳

- [Graphics/2D](../../../Graphics/2D/README.md): 정렬 키, 투명 정렬 축, 9-슬라이스 메시
- [Renderer](../../../Renderer/README.md): 인스턴스 배치와 투명 정렬
- [Empty 게임 팩](../../../../../Resource/game/empty/README.md): 2D 데모 씬 설명
