/**
 * shadowdepth.hlsl — 머티리얼 셰이더가 없는 배치의 깊이 패스(그림자 `SW_PASS_SHADOW` · 깊이 프리패스 `SW_PASS_DEPTH_PREPASS`).
 *
 * 머티리얼 셰이더 뼈대(materialshader.hlsli · materialshadermain.hlsli)의 **머티리얼 없는 사용자**(SW_MATERIAL_NONE)다 — 훅을 하나도 정의하지 않는다.
 * 위치는 뼈대가 만든다: 그림자도 같은 정점(VAT · 모프 · 스킨)을 봐야 하고, 깊이 프리패스는 **카메라** 깊이를 기본 패스(forwardlit · gbuffer)와 같은 함수로
 * 내 LessEqual 을 통과한다(비트까지 같은 깊이 — 위치를 만드는 코드가 뼈대 한 곳뿐이다).
 * 이 셰이더가 같은 자리를 못 만드는 배치는 뼈대가 빼 준다 — 정점을 옮기는 머티리얼(MATERIAL_VERTEX_DEFORM)은 깊이 프리패스에서(기본 패스의 흔들린 정점이 프리패스
 * 깊이에 가려 구멍이 나지 않게), 그림자를 끈 머티리얼(MATERIAL_SHADOW_CAST_OFF)은 그림자에서. 그 머티리얼의 그림자와 알파 컷오프 머티리얼의 깊이는
 * 머티리얼 셰이더가 그린다(FrameRendererUtil::drawsDepthWithMaterialShader).
 * 픽셀 스테이지는 등장 디더 변형에만 붙는다(FrameRendererUtil::addsAppearanceDitherPixelStage) — 기본 패스와 같은 픽셀을 버려 프리패스 · 그림자가 맞는다.
 */
#include "lighting.hlsli"

#define SW_MATERIAL_NONE
#define SW_MATERIAL_UNLIT
#define SW_MATERIAL_DEFAULT_VARYINGS
#include "materialshader.hlsli"

// 깊이 패스는 표면을 읽지 않는다(알파 컷오프는 머티리얼 셰이더의 몫이다) — 정의할 훅이 없다.

#include "materialshadermain.hlsli"
