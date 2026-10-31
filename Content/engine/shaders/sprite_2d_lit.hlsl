/**
 * sprite_2d_lit.hlsl — 2D 빛을 받는 스프라이트(유니티 Sprite-Lit-Default · Godot CanvasItem + Light2D).
 *
 * 머티리얼 셰이더 뼈대(material_shader.hlsli · material_shader_main.hlsli)의 사용자다. 이 파일은 월드 위치 오프셋(픽셀 스냅) · 사용자 보간 칸 · 표면 · 조명 훅만
 * 정의한다. 정점 단계는 sprite_2d.hlsl 과 같다(인스턴스의 아틀라스 프레임 · 색 · 픽셀 스냅). 조명 훅은 알베도에 2D 빛(lighting_2d.hlsli)을 곱하고,
 * 노멀 맵이 있으면 N·L 을 곱한다. 노멀 맵은 탄젠트 공간(빨강 = 스프라이트의 +X, 초록 = +Y, 파랑 = 카메라 쪽)이다.
 * 스프라이트 머티리얼은 MATERIAL_VERTEX_DEFORM(픽셀 스냅이 오프셋이다) · MATERIAL_BLEND_TRANSLUCENT(알파를 쓴다)를 가진다.
 */
#include "lighting_2d.hlsli"

// 보기 모드 Roughness · Metallic 이 보이는 값 — 스프라이트는 PBR 칸이 없는 거친 유전체로 본다.
static const float kSurfaceRoughness = 1.0f;
static const float kSurfaceMetallic  = 0.0f;

SW_MATERIAL_BEGIN
{
    float4 _color;
    float4 _uvRect;
    uint   _albedoMap;
    uint   _normalMap;
    uint   _pointFilter;
}
SW_MATERIAL_END

#if defined( MATERIAL_ALPHA_CUTOFF )
    #error "sprite_2d_lit.hlsl has no alpha cutoff property (MaterialSurface._alphaCutoff) — add one to the material struct and fill it in computeMaterialSurface"
#endif

// 훅 선택(material_shader.hlsli) — 조명은 2D 빛이라 직접 정의한다. 정점이 만든 UV · 색 · 스프라이트 축(노멀 맵 방향)을 사용자 보간 칸으로 넘긴다.
#define SW_MATERIAL_CUSTOM_LIGHTING
#define SW_MATERIAL_CUSTOM_VARYINGS
struct MaterialVaryings
{
    float2 _frameUv : TEXCOORD8;    // 인스턴스의 UV 사각형(아틀라스 프레임)이 들어간 UV
    float4 _tinted : TEXCOORD9;     // 정점 색 × 인스턴스 색
    float2 _tangent : TEXCOORD10;   // 스프라이트 +X 의 월드 방향(XY) — 노멀 맵 빨강
    float2 _bitangent : TEXCOORD11; // 스프라이트 +Y 의 월드 방향(XY) — 노멀 맵 초록
};
#include "material_shader.hlsli"

/** @brief 픽셀 스냅 — sprite_2d.hlsl 과 같다(인스턴스 원점을 자산 픽셀 격자에). */
float3 computeWorldPositionOffset( VertexContext context )
{
    float3 offset = float3( 0.0f, 0.0f, 0.0f );
    if ( context._instance._pixelSnap > 0.0f )
    {
        const float2 origin  = mul( float4( 0.0f, 0.0f, 0.0f, 1.0f ), context._instance._world ).xy;
        const float2 snapped = floor( origin / context._instance._pixelSnap + 0.5f ) * context._instance._pixelSnap;
        offset.xy            = snapped - origin;
    }
    return offset;
}

MaterialVaryings computeMaterialVaryings( VertexContext context )
{
    const float4     frame = context._instance.computeUvRect();
    MaterialVaryings varyings;
    varyings._frameUv   = context._input._uv * frame.zw + frame.xy;
    varyings._tinted    = context._input._color * context._instance.computeTint();
    const float2 axisX  = mul( float4( 1.0f, 0.0f, 0.0f, 0.0f ), context._instance._world ).xy;
    const float2 axisY  = mul( float4( 0.0f, 1.0f, 0.0f, 0.0f ), context._instance._world ).xy;
    varyings._tangent   = axisX / max( length( axisX ), 1e-6f );
    varyings._bitangent = axisY / max( length( axisY ), 1e-6f );
    return varyings;
}

MaterialSurface computeMaterialSurface( PixelContext context )
{
    const MaterialData material     = context._material;
    const float2       uv           = context._user._frameUv * material._uvRect.zw + material._uvRect.xy;
    float4             textureColor = float4( 1.0f, 1.0f, 1.0f, 1.0f );
    if ( material._albedoMap != kInvalidIndex )
        textureColor = ( material._pointFilter != 0u ) ? MaterialTexture::samplePoint( material._albedoMap, uv ) : MaterialTexture::sampleIndex( material._albedoMap, uv );

    float3 normal = float3( 0.0f, 0.0f, -1.0f );
    if ( material._normalMap != kInvalidIndex )
    {
        const float3 encoded       = ( material._pointFilter != 0u ) ? MaterialTexture::samplePoint( material._normalMap, uv ).xyz : MaterialTexture::sampleIndex( material._normalMap, uv ).xyz;
        const float3 tangentNormal = encoded * 2.0f - 1.0f;
        normal                     = normalize( float3( context._user._tangent * tangentNormal.x + context._user._bitangent * tangentNormal.y, -tangentNormal.z ) );
    }

    MaterialSurface surface;
    surface._albedo    = textureColor * material._color * context._user._tinted;
    surface._normal    = normal;
    surface._roughness = kSurfaceRoughness;
    surface._metallic  = kSurfaceMetallic;
    return surface;
}

#if SW_VIEWMODE_SKIPS_LIGHTING
float4 computeMaterialLighting( PixelContext context, MaterialSurface surface )
{
    // 보기 모드(Unlit · Normals · Depth · Overdraw) — 2D 빛 없이 보기 모드의 색(뼈대의 SurfaceOutput::store 가 바꾼다).
    return surface._albedo;
}
#else
float4 computeMaterialLighting( PixelContext context, MaterialSurface surface )
{
    const bool bHasNormal = context._material._normalMap != kInvalidIndex;
    // 보기 모드 LightingOnly 는 2D 빛은 그대로 두고 표면 색만 회색으로 바꾼다(Lighting2d::shadeLights 는 Lighting::shadeLights 를 거치지 않는다). 알파는 텍스처 것 그대로.
    const float3 lit = Lighting2d::shadeLights( ViewMode::overrideLightingAlbedo( surface._albedo.rgb ), context._worldPosition.xy, surface._normal, bHasNormal );
    return float4( lit, surface._albedo.a );
}
#endif

#include "material_shader_main.hlsli"
