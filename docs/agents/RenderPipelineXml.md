# RenderPipelineXml

> 질문: 렌더 패스를 코드 없이 데이터로 어떻게 바꾸는가?
> 독자: 파이프라인 · 렌더 패스 XML 을 쓰거나 패스 코드를 고치는 사람과 에이전트.
> 결론: 파이프라인 XML 에 적힌 값이 곧 실행되는 값이다. 실행이 받아 줄 수 없는 조합은 무시되지 않고 읽기 오류다. 표(`RenderPassTypeInfo`)는 XML 이 없는 내부 슬롯과 XML 이 비운 기본값만 정한다.

## 파일

| 파일 | 하는 일 |
|---|---|
| `<도메인>/pipeline/*.xml` (`RenderPipelineDesc`) | 첨부, 패스 그래프, 패스별 PSO 설정. 고르는 법은 `-gv_renderPipeline=` 또는 팩 설정 |
| `engine/renderpass/*.xml` (`RenderPassDesc`) | 여러 파이프라인이 함께 쓰는 첨부 묶음. `_listRenderPassRef` 로 가져오면 읽을 때 `_listAttachment` 앞에 합쳐진다 |

이름이 겹치는 첨부는 오류다 — 파이프라인이 덮어쓰지 않는다.

비슷한 파이프라인은 `_base="engine/pipeline/forwardpipeline.xml"` 로 부모를 가리키고 차이만 적는다. 패스 · 첨부 목록은 `_name` 으로 합치고(`_op="Remove"` · `_op="Replace"` · `_before` · `_after`), 쿠킹이 펼친다. 규칙은 [DataInheritance](DataInheritance.md).

## 패스 칸

| 칸 | 뜻 | 받아 주지 않는 조합(읽기 오류) |
|---|---|---|
| `_bEnableDepthTest` | 깊이 테스트 | 켰는데 `_depthAttachment` 가 없거나, 그 패스 종류의 실행이 깊이를 붙이지 않을 때 |
| `_depthAttachment` | 붙일 깊이 첨부 | 깊이 테스트를 끄고 적었을 때 |
| `_bEnableDepthWrite` | 깊이 쓰기 | 깊이 테스트 없이 켰을 때 |
| `_bEnableBlend` | 블렌드. 프리멀티플라이 여부는 셰이더 규약이라 표가 정한다 | 컴퓨트 패스. 반투명 패스에서 끄면 경고만 한다 |
| `_cullMode` | `None` · `Front` · `Back` | 그 밖의 글. 풀스크린 패스의 `None` 아닌 값 |
| `_shaderPath` · `_listPermutation` | 셰이더와 define. 비면 표의 기본 셰이더 | 컴퓨트 패스의 빈 `_shaderPath` |
| `_vertexEntryPoint` · `_pixelEntryPoint` | 진입점. 비면 `VSMain` · `PSMain` | |
| `_geometryEntryPoint` | 적으면 지오메트리 스테이지가 붙는다 | 머티리얼 셰이더 패스, 컴퓨트 패스 |
| `_hullEntryPoint` · `_domainEntryPoint` | 함께 적으면 테셀레이션이 붙고 제어점 셋의 패치로 그린다 | 한쪽만 적었을 때, 머티리얼 셰이더 패스, 컴퓨트 패스 |
| `_computeEntryPoint` · `_threadGroupSize` | 컴퓨트 패스의 진입점과 그룹 크기(셰이더의 `numthreads` 와 같게) | 컴퓨트 패스 밖의 진입점 |

메시 · 증폭 스테이지는 지원하지 않는다(적으면 모르는 칸이라 오류다).

## 컴퓨트 패스

`_type="Compute"` 패스는 셰이더 하나로 효과를 더한다. 선언: 컬러 출력 하나, 입력 최대 둘(깊이 형식 제외 — 깊이는 `SceneDepthCopy` 를 읽는다), 입력과 출력은 겹치지 않는다. 첨부는 RW 텍스처로 묶이고 값은 `compute_pass.hlsli` 의 루트 상수로 받는다. GL 은 묶은 포맷과 셰이더 선언이 같아야 하므로 include 전에 `SW_RW_SLOT0_GL_FORMAT` 을 적는다. 예: `game/empty/pipeline/computepasspipeline.xml`.

## 선택 스테이지를 쓰는 셰이더

- 소스는 패스 셰이더 한 파일이다. 머티리얼 변형은 머티리얼 셰이더의 정점 출력으로 만들어져 신호가 맞지 않아 머티리얼 셰이더 패스에는 쓸 수 없다.
- 헐 · 도메인의 제어점 위치는 `SV_POSITION` 이 아니라 일반 값으로 넘긴다. 내장 위치로 넘기면 GL 에서 오류 없이 아무것도 그리지 않는다. 예: `game/empty/shaders/optional_stages.hlsl`.
- 이 스테이지는 패스 상수 · 루트 상수를 받지 않는다고 보고 쓴다. 필요하면 정점 스테이지에서 값으로 넘긴다.

## 고칠 때

새 칸을 더하면 PSO(`FramePSOManager::makePassPSODesc`) · 쿠커(`ShaderCookRequestCollector`) · 검증(`RenderPipelineAsset::validate`)을 함께 고친다. 칸만 두고 실행이 읽지 않으면 XML 이 거짓말을 한다. 값이 그림을 바꾸는지는 끄고 켠 두 장을 견주는 시나리오로 확인한다(예: `game/empty/automation/deferredtransparent`).

쿠커 코드를 고칠 때 번호를 올릴 일이 없다 — 쿠킹 키는 소스 지문에서 산출한다(`Config/Dev/CookCodeInputs.json`). 새 쿠커는 이 표에 폴더를 적는다.
