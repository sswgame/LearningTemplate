# MaterialGraph

> 결론: 새 노드는 `MaterialGraphNodes.cpp` 에 `SW_MATERIAL_GRAPH_NODE` 한 줄. 그래프는 `.hlsl` 과 `.gen.material` 을 생성하고 그 뒤는 손으로 쓴 머티리얼과 같다. 그래프 코드는 `Source/DevTools/Material/`(배포에 없음), 편집기는 `Source/Editor/Panels/MaterialGraph/`.

## 생성물

- 생성물(`shaders/matgraph/<줄기>_<해시>.hlsl` · `materials/…/<이름>.gen.material`)은 **커밋하고 손으로 고치지 않는다.** 그래프를 고쳤으면 `App --cook-shaders` 나 편집기 저장으로 다시 쓴다(테스트 `SampleGeneratedAssetsAreCurrent` 가 낡음을 잡는다).
- 엔진은 컴파일러를 모른다 — 셰이더 쿠커는 `IShaderSourceGenerator` 창구만 부른다. 엔진 · 런타임에서 그래프 코드를 include 하지 않는다.

## 파일에 적는 법

- 그래프 경로에 `/materials/` 칸이 있어야 한다(생성물 자리가 거기서 정해진다).
- 연결은 받는 노드의 `_listLink` 에 적는다. 입력 핀 하나에 링크 하나. `_listValue` 는 설정이거나 잇지 않은 핀의 상수.
- 값 · 렌더 상태(`alphaMode` · `cullMode`) 변형은 `.gen.material` 의 자식 `.material`(`_base`). 셰이더가 달라지는 변형만 그래프 `_base`(열쇠 `_name` · `_pin`).

## 새 노드 더하기

`MaterialGraphNodes.cpp` 에 한 줄만 더한다(컴파일러 · 메뉴 · 검사는 고치지 않는다).

```cpp
//                    이름     메뉴 묶음  입력                           출력                            설정
SW_MATERIAL_GRAPH_NODE( Lerp, "Math", "A:any!|B:any!|T:float=0.5", "Out:any=lerp( $A, $B, $T )", "" );
// 머티리얼 칸이 되는 노드: 마지막 인자 `HLSL 형식:프로퍼티 형식`, 설정 `Parameter` 가 칸 이름
SW_MATERIAL_GRAPH_PARAMETER_NODE( ScalarParameter, "Parameter", "", "Out:float=%Parameter", "Parameter:string=_value|Default:float=0|Min:float=0|Max:float=1", "float:Range" );
```

| 자리 | 문법 |
|---|---|
| 입력 | `이름:형식`, 필수 `!`, 기본값 `=수치` 또는 `=HLSL 식`. `\|` 로 나눈다 |
| 출력 | `이름:형식=HLSL 식`. `$이름` 입력 · 앞선 출력, `%이름` 설정 |
| 설정 | `이름:형식=기본값`(잇지 못하는 값) |
| 형식 | `float` · `float2` · `float3` · `float4` · `any`(이어진 입력 크기, `float` 만 넓어진다) · `string`(설정만) |
| 머티리얼 칸 형식 | `float` · `float4` · `uint`(텍스처 번호)만 |

- 식에는 `PixelContext` 칸(`context._uv` 등)과 `SurfaceFunction::*`(`material_functions.hlsli`) 같은 공용 함수만 쓴다. 식 안에 `|` 를 쓰지 않는다 — 비트 연산은 함수로 감싼 `.hlsli` 를 더한다.
- **등록은 엔진 안에만.** 팩 쿠킹은 게임 모듈을 올리기 전에 돌아 키트 · 게임 등록을 보지 못한다. 게임 전용 재사용은 하위 그래프로(테스트 `EveryRegisteredKindParses` · `EverySnippetExpands`).

## 하위 그래프 · 층

- 하위 그래프는 `GraphInput` · `GraphOutput` 을 가진 그래프, `Subgraph` 노드의 `_assetPath` 로 가리킨다. 스스로를 부르면 오류.
- 층 파일의 `GraphOutput` `Name` 은 `BaseColor` · `Metallic` · `Roughness` · `Normal` · `Emissive` 가운데 하나. **층은 알파를 바꾸지 않는다** — 렌더 상태라 패스가 달라진다.
- `Layer` 노드는 머티리얼 그래프에만, 노드 이름은 칸 머리(글자 · 숫자 · `_`). 상한은 `Config/Dev/MaterialLayers.json`(`_maxLayerCount` · `_maxMaskChannelCount`).
- 층을 끄는 변형은 자식 `.material` 의 `_<층>Weight=0`(같은 셰이더). 층 파일을 바꾸거나 더하는 변형만 그래프 `_base`.

## 후처리 영역

- 출력 노드 `PostProcessOutput` 이면 후처리 머티리얼. 화면 입력 노드(`SceneColor` · `SceneDepth` · `GBuffer*`)는 후처리 그래프에만, `Layer` 는 받지 않는다(`DomainMismatch`).

## 오류

컴파일 문제는 `MaterialGraphIssue` 값(노드 이름 포함)으로 돌아오고 쿠킹은 `MaterialGraph.CompileFailed` 로 실패한다 — 컴파일 강제. 편집기 저장은 컴파일이 통과할 때만 쓴다.
