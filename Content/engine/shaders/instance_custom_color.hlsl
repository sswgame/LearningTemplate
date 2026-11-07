/**
 * instance_custom_color.hlsl — 인스턴스 사용자 값(InstanceCustomData)을 머티리얼 셰이더가 읽는 본보기. 머티리얼 색 × 인스턴스 값 0 을 조명 없이 그린다.
 *
 * 인스턴스 값은 정점 단계에서 읽어 보간 칸으로 넘긴다 — 머티리얼은 픽셀 단계에서만 읽는다(binding.hlsli SW_MATERIAL: 두 단계가 읽으면 GL 이 링크를 거절한다).
 * 같은 머티리얼의 인스턴스는 값이 달라도 한 배치다. 시나리오 game/empty/automation/instancecustomdata 가 끄고 켠 두 장을 견준다.
 */
#include "lighting.hlsli"

static const float kSurfaceRoughness = 1.0f;
static const float kSurfaceMetallic  = 0.0f;

SW_MATERIAL_BEGIN
{
    float4 _color;
}
SW_MATERIAL_END

#if defined( MATERIAL_ALPHA_CUTOFF )
    #error "instance_custom_color.hlsl has no alpha cutoff property"
#endif

#define SW_MATERIAL_UNLIT
#define SW_MATERIAL_CUSTOM_VARYINGS
struct MaterialVaryings
{
    float4 _customColor : TEXCOORD8; // 인스턴스 값 0
};
#include "material_shader.hlsli"

MaterialVaryings computeMaterialVaryings( VertexContext context )
{
    MaterialVaryings varyings;
    varyings._customColor = context._instance.getCustomData( 0u );
    return varyings;
}

MaterialSurface computeMaterialSurface( PixelContext context )
{
    const MaterialData material = context._material;

    MaterialSurface surface;
    surface._albedo    = material._color * context._user._customColor;
    surface._normal    = context._normal;
    surface._roughness = kSurfaceRoughness;
    surface._metallic  = kSurfaceMetallic;
    return surface;
}

#include "material_shader_main.hlsli"
