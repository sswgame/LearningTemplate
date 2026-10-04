# Object/Component/2D — 2D 컴포넌트

스프라이트 · 2D 충돌 · 2D 렌더 기능의 컴포넌트입니다. 렌더 데이터(정렬 레이어 표 등)는 [Graphics/2D](../../../Graphics/2D/README.md) 에 있습니다.

| 파일 | 역할 |
|------|------|
| `SpriteComponent` | 텍스처를 입힌 사각형(아틀라스 프레임 · 색 · 정렬 레이어 · 레이어 안 순서) |
| `SpriteAnimatorComponent` | 클립(`.sprite.json`)의 프레임을 넘김 |
| `SpriteRenderUtil` | 스프라이트 머티리얼 경로 · (머티리얼, 텍스처) 인스턴스 공유 · 정렬 키 풀기 |
| `BoxCollider2DComponent` · `TileColliderComponent` | 2D 충돌 |

## 정렬 — 유니티 · Godot 와 견줘

| 기능 | 유니티 | Godot | 여기 |
|------|--------|-------|------|
| 정렬 레이어 표 | Tags & Layers › Sorting Layers | CanvasLayer 순서 | `render2d.xml` `<SortingLayers>` (게임 팩이 통째로 대신할 수 있음) |
| 레이어 안 순서 | Order in Layer (−32768..32767) | `z_index` | `SpriteComponent::_orderInLayer` (−32767..32767) |
| 같은 순서의 앞뒤 | Transparency Sort Mode (Default · Perspective · Orthographic · Custom Axis) | 트리 순서 · `y_sort_enabled` | `TransparencySortMode` Auto · Distance · ViewAxis · CustomAxis, 그다음 등록 순서 |
| 월드 공간 UI | Canvas 의 Sorting Layer | CanvasLayer | `SpriteInstanceBatch::setSorting` — HP 바 · 데미지 숫자는 `WorldUI` |

정렬 레이어는 투명 큐에만 듣습니다(스프라이트 머티리얼은 투명). 모르는 레이어 이름은 오류를 남기고 `Default` 로 그립니다.
