# Graphics/2D — 2D 렌더 데이터

2D 렌더러(스프라이트 · 타일맵 · 2D 조명)가 프로젝트 단위로 읽는 표와 메시 생성기입니다. 컴포넌트(Object, 티어 6)가 채우고
렌더러(`Graphics/Renderer`, 티어 8)가 읽으므로 그 둘 아래(티어 5)에 둡니다. 컴포넌트 쪽 설명과 유니티 · Godot 대응표는
[Object/Component/2D/README.md](../../Object/Component/2D/README.md) 에 있습니다.

| 파일 | 역할 |
|------|------|
| `Render2DSettings` | `render2d.xml` — 정렬 레이어 표 · 정렬 키(`makeSortKey`) · 투명 정렬 축(`TransparencySortMode`) |
| `SpriteMeshBuilder` | 9-슬라이스(Sliced) · 타일(Tiled) 메시 — 정점 짓기(`buildSlicedVertices`)와 같은 값끼리 나눠 쓰는 약한 참조 표(`acquireSlicedMesh`) |

## 정렬 키

투명 큐는 **정렬 키 → 깊이 → 후보 번호**의 전순서로 그립니다(`GpuSceneBuilder::isDrawnBefore`).

- 키 = `(레이어 순번 << 16) | (레이어 안 순서 + 0x8000)`. 레이어가 순서를 이깁니다(유니티 Sorting Layer > Order in Layer).
- 키 0 은 "Default 레이어 · 순서 0" 자리표입니다. `MeshComponent` 는 표를 읽지 않고 0 을 들고, 빌더가 표의 기본 키로 바꿉니다 —
  그래서 3D 투명 물체와 Default 레이어의 스프라이트가 한 줄에서 깊이로 섞입니다.
- 깊이는 `GpuSceneBuilder::setTransparentSortAxis` 의 축이 영벡터면 카메라까지의 거리², 아니면 그 축 위의 깊이입니다.
  `EngineLoop` 가 `computeTransparentSortAxis( 직교?, 카메라 전방 )` 로 정합니다 — Auto 는 직교에서 시선 축입니다.
- GPU 컬링이 투명 배치를 압축한 뒤 `instancesort.hlsl` 은 **인스턴스 번호 오름차순**으로 되돌립니다. 배치 안의 인스턴스는 CPU 정렬 순서로
  놓이므로 번호가 곧 순서입니다. 정렬 기준은 CPU 한 곳뿐입니다 — GPU 가 깊이를 다시 재면 레이어 · 시선 축을 모르고 같은 깊이를 불안정하게 가릅니다.

## 9-슬라이스 · 타일 메시

- 테두리는 **프레임 비율**(0..1, 왼 · 아래 · 오른 · 위)이고 스프라이트의 자연 크기가 1 × 1 이라 그 비율이 곧 모서리의 월드 크기입니다.
- UV 는 프레임 안의 0..1 이라 셰이더가 인스턴스의 아틀라스 프레임으로 옮깁니다 — 애니메이션 프레임이 바뀌어도 메시는 그대로입니다(테두리가 같으면).
- 앞면(-Z)과 그것을 X 로 비춘 뒷면(+Z)이 같은 UV 를 써서 어느 쪽에서 봐도 뒤집히지 않습니다(`MeshUtil::createSpriteQuad` 와 같은 규약).
- 크기가 테두리 합보다 작으면 테두리를 비율대로 줄입니다. 타일은 마지막 칸을 잘라 UV 도 그만큼만 쓰고, 한 축 64 칸이 상한입니다.
- 크기 · 테두리가 같은 패널은 메시 하나라 한 배치입니다. 크기가 다르면 메시가 달라 배치가 갈리지만 정점 풀 · 멀티 드로우로 호출은 하나로 묶입니다.

## 주의

- 정렬 레이어는 **투명 큐에만** 듣습니다. 스프라이트 머티리얼(`sprite2d.material`)은 투명입니다. 불투명(알파 테스트) 스프라이트는 깊이 버퍼로 가립니다.
- 같은 Z 의 스프라이트를 거리로 정렬하면 카메라의 XY 위치에 따라 앞뒤가 바뀝니다. 2D 는 직교 카메라 + Auto(또는 ViewAxis · CustomAxis)로 둡니다.
