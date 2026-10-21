# 상호작용 강조 패스와 몽타주 실행 확인

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: D4-12 · 진행: 2/4

상호작용 · 기믹(`GameFramework/Base/Gameplay/Interaction` · `Gimmick` · `Spline` README)의 남은 두 조각입니다. 고른 대상이 화면에서 외곽선으로 구별되고(감각 모드는 벽 너머로도), 상호작용 단계의 몽타주가 실제로 애니메이터를 부르는지 확인합니다.

## 1. 결정

- 강조는 **인스턴스 내용**이다 — `GPUInstance::_highlight`(색 RGB8 + 방식, `GPUInstanceHighlight`)에 싣고 배치 키에 넣지 않는다. 켜고 꺼도 배치를 다시 나누지 않고 그 인스턴스만 다시 올라간다(스프라이트 색 · 프레임과 같은 자리).
- 패스는 머티리얼과 무관한 엔진 셰이더(`highlightoutline.hlsl`) 하나로 모든 불투명 배치를 그리되, 그 패스의 방식이 아닌 인스턴스는 정점에서 버린다(CPU 로 배치를 거르지 않는다). Outline 은 깊이 테스트, Sense 는 테스트 없이 그린다.
- 두께는 파이프라인 XML 의 패스 퍼뮤테이션 `SW_HIGHLIGHT_WIDTH` 가 정한다(데이터가 정본 — 없으면 셰이더 컴파일 오류). 색은 상호작용 정의(`InteractionDef::_highlightColor`, XML `highlightColor`)다.
- 요청 플래그는 고른 쪽(`InteractorComponent`)이 다른 오브젝트의 틱에서 켜므로, 메시는 대상 자신(`InteractableComponent`)이 자기 틱에서 옮긴다(한 프레임 늦다).

## 2. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| H1 | 인스턴스 강조 칸 — `MeshComponent::setHighlight` → `GPUSceneBuilder` → `GPUInstance::_highlight`, 상호작용 정의의 색, `InteractableComponent::applyHighlightToMeshes` | 코드 · 테스트(`InteractionTest.HighlightRequestReachesTheOwnersMeshes`), 빌드 미확인 |
| H2 | 강조 패스 둘(`RenderPassType::HighlightOutline` · `HighlightSense`) · 셰이더(`highlightoutline.hlsl`, `instancedata.hlsli` 의 `highlight` 칸) · 다섯 파이프라인 XML · 엔진 기본 에셋 `_shaderHighlightOutline` | 코드는 패치로만 있다 — 셰이더 쿠킹 게이트(`CheckShaderStamps`)가 쿠킹 전 커밋을 막아 `wt/gpB` 의 stash(`gpB highlight renderer`)와 스크래치패드 `gpB-highlight-renderer.patch` 로 남겼다 |
| H3 | 화면 확인 — 네 백엔드에서 외곽선이 대상 둘레에만, 감각 모드가 벽 너머로 보이는지 | 남음(GPU) |
| H4 | 몽타주 실행 확인 — 상호작용 단계 몽타주가 애니메이터를 실제로 부르는지(그래프 에셋이 있는 씬). 3D 기믹 변형의 겹침은 `GimmickGenreTest.Gimmick3DTriggerVariantsReceiveOverlapsInA3DScene` 이 본다 | 남음(씬 실행) |

## 남은 것

- H2 적용: 패치를 얹고(`git apply gpB-highlight-renderer.patch`) `App.exe --cook-shaders`, `ShaderCookRequestTest` 가 알리는 낡은 바이너리를 지운 뒤 `ShaderBindingValidatorTest.InstanceElementLayoutMatchesCPUStruct`(이제 `highlight` 칸을 대조한다)와 `RenderPassTest` 필터.
- 함정: 같은 시기의 다른 작업자(비동기 로드 · 디더 등장)도 `GPUInstance` 의 예약 칸을 쓴다 — 병합 때 칸을 하나씩 나눠 가져야 한다(원소 크기 128 바이트를 지킨다).
- 반투명 메시는 강조 패스가 보지 않는다(불투명 배치만 그린다). 필요해지면 반투명 꼬리 배치를 같은 셰이더로 그리는 패스를 더한다.
