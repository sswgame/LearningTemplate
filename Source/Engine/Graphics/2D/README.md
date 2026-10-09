# Graphics/2D — 2D 렌더 데이터

## 이것은 무엇이고 왜 있나

2D 게임의 스프라이트는 깊이 버퍼만으로 앞뒤를 정할 수 없습니다. 같은 평면에 놓인 스프라이트가 많고, 디자이너는 "배경 레이어 위에 캐릭터 레이어"처럼 그리는 순서를 직접 정하고 싶어 합니다.
이 폴더는 그 순서를 정하는 프로젝트 설정(정렬 레이어)과, 9-슬라이스나 타일 방식 스프라이트의 메시를 만드는 코드를 둡니다. 유니티의 Sorting Layer와 Sprite Renderer 그리기 방식에 해당합니다.

컴포넌트(`Object`)가 이 값을 채우고 렌더러(`Renderer`)가 읽으므로 그 둘보다 아래 티어인 `Graphics` 에 둡니다.
스프라이트 컴포넌트 쪽 설명과 유니티, Godot 대응은 [Object/Component/2D 문서](../../Object/Component/2D/README.md)에 있습니다.

## 머릿속 그림

**정렬 레이어.** `Resource/engine/data/render2d.xml` 에 레이어 이름을 위에서부터 적고, 위의 레이어가 먼저(뒤에) 그려집니다. `Default` 레이어는 반드시 있어야 합니다.
활성 게임 팩에 `data/render2d.xml` 이 있으면 그 파일이 엔진 파일을 통째로 대신합니다. `Render2DSettings` 가 이 파일을 읽습니다.

**정렬 키.** 스프라이트마다 레이어와 "레이어 안 순서"를 정수 키 하나로 합칩니다(`Render2DSettings::makeSortKey`). 투명 큐는 이 키로 먼저 정렬합니다.

**투명 정렬 축.** 키가 같은 물체끼리는 깊이로 앞뒤를 정합니다. 무엇을 깊이로 쓸지는 `TransparencySortMode` 가 정합니다(Auto, Distance, ViewAxis, CustomAxis).

## 작동 원리

### 정렬 키와 그리는 순서

투명 큐는 **정렬 키, 깊이, 후보 번호** 순서로 완전히 정렬해 그립니다(`GpuSceneBuilder::isDrawnBefore`).

- 키는 `(레이어 순번 << 16) | (레이어 안 순서 + 0x8000)` 입니다. 그래서 레이어가 순서보다 우선합니다. 유니티에서 Sorting Layer가 Order in Layer보다 우선하는 것과 같습니다.
- 키 0은 "Default 레이어, 순서 0"을 뜻하는 예약값입니다. `MeshComponent` 는 정렬 테이블을 읽지 않고 0을 가지며, 빌더가 테이블의 기본 키로 바꿉니다.
  그래서 3D 투명 물체와 Default 레이어의 스프라이트가 같은 줄에서 깊이로 섞입니다.
- 깊이는 `GpuSceneBuilder::setTransparentSortAxis` 의 축이 영벡터면 카메라까지 거리의 제곱이고, 아니면 그 축 위의 깊이입니다.
  `EngineLoop` 이 `computeTransparentSortAxis( 직교 여부, 카메라 전방 )` 로 축을 정합니다. Auto는 직교 카메라에서 시선 축을 씁니다.
- GPU 컬링이 투명 배치를 압축한 뒤 `instancesort.hlsl` 은 인스턴스 번호 오름차순으로 되돌립니다. 배치 안의 인스턴스가 CPU 정렬 순서로 놓이므로 번호가 곧 순서입니다.
  정렬 기준은 CPU 한 곳뿐입니다. GPU에서 깊이를 다시 측정하면 레이어와 시선 축을 모르므로 같은 깊이를 불안정하게 나눕니다.

### 9-슬라이스와 타일 메시

`SpriteMeshBuilder` 가 9-슬라이스(Sliced)와 타일(Tiled) 메시를 만듭니다. 정점은 `buildSlicedVertices` 가 만들고, 같은 값의 메시는 약한 참조 테이블(`acquireSlicedMesh`)로 나눠 씁니다.

- 테두리는 **프레임 비율**(0..1, 왼쪽, 아래, 오른쪽, 위)입니다. 스프라이트의 자연 크기가 1 × 1이라 그 비율이 곧 모서리의 월드 크기입니다.
- UV는 프레임 안의 0..1이고 셰이더가 인스턴스의 아틀라스 프레임으로 옮깁니다. 그래서 애니메이션 프레임이 바뀌어도 테두리가 같으면 메시는 그대로입니다.
- 앞면(-Z)과 그것을 X로 비춘 뒷면(+Z)이 같은 UV를 써서, 어느 쪽에서 봐도 뒤집혀 보이지 않습니다. `MeshUtil::createSpriteQuad` 와 같은 규칙입니다.
- 크기가 테두리 합보다 작으면 테두리를 비율대로 줄입니다. 타일 방식은 마지막 타일을 잘라 UV도 그만큼만 쓰고, 한 축에 64개(`kMaxTileCountPerAxis`)까지입니다.
- 크기와 테두리가 같은 패널은 메시 하나라 한 배치로 그려집니다. 크기가 다르면 메시가 달라 배치가 나뉘지만, 정점 풀과 멀티 드로우 덕분에 드로우 호출은 하나로 묶입니다.

## 확장하는 법

1. 정렬 레이어를 더하려면 게임 팩에 `data/render2d.xml` 을 두고, 엔진 파일의 레이어 목록을 복사한 뒤 원하는 위치에 `<Layer name="..."/>` 을 넣습니다. 팩 파일이 엔진 파일을 통째로 대신하므로 `Default` 를 빼면 안 됩니다.
2. 스프라이트의 정렬 레이어와 레이어 안 순서는 `SpriteComponent` 에서 지정합니다([Object/Component/2D 문서](../../Object/Component/2D/README.md)).
3. 탑다운 게임처럼 위쪽 물체를 뒤에 그리고 싶으면 `<TransparencySort mode="CustomAxis" axis="0 1 0" />` 을 씁니다.

## 함정과 주의

**정렬 레이어는 투명 큐에만 적용됩니다.** 스프라이트 머티리얼(`sprite2d.material`)은 투명입니다. 불투명(알파 테스트) 스프라이트는 깊이 버퍼로 가려집니다.

**2D는 직교 카메라와 Auto(또는 ViewAxis, CustomAxis)로 두세요.** 같은 Z의 스프라이트를 거리로 정렬하면 카메라의 XY 위치에 따라 앞뒤가 바뀝니다.

**모르는 레이어 이름은 오류를 남기고 `Default` 로 그립니다.** 레이어 이름을 바꿨다면 그 이름을 쓰는 씬과 프리팹 데이터도 함께 고칩니다.

## 더 볼 곳

- [Object/Component/2D](../../Object/Component/2D/README.md): 스프라이트, 타일맵, 2D 빛 컴포넌트
- [Renderer](../../Renderer/README.md): 투명 정렬과 GPU 정렬, 추가 뷰의 투명 정렬
