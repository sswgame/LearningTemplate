# MaterialGraph

> 질문: 머티리얼 그래프는 어떻게 동작하고, 새 노드는 어떻게 더하는가?
> 독자: 머티리얼 그래프 노드 · 컴파일러 · 생성물을 다루는 사람과 에이전트.
> 결론: 새 노드는 `MaterialGraphNodes.cpp` 에 `SW_MATERIAL_GRAPH_NODE` 한 줄이다. 그래프는 HLSL 과 `.gen.material` 을 소스 트리에 생성하고, 그 뒤는 손으로 쓴 머티리얼과 같은 길이다. 런타임과 배포에는 그래프 코드가 없다.

구현: `Source/Engine/Graphics/Material/Dev/`(개발 전용), 편집기 `Source/Editor/Panels/MaterialGraph/`. 상용 엔진 비교는 기억에 의존한 설명이다.

## 길

```
rim_glow.matgraph.xml                         저작 파일(팩에 들지 않는다)
  └ MaterialGraphCompileResult위상 정렬(DependencyGraph 깊이 우선) → 형식 검사 → HLSL 조각 펼치기
      ├ shaders/matgraph/<줄기>_<해시>.hlsl    SW_MATERIAL 칸 + evaluateMaterialGraph
      └ materials/.../rim_glow.gen.material    파라미터 칸 · 순열
          └ rim_glow_red.material              _base 로 값만 바꾼다(언리얼 Material Instance)
```

- **언제 생성하나:** 셰이더 쿠킹의 시작(`ShaderCookDriver::cookAllShaders`)과 편집기의 핫 리로드 임포터(그래프 파일이 바뀔 때). 바이트가 같으면 쓰지 않는다. 생성물은 커밋한다 — 쿠킹 없는 개발 실행도 같은 파일을 읽는다.
- **쿠킹 입력:** 생성된 `.hlsl` 은 셰이더 스탬프가 내용 해시로 추적하고, `.gen.material` 은 요청 수집과 팩이 그대로 읽는다. 컴파일 오류는 쿠킹 실패다.
- **파일 이름의 해시:** 내용이 바뀌면 경로가 바뀐다. PSO · 리플렉션 캐시의 열쇠가 경로라 무효화 코드 없이 핫 리로드된다.
- **낡음 확인:** 단위 테스트 `SampleGeneratedAssetsAreCurrent` 가 견본을 대조한다. 그래프를 손으로 고쳤으면 `App --cook-shaders` 나 편집기 저장이 다시 쓴다. 생성물은 손으로 고치지 않는다.

## 파일에 적는 법

```xml
<MaterialGraphDef formatVersion="0">
	<_listNode>
		<MaterialGraphNodeDef _name="Tint" _kind="ColorParameter" _position="0,0">
			<_listValue><MaterialGraphValue _name="Parameter" _value="_tint" /></_listValue>
		</MaterialGraphNodeDef>
		<MaterialGraphNodeDef _name="Out" _kind="Output" _position="280,0">
			<_listLink><MaterialGraphLink _pin="BaseColor" _fromNode="Tint" _fromPin="RGB" /></_listLink>
		</MaterialGraphNodeDef>
	</_listNode>
</MaterialGraphDef>
```

- 경로에 `/materials/` 칸이 있어야 한다. 생성물 자리가 거기서 정해진다.
- 연결의 기준은 받는 노드의 `_listLink` 다. 입력 핀 하나에 링크 하나다.
- `_listValue` 는 설정이거나, 잇지 않은 입력 핀의 상수다.
- 값 변형은 `.gen.material` 의 자식 `.material` 이다. 렌더 상태(`alphaMode` · `cullMode`)도 그 자식의 칸이다. 셰이더가 달라지는 변형만 그래프 `_base` 를 쓴다(열쇠는 `_name` · `_pin`).

## 새 노드 더하기

`MaterialGraphNodes.cpp` 에 한 줄을 더한다. 컴파일러 · 메뉴 · 검사는 고치지 않는다.

```cpp
//                    이름     메뉴 묶음  입력                           출력                            설정
SW_MATERIAL_GRAPH_NODE( Lerp, "Math", "A:any!|B:any!|T:float=0.5", "Out:any=lerp( $A, $B, $T )", "" );
SW_MATERIAL_GRAPH_NODE( Constant, "Constant", "", "Out:float=%Value", "Value:float=0" );
// 머티리얼 칸이 되는 노드: 마지막 인자가 `HLSL 형식:프로퍼티 형식`, 설정 `Parameter` 가 칸 이름이고 `%Parameter` 가 그 칸을 읽는다
SW_MATERIAL_GRAPH_PARAMETER_NODE( ScalarParameter, "Parameter", "", "Out:float=%Parameter", "Parameter:string=_value|Default:float=0|Min:float=0|Max:float=1", "float:Range" );
```

| 자리 | 문법 |
|---|---|
| 입력 | `이름:형식`, 필수면 `!`, 기본값은 `=수치` 또는 `=HLSL 식`(`UV:float2=context._uv`). `\|` 로 나눈다 |
| 출력 | `이름:형식=HLSL 식`. `$이름` 은 입력 또는 앞선 출력, `%이름` 은 설정이다 |
| 설정 | `이름:형식=기본값`. 잇지 못하는 값이다 |
| 형식 | `float` · `float2` · `float3` · `float4` · `any` · `string`(설정만) |
| `any` | 이어진 입력의 벡터 크기를 따른다. `float` 만 넓어지고 다른 크기가 섞이면 오류다 |
| 머티리얼 칸 형식 | `float` · `float4` · `uint`(텍스처 번호). 원소 채움이 이 셋을 전제한다 |

식에서 쓸 수 있는 문맥은 `PixelContext` 칸(`context._uv` · `context._normal` · `context._worldPosition` · `context._color`)과 `g_SwMaterialTime` · `View::computeToView` · `MaterialTexture::sampleIndex` · `applyNormalMap` 이다. 식 안에 `|` 를 쓰지 않는다(`abs`, 비트 연산은 함수로 감싼 `.hlsli` 를 include 뼈대에 더한다).

**등록은 엔진 안에만 둔다.** 팩 쿠킹은 게임 모듈을 올리기 전에 돌아 키트 · 게임의 등록을 보지 못한다. 게임 전용 재사용은 하위 그래프로 한다. 단위 테스트 `EveryRegisteredKindParses` · `EverySnippetExpands` 가 새 줄의 오타를 잡는다.

## 하위 그래프

`GraphInput`(설정 `Name` · `Type`)과 `GraphOutput`(설정 `Name`, 입력 `Value`)을 가진 그래프가 하위 그래프다. `Subgraph` 노드의 `_assetPath` 로 가리키면 핀이 그 파일에서 오고, 컴파일이 펼쳐 넣는다(언리얼 Material Function). 안의 파라미터는 쓰는 머티리얼의 칸이 된다. 스스로를 부르면 오류다.

## 층

기반(`Output`) 위에 층(먼지 · 젖음 · 피 · 칠)을 마스크와 함께 쌓는다. 언리얼 Material Layers · Layer Blend, 유니티 HDRP Layered Lit 의 자리다(비교는 기억에 의존).

```
layer_dirt.matgraph.xml        층 한 벌 — GraphOutput 이름이 표면 칸이다. 머티리얼 여럿이 함께 쓴다
layered_stone.matgraph.xml     Output + Layer 노드 목록(노드 차례가 층 차례)
  └ layered_stone.gen.material     칸 _DirtWeight · _Dirt_color · _Dirt_roughness (· _layerMask)
      └ layered_stone_clean.material   _DirtWeight=0 — 층을 끈 변형(같은 셰이더)
```

- **층 파일:** 하위 그래프다. `GraphOutput` 의 `Name` 이 `BaseColor` · `Metallic` · `Roughness` · `Normal` · `Emissive` 가운데 하나이고, 내지 않은 칸은 아래 값 그대로다. `GraphInput` 은 같은 이름으로 아래 칸을 읽는다(젖음이 아래 색을 어둡게 한다). 알파는 층이 바꾸지 않는다 — 렌더 상태라 패스가 달라진다.
- **Layer 노드:** `_assetPath` 가 층 파일이고 노드 이름이 칸 머리다(글자 · 숫자 · `_`). 머티리얼 그래프에만 둔다. 생성 길은 하위 그래프와 같은 펼침이라 런타임은 생성물만 읽는다.
- **마스크:** 핀 `Mask` 를 이으면 그 값(꼭짓점 색 · 법선 · 텍스처), 아니면 설정 `MaskChannel`(`None` · `R` · `G` · `B` · `A`)이 칸 `_layerMask`(선형 텍스처, 한 번 읽어 층들이 나눠 쓴다)의 채널을 고른다. 섞기는 `lerp( 아래, 층, saturate( 마스크 × _<층>Weight ) )` 이고 법선은 다시 정규화한다.
- **상한:** `Config/Dev/MaterialLayers.json` 의 `_maxLayerCount`(머티리얼 하나의 층 수) · `_maxMaskChannelCount`(`_layerMask` 에서 R 부터 쓸 수 있는 채널 수). 층이 있는 그래프만 읽고, 없거나 틀리면 컴파일 오류다.
- **변형:** 값 · 켜고 끄기는 자식 `.material` 이다(셰이더 · PSO 하나). 층 파일을 바꾸거나 층을 더하는 변형은 셰이더가 달라지므로 그래프 `_base` 로 Layer 노드를 열쇠(`_name`)로 고친다.

외형 변형이 거는 칸(연결은 외형 쪽이 한다):

| 칸 | 형식 | 뜻 |
|---|---|---|
| `_<층>Weight` | Range 0~1 | 층의 세기. 마스크에 곱한다. 0 이면 층이 꺼진다. 기본은 노드 설정 `Weight` |
| `_<층>_<칸>` | 층 파일의 파라미터 형식 | 그 층의 값(`_Dirt_color`). 같은 층 파일을 두 노드가 쓰면 칸이 갈린다 |
| `_layerMask` | Texture2D(선형) | 채널로 고른 층들의 마스크. 쓰는 층이 있을 때만 생긴다 |

## 출력과 조명

`Output` 의 핀은 `material_graph.hlsli` 의 `MaterialGraphOutput` 칸과 이름이 같다. 조명은 `pbr_lit` 과 같은 뼈대의 기본 PBR 이다. 자체 발광을 잇거나 값을 적으면 `SW_MATERIAL_EMISSIVE` 훅을 쓰고, G버퍼에 칸이 없어 생성된 머티리얼이 `MATERIAL_FORWARD_ONLY` 를 함께 적는다(툰과 같은 길). 빛을 받는 식은 `Output` 의 설정 `ShadingModel` 이 고른다 — `DefaultLit`(기본 PBR) · `Hair`(`hair_shading.hlsli` Marschner 근사, 언리얼 Shading Model 의 Hair 자리). `Hair` 는 생성물에 `SW_MATERIAL_GRAPH_HAIR` 를 적고 칸 `_hairShift` · `_hairScatter` · `_hairStrandAxis`(0 탄젠트 · 1 바이탄젠트가 가닥)를 덧붙이며 G버퍼 칸이 없어 `MATERIAL_FORWARD_ONLY` 다. 그 밖의 이름은 `BadValue` 다. 새 모델은 이 설정의 값 하나와 `material_graph.hlsli` 의 조명 훅 선택 · `material_graph_main.hlsli` 의 몸통을 잇는다.

## 오류

컴파일 문제는 값(`MaterialGraphIssue`)으로 돌아오고 늘 노드 이름을 적는다. 편집기는 노드에 붙이고, 쿠킹은 `MaterialGraph.CompileFailed` 로 남기고 실패한다.

| id | 뜻 |
|---|---|
| `Cycle` | 순환(길을 이름으로 적는다) |
| `MissingPin` | 필수 입력이 끊겼다 |
| `TypeMismatch` | 벡터 크기가 맞지 않다 |
| `NoOutput` · `TwoOutputs` | 출력 노드가 없거나 둘이다 |
| `UnknownKind` · `UnknownPin` · `UnknownValue` · `MissingNode` | 없는 종류 · 핀 · 값 · 노드 |
| `BadValue` · `BadName` · `BadParameter` | 읽지 못한 값 · 이름 |
| `ParameterConflict` | 같은 칸 이름에 다른 형식 |
| `MissingSubgraph` · `SubgraphUnreadable` · `NotSubgraph` · `SubgraphCycle` | 하위 그래프 문제(층 파일도 같다) |
| `LayerOutsideMaterial` · `TooManyLayers` · `LayerLimitsUnreadable` · `BadLayerPin` | 층 문제: 하위 그래프 · 층 안의 층, 상한 초과, 상한 파일, 표면 칸이 아닌 층 핀 |

## 편집기

Window > Material Graph(`MaterialGraphPanel`). 오른쪽 클릭 메뉴는 등록표에서 만든다. 고치면 다시 컴파일하고, 통과한 내용을 작업 도메인(`scratch`)에 생성해 미리보기 구(`MaterialModel`)에 건다 — 저장 전 편집이 콘텐츠를 건드리지 않는다. 저장은 컴파일이 통과할 때만 그래프 파일을 쓰고, 임포터가 생성물을 쓰면 감시가 머티리얼 캐시와 자식 머티리얼까지 다시 읽는다.
