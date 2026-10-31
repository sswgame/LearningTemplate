/**
 * sprite_2d.hlsl — 2D 스프라이트(유니티 Sprite-Unlit-Default). 텍스처 × 머티리얼 색 × 정점 색 × 인스턴스 색을 조명 없이 그린다.
 *
 * 머티리얼 셰이더 뼈대(material_shader.hlsli · material_shader_main.hlsli)의 사용자다. 이 파일은 월드 위치 오프셋(픽셀 스냅) · 사용자 보간 칸 · 표면 훅만 정의한다.
 * 스프라이트 머티리얼은 MATERIAL_VERTEX_DEFORM(픽셀 스냅이 오프셋이다) · MATERIAL_BLEND_TRANSLUCENT(알파를 쓴다)를 가진다.
 */
#include "lighting.hlsli"

// 보기 모드 Roughness · Metallic 이 보이는 값 — 스프라이트는 PBR 칸이 없는 거친 유전체로 본다.
static const float kSurfaceRoughness = 1.0f;
static const float kSurfaceMetallic  = 0.0f;

SW_MATERIAL_BEGIN
{
    float4 _color;
    float4 _uvRect;
    uint   _albedoMap;
    uint   _pointFilter;
    uint   _premultipliedTexture;
}
SW_MATERIAL_END

#if defined( MATERIAL_ALPHA_CUTOFF )
    #error "sprite_2d.hlsl has no alpha cutoff property (MaterialSurface._alphaCutoff) — add one to the material struct and fill it in computeMaterialSurface"
#endif

// 훅 선택(material_shader.hlsli) — 조명 없음. 정점이 만든 UV(아틀라스 프레임) · 색(인스턴스 색)을 사용자 보간 칸으로 넘긴다. 머티리얼은 픽셀에서만 읽는다.
#define SW_MATERIAL_UNLIT
#define SW_MATERIAL_CUSTOM_VARYINGS
struct MaterialVaryings
{
    float2 _frameUv : TEXCOORD8; // 인스턴스의 UV 사각형(아틀라스 프레임)이 들어간 UV
    float4 _tinted : TEXCOORD9;  // 정점 색 × 인스턴스 색
};
#include "material_shader.hlsli"

/**
 * @brief 픽셀 스냅: 인스턴스 원점의 X · Y 를 자산 픽셀 격자(1 / PPU)에 붙인다(정점마다가 아니라 원점을 — 사각형이 찌그러지지 않는다).
 * @details 그리는 눈은 화면 픽셀 격자에 붙어 있고 배율이 정수라(PixelPerfectCameraComponent) 자산 픽셀이 화면 픽셀 배율 칸에 딱 맞고, 스프라이트끼리 아트 픽셀이 어긋나지 않는다.
 */
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
    // 사각형의 UV 는 메시의 것이다(MeshUtil::createSpriteQuad — 양면이고 면마다 u 의 방향이 달라 어느 쪽에서 봐도 뒤집히지 않는다).
    // 위치에서 지어내면(u = x + 0.5) 그 u 는 이 사각형이 보이는 쪽 카메라에서 화면 왼쪽으로 늘어 모든 스프라이트가 좌우로 뒤집힌다.
    // 인스턴스의 UV 사각형(아틀라스 프레임)은 여기서, 머티리얼의 uvRect 는 픽셀 단계에서 그 위에 적용한다 — 머티리얼은 픽셀 단계에서만 읽는다
    // (binding.hlsli SW_MATERIAL: 두 단계가 읽으면 GL 이 링크를 거절한다). 둘 다 아핀이라 정점에서 한 것과 픽셀에서 한 것이 같다.
    // 합성은 "머티리얼 사각형 안의 인스턴스 사각형" 이다.
    // 인스턴스 색은 정점 색에 곱해 넘긴다 — 프레임 · 색은 머티리얼 인스턴스가 아니라 인스턴스에 실려, 같은 텍스처의 스프라이트가 한 배치로 남는다.
    const float4     frame = context._instance.computeUvRect();
    MaterialVaryings varyings;
    varyings._frameUv = context._input._uv * frame.zw + frame.xy;
    varyings._tinted  = context._input._color * context._instance.computeTint();
    return varyings;
}

MaterialSurface computeMaterialSurface( PixelContext context )
{
    const MaterialData material = context._material;
    // 머티리얼 uvRect (x=u, y=v, z=폭, w=높이) — 인스턴스 사각형이 이미 들어간 UV 위에 얹는다
    const float2 uv           = context._user._frameUv * material._uvRect.zw + material._uvRect.xy;
    float4       textureColor = float4( 1.0f, 1.0f, 1.0f, 1.0f );
    if ( material._albedoMap != kInvalidIndex )
    {
        // 점 필터(픽셀 아트)면 텍셀 중심에 붙여 읽는다 — 확대해도 텍셀 경계가 번지지 않는다(유니티 Filter Mode Point · Godot TEXTURE_FILTER_NEAREST).
        textureColor = ( material._pointFilter != 0u ) ? MaterialTexture::samplePoint( material._albedoMap, uv ) : MaterialTexture::sampleIndex( material._albedoMap, uv );
        // 프리멀티플라이 텍스처(월드 공간 UI 의 렌더 텍스처 — 캔버스가 프리멀티플라이로 그린다)는 곧은 알파로 되돌린다 — 투명 블렌드가 SrcAlpha 를 다시 곱한다.
        if ( material._premultipliedTexture != 0u && textureColor.a > 0.0f )
            textureColor.rgb /= textureColor.a;
    }

    MaterialSurface surface;
    surface._albedo    = textureColor * material._color * context._user._tinted;
    surface._normal    = context._normal;
    surface._roughness = kSurfaceRoughness;
    surface._metallic  = kSurfaceMetallic;
    return surface;
}

#include "material_shader_main.hlsli"
