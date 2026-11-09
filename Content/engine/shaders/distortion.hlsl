/**
 * distortion.hlsl — 화면 왜곡 머티리얼 셰이더(충격파 고리 · 아지랑이 · 마법 물결 · 유리 · 얼음 굴절). 언리얼 머티리얼의 Refraction 출력 · 유니티 왜곡 셰이더의 자리다.
 *
 * 머티리얼은 `_alwaysDefines` 에 MATERIAL_DISTORTION 과 MATERIAL_BLEND_TRANSLUCENT 를 적는다(engine/materials/distortion.material). 그러면 같은 배치가 두 패스에 그려진다.
 *   반투명 패스: 표면 색(_tint, 알파로 세기) — 알파 0 이면 보이지 않고 왜곡만 남는다.
 *   왜곡 누적 패스(SW_PASS_DISTORTION): 왜곡 훅이 화면 UV 오프셋을 낸다 — 화면에 투영한 노멀 방향으로 민다. 정면(시선과 평행한 노멀)은 거의 밀지 않고 가장자리일수록 세다
 *   (구 · 고리 메시가 렌즈처럼 보인다). 세기는 _strength(화면 UV 단위), 정점 색 알파가 곱해진다(VFX 메시 입자의 수명 페이드).
 * 적용은 파이프라인의 왜곡 적용 패스(distortion_apply.hlsl)가 장면 색 사본을 비껴 읽어 한다.
 */
#include "lighting.hlsli"

static const float kSurfaceRoughness = 1.0f;
static const float kSurfaceMetallic  = 0.0f;

SW_MATERIAL_BEGIN
{
    float4 _tint;
    float  _strength;
    uint   _swPad0;
    uint   _swPad1;
    uint   _swPad2;
}
SW_MATERIAL_END

#if defined( MATERIAL_ALPHA_CUTOFF )
    #error "distortion.hlsl has no alpha cutoff property"
#endif

#define SW_MATERIAL_UNLIT
#define SW_MATERIAL_DEFAULT_VARYINGS
#define SW_MATERIAL_CUSTOM_DISTORTION
#include "material_shader.hlsli"

MaterialSurface computeMaterialSurface( PixelContext context )
{
    const MaterialData material = context._material;

    MaterialSurface surface;
    surface._albedo    = material._tint * float4( 1.0f, 1.0f, 1.0f, context._color.a );
    surface._normal    = context._normal;
    surface._roughness = kSurfaceRoughness;
    surface._metallic  = kSurfaceMetallic;
    return surface;
}

/** @brief 화면 UV 오프셋 — 노멀을 지금 뷰의 클립 공간 방향으로 옮겨 그 xy 로 민다(UV 의 y 는 NDC 와 반대). */
float2 computeMaterialDistortion( PixelContext context, MaterialSurface surface )
{
    const MaterialData material  = context._material;
    const float4       clipDir   = mul( float4( surface._normal, 0.0f ), View::getViewProjection() );
    const float2       screenDir = clipDir.xy * float2( 0.5f, -0.5f );
    return screenDir * material._strength * context._color.a;
}

#include "material_shader_main.hlsli"
