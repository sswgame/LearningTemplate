/**
 * @file forwardlit.hlsl
 * @brief 기본 PBR 머티리얼 셰이더 — 정점 색 × 머티리얼 색 × 알베도 텍스처를 금속도 · 거칠기 PBR 로 비춘다.
 * @details 머티리얼 셰이더 뼈대(materialshader.hlsli · materialshadermain.hlsli)의 사용자다. 이 파일은 훅만 정의한다.
 *          정점 변형 · 그림자 · 깊이 패스 · G버퍼 서명 · 보기 모드 · 반투명 알파는 뼈대가 한다.
 *          조명 훅은 포워드 림 라이트를 더하려고 직접 정의한다(디퍼드 조명 패스는 같은 림을 다른 식으로 더한다).
 *          머티리얼 define: `MATERIAL_WORLD_UV`(월드 좌표 트라이플래너 UV — 블록아웃용) · `MATERIAL_BLEND_TRANSLUCENT`(반투명, 머티리얼 blendMode 와 짝).
 *          알파 컷오프는 이 셰이더에 없다(`MATERIAL_ALPHA_CUTOFF` 는 `#error`) — 컷오프 프로퍼티가 필요하면 머티리얼 구조체에 더하고 표면 훅이 채운다.
 */
#include "lighting.hlsli"

// defaultmaterial.material 의 프로퍼티 순서·타입과 1:1 (color/roughness/metallic/albedoMap). 머티리얼 패커는
// 리플렉션 오프셋(StructuredBuffer 원소 레이아웃)으로 채우므로 여기 순서를 바꾸면 에셋도 같이 바꿔야 한다.
// 금속도 · 거칠기는 PBR 조명 식(lighting.hlsli Lighting::shadeLights)과 G버퍼 머티리얼 칸(SurfaceOutput::store)이 읽는다.
SW_MATERIAL_BEGIN
{
    float4 _color;
    float  _roughness;
    float  _metallic;
    uint   _albedoMap;
}
SW_MATERIAL_END

#if defined( MATERIAL_ALPHA_CUTOFF )
    #error "forwardlit.hlsl has no alpha cutoff property (MaterialSurface._alphaCutoff) — add one to the material struct and fill it in computeMaterialSurface"
#endif

#define SW_MATERIAL_CUSTOM_LIGHTING
#define SW_MATERIAL_DEFAULT_VARYINGS
#include "materialshader.hlsli"

#if defined( MATERIAL_WORLD_UV )
// 월드 좌표 UV(트라이플래너) — 메시 UV 대신 월드 위치로 텍스처를 읽는다. 텍스처 한 장이 1 m 이고 면의 방향(노멀)에 따라 세 축 투영을 섞는다.
// 언리얼 WorldGridMaterial 과 같은 블록아웃용이다 — 메시 크기와 상관없이 한 칸이 1 m 라 거리 · 높이를 눈으로 잴 수 있다(prototypegrid_*.material).
static const float kWorldUvTilesPerMeter = 1.0f;
static const float kTriplanarSharpness   = 4.0f; // 노멀 성분의 거듭제곱 — 클수록 축 사이 섞이는 띠가 좁다
float4 sampleWorldTriplanar( uint textureIndex, float3 worldPosition, float3 worldNormal )
{
    float3 weight = pow( abs( worldNormal ), kTriplanarSharpness );
    weight /= max( weight.x + weight.y + weight.z, 1e-4f );
    const float3 scaledPosition = worldPosition * kWorldUvTilesPerMeter;
    // 옆면은 위(+Y)가 텍스처 위(v 가 작은 쪽)가 되게 v 를 뒤집는다.
    const float4 alongX = MaterialTexture::sampleIndex( textureIndex, float2( scaledPosition.z, -scaledPosition.y ) );
    const float4 alongY = MaterialTexture::sampleIndex( textureIndex, float2( scaledPosition.x, scaledPosition.z ) );
    const float4 alongZ = MaterialTexture::sampleIndex( textureIndex, float2( scaledPosition.x, -scaledPosition.y ) );
    return alongX * weight.x + alongY * weight.y + alongZ * weight.z;
}

float4 sampleSurfaceTexture( PixelContext context )
{
    return sampleWorldTriplanar( context._material._albedoMap, context._worldPosition, context._normal );
}
#else
float4 sampleSurfaceTexture( PixelContext context )
{
    return MaterialTexture::sampleIndex( context._material._albedoMap, context._uv );
}
#endif

MaterialSurface computeMaterialSurface( PixelContext context )
{
    // 정점 색 x 머티리얼 색 x 알베도 텍스처. 텍스처 인덱스가 없거나(kInvalidIndex) 백엔드가 bindless 에뮬(DX11/GL)이면 흰색이 곱해진다 —
    // 자세한 이유는 binding.hlsli 의 MaterialTexture::sampleIndex 주석 참고.
    const float4    surfaceTexture = sampleSurfaceTexture( context );
    MaterialSurface surface;
    surface._albedo    = context._color * context._material._color * surfaceTexture;
    surface._normal    = context._normal;
    surface._roughness = context._material._roughness;
    surface._metallic  = context._material._metallic;
    return surface;
}

#if SW_VIEWMODE_SKIPS_LIGHTING
float4 computeMaterialLighting( PixelContext context, MaterialSurface surface )
{
    // 보기 모드 Unlit — 조명 · 그림자 · 림 없이 알베도 그대로.
    return surface._albedo;
}
#else
float4 computeMaterialLighting( PixelContext context, MaterialSurface surface )
{
    // 조명 식은 디퍼드와 **같은 함수**다(lighting.hlsli). 두 벌로 두면 두 경로의 그림이 갈라진다.
    const float shadow = Shadow::sampleAtWorld( context._worldPosition, surface._normal );
    float3      lit    = Lighting::shadeLights( surface._albedo.rgb, context._worldPosition, surface._normal, shadow, surface._roughness, surface._metallic );
    lit += Lighting::computeRimLight( surface._normal );
    return float4( lit, surface._albedo.a );
}
#endif

#include "materialshadermain.hlsli"
