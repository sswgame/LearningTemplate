# Object/Component/2D — 2D 컴포넌트

스프라이트 · 2D 충돌 · 2D 렌더 기능의 컴포넌트입니다. 렌더 데이터(정렬 레이어 표 등)는 [Graphics/2D](../../../Graphics/2D/README.md) 에 있습니다.

| 파일 | 역할 |
|------|------|
| `SpriteComponent` | 텍스처를 입힌 사각형(아틀라스 프레임 · 색 · 정렬 레이어 · 레이어 안 순서 · 그리기 방식 Simple/Sliced/Tiled) |
| `SpriteAnimatorComponent` | 클립(`.sprite.json`)의 프레임을 넘김 |
| `ParallaxLayerComponent` | 시차 레이어 — 게임 카메라에 대한 배율로 오브젝트(자식 포함)를 옮기고 되풀이 길이로 감는다 |
| `SpriteRenderUtil` | 스프라이트 머티리얼 경로 · (머티리얼, 텍스처) 인스턴스 공유 · 정렬 키 풀기 |
| `BoxCollider2DComponent` · `TileColliderComponent` | 2D 충돌 |

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

## 시차 레이어

| 기능 | 유니티 | Godot | 여기 |
|------|--------|-------|------|
| 카메라에 대한 배율 | 기본 부품 없음(스크립트) | `ParallaxLayer.motion_scale` | `ParallaxLayerComponent::_scrollFactor`(축마다, 1 = 월드 · 0 = 카메라에 붙음) |
| 끝없이 되풀이 | — | `motion_mirroring` | `_repeatSize`(축마다) — 카메라에 대한 상대 자리를 감는다. 내용은 M 마다 같고 화면 폭 + M 을 덮어야 한다(Tiled 스프라이트 폭 3M) |
| 기준점 | — | `ParallaxBackground.scroll_base_offset` | `_referencePoint` — 카메라가 여기 있을 때 저작한 자리 |

틱은 마지막 그룹(PostUpdate)입니다. 카메라를 움직이는 컴포넌트는 그 앞 그룹에서 돌아야 같은 프레임의 카메라를 봅니다(같은 그룹이면 한 프레임 늦거나
레이스입니다 — 틱 순서는 오브젝트 사이에서 정해지지 않습니다). Godot 처럼 레이어마다 자식을 복제해 그리지 않으므로 되풀이는 내용 쪽이 맡습니다.

정렬 레이어는 투명 큐에만 듣습니다(스프라이트 머티리얼은 투명). 모르는 레이어 이름은 오류를 남기고 `Default` 로 그립니다.
