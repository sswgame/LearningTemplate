/**
 * post_process_color_tint.hlsl — 후처리 머티리얼 본보기(손으로 쓴 PostProcess 영역 셰이더): 장면 색을 덜 채도로 바꾸고 색을 입히며, 멀수록 안개색 쪽으로 민다.
 * 머티리얼은 engine/materials/postprocess/colortint.material 이고 카메라 · 볼륨의 블렌더블 목록(`PostProcessValues::_listBlendable`)에 넣어 쓴다.
 * 그래프로 만든 후처리 머티리얼(`PostProcessOutput`)도 같은 뼈대(post_process_material.hlsli)로 생성된다.
 */
#include "lighting.hlsli"

// colortint.material 의 프로퍼티 순서 · 타입과 1:1 이다.
SW_MATERIAL_BEGIN
{
    float4 _tint;         ///< 곱하는 색(알파 = 덮임)
    float4 _farColor;     ///< 먼 곳이 다가가는 색
    float  _desaturation; ///< 0 원색 · 1 회색
    float  _farDistance;  ///< 이 뷰 깊이(m)에서 먼 색이 다 된다(0 이면 쓰지 않는다)
    uint   _swPad0;
    uint   _swPad1;
}
SW_MATERIAL_END

#include "post_process_material.hlsli"

float4 computePostProcessMaterial( PixelContext context )
{
    const MaterialData material  = context._material;
    const float3       source    = context._color.rgb;
    const float        luminance = CommonMath::computeLuminance( source );
    const float3       tinted    = lerp( source, float3( luminance, luminance, luminance ), saturate( material._desaturation ) ) * material._tint.rgb;
    const float        viewDepth = PostProcessInput::computeLinearDepth( context._uv );
    const float        farWeight = material._farDistance > 0.0f ? saturate( viewDepth / material._farDistance ) * material._farColor.a : 0.0f;
    return float4( lerp( tinted, material._farColor.rgb, farWeight ), material._tint.a );
}

#include "post_process_material_main.hlsli"
