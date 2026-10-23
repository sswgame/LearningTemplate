/**
 * water.hlsl — 물(WaterBodyComponent). 거스트너 파도로 정점을 옮기고, 깊이 색 · 프레넬 · 반사광 · 잔물결 · 물가 거품으로 칠한다.
 *
 * 머티리얼 셰이더 뼈대(materialshader.hlsli · materialshadermain.hlsli)의 사용자다. 이 파일은 월드 위치 오프셋(파도) · 사용자 보간 칸 · 표면 · 조명 훅만
 * 정의한다. 위치는 뼈대가 만든다 — 파도는 오프셋으로만 준다(물은 MATERIAL_VERTEX_DEFORM 을 가진다).
 *
 * 머티리얼은 **정점 셰이더만** 읽는다 — GL(ARB_gl_spirv)은 구조버퍼를 정점 · 픽셀 두 단계에서 읽으면 링크를 거절한다. 픽셀이 필요한 값은
 * 사용자 보간 칸으로 넘긴다. 반투명 패스에는 장면 깊이 · 색 입력이 없어 굴절 · 화면 공간 두께는 없다: 깊이는 컴포넌트가 지형에서 재어 정점 색 알파에
 * 구워 둔다(m). 정점 색 rg 는 강의 흐름 방향이다(호수는 0).
 */
#include "lighting.hlsli"

// 보기 모드 Roughness · Metallic 이 보이는 값 — 물은 매끈한 유전체다.
static const float kSurfaceRoughness = 0.0f;
static const float kSurfaceMetallic  = 0.0f;
// 물 몸통(Lighting::shadeLights)의 거칠기 — 몸통은 확산만 받고 반사는 프레넬 · 주광 반사가 따로 한다(같은 빛이 두 번 반짝이지 않게 가장 거친 값).
static const float kBodyRoughness = 1.0f;
#include "gerstner.hlsli"

// water.material 의 프로퍼티와 같은 이름 · 타입이다.
SW_MATERIAL_BEGIN
{
    float4 wave0;
    float4 wave1;
    float4 wave2;
    float4 wave3;
    float4 waterParams; // x = 파도 시간, y = 깊은 색이 다 차는 깊이, z = 거품 폭, w = 잔물결 세기
    float4 shallowColor;
    float4 deepColor;
    float4 skyColor;
    float4 waveParams; // x = 파도 분산의 중력(m/s²) — 컴포넌트가 설정된 물리 중력을 싣는다
}
SW_MATERIAL_END

#if defined( MATERIAL_ALPHA_CUTOFF )
    #error "water.hlsl has no alpha cutoff property (MaterialSurface.alphaCutoff) — add one to the material struct and fill it in computeMaterialSurface"
#endif

// 훅 선택(materialshader.hlsli) — 머티리얼은 정점에서만 읽고 픽셀이 쓸 값은 사용자 보간 칸으로 넘긴다. 조명(몸통 + 프레넬 + 주광 반사 + 거품)은 직접 정의한다.
#define SW_MATERIAL_READ_IN_VERTEX
#define SW_MATERIAL_CUSTOM_LIGHTING
#define SW_MATERIAL_CUSTOM_VARYINGS
struct MaterialVaryings
{
    float4 waterColor : TEXCOORD8; // rgb = 깊이로 섞은 물 색, a = 불투명도
    float3 toView : TEXCOORD9;     // 표면에서 카메라 쪽(정규화 전)
    float4 shading : TEXCOORD10;   // x = 거품, y = 파도 시간, z = 잔물결 세기, w = 깊이(m)
    float3 skyColor : TEXCOORD11;
    float2 flow : TEXCOORD12;
    float3 waveNormal : TEXCOORD13; // 거스트너 파도의 노멀(정규화 전 — 메시 노멀이 아니다)
};
#include "materialshader.hlsli"

/** @brief 거스트너 파도 넷(머티리얼 프로퍼티)입니다. */
void loadWaves( MaterialData material, out float4 outArrWave[SW_GERSTNER_WAVE_COUNT] )
{
    outArrWave[0] = material.wave0;
    outArrWave[1] = material.wave1;
    outArrWave[2] = material.wave2;
    outArrWave[3] = material.wave3;
}

/** @brief 월드 위치 오프셋 — 파도 변위. 파도는 월드 (x, z) 의 함수다(CPU 질의 WaterWaveMath 가 같은 원점으로 같은 값을 낸다). */
float3 computeWorldPositionOffset( VertexContext context )
{
    float4 arrWave[SW_GERSTNER_WAVE_COUNT];
    loadWaves( context.material, arrWave );
    return Gerstner::computeDisplacement( context.worldPosition.xz, context.material.waterParams.x, context.material.waveParams.x, arrWave );
}

MaterialVaryings computeMaterialVaryings( VertexContext context )
{
    const MaterialData material = context.material;
    float4             arrWave[SW_GERSTNER_WAVE_COUNT];
    loadWaves( material, arrWave );
    const float time = material.waterParams.x;

    MaterialVaryings varyings;
    // 파도는 월드 (x, z) 의 함수다 — 변위를 더하기 전의 원점으로 노멀을 구한다.
    varyings.waveNormal = Gerstner::computeNormal( context.worldPosition.xz, time, material.waveParams.x, arrWave );

    const float depth       = max( context.input.color.a, 0.0f );
    const float depthFactor = saturate( depth / max( material.waterParams.y, 1e-3f ) );
    varyings.waterColor     = lerp( material.shallowColor, material.deepColor, depthFactor );
    const float foamWidth   = material.waterParams.z;
    const float foam        = ( foamWidth > 0.0f ) ? saturate( 1.0f - depth / foamWidth ) : 0.0f;
    varyings.shading        = float4( foam, time, material.waterParams.w, depth );
    varyings.skyColor       = material.skyColor.rgb;
    varyings.flow           = context.input.color.rg;

    // 카메라 쪽 방향: 이 정점의 화면 자리를 가까운 면과 먼 면으로 되돌려 그 차를 쓴다 — 원근 · 직교 모두 맞다. 화면 자리는 뼈대가 만든 최종 클립 위치다.
    const float2 ndc       = context.clipPosition.xy / max( context.clipPosition.w, 1e-5f );
    const float4 nearPoint = mul( float4( ndc, 0.0f, 1.0f ), g_InvViewProj );
    const float4 farPoint  = mul( float4( ndc, 1.0f, 1.0f ), g_InvViewProj );
    varyings.toView        = nearPoint.xyz / nearPoint.w - farPoint.xyz / farPoint.w;
    return varyings;
}

/** @brief 작은 물결 노멀 — 흐름 방향으로 흘러가는 사인 셋의 기울기. */
float3 computeRippleNormal( float2 position, float time, float2 flow )
{
    const float2 drift  = position - flow * time * 0.8f;
    const float2 slopeA = float2( cos( drift.x * 2.3f + time * 1.7f ), cos( drift.y * 2.9f - time * 1.3f ) );
    const float2 slopeB = float2( cos( ( drift.x + drift.y ) * 4.1f + time * 2.6f ), cos( ( drift.x - drift.y ) * 3.7f - time * 2.2f ) ) * 0.5f;
    const float2 slope  = ( slopeA + float2( slopeB.x + slopeB.y, slopeB.x - slopeB.y ) ) * 0.12f;
    return normalize( float3( -slope.x, 1.0f, -slope.y ) );
}

MaterialSurface computeMaterialSurface( PixelContext context )
{
    const float3 rippleNormal = computeRippleNormal( context.worldPosition.xz, context.user.shading.y, context.user.flow );

    MaterialSurface surface;
    surface.albedo    = float4( context.user.waterColor.rgb, 1.0f );
    surface.normal    = normalize( lerp( context.user.waveNormal, normalize( context.user.waveNormal + rippleNormal - float3( 0.0f, 1.0f, 0.0f ) ), context.user.shading.z ) );
    surface.roughness = kSurfaceRoughness;
    surface.metallic  = kSurfaceMetallic;
    return surface;
}

float4 computeMaterialLighting( PixelContext context, MaterialSurface surface )
{
    const float3 normal = surface.normal;
    const float3 toView = normalize( context.user.toView );

    // 프레넬(슐릭, 물 F0 = 0.02) — 비스듬히 볼수록 하늘이 비친다.
    const float facing  = saturate( dot( normal, toView ) );
    const float fresnel = 0.02f + 0.98f * pow( 1.0f - facing, 5.0f );
#if SW_VIEWMODE_SKIPS_LIGHTING
    // 보기 모드 Unlit — 물 색 그대로, 주광 반사 없음(하늘 반사 · 거품은 조명이 아니라 표면 색이라 남긴다).
    const float3 body     = context.user.waterColor.rgb;
    const float  specular = 0.0f;
#else
    const float  shadow = Shadow::sampleAtWorld( context.worldPosition, normal );
    const float3 body   = Lighting::shadeLights( context.user.waterColor.rgb, context.worldPosition, normal, shadow, kBodyRoughness, kSurfaceMetallic );

    // 주광의 반사(블린-퐁) — 물은 매끈하다.
    const float3 toLight  = normalize( -g_KeyLightDirIntensity.xyz );
    const float3 halfway  = normalize( toLight + toView );
    const float  specular = pow( saturate( dot( normal, halfway ) ), 180.0f ) * g_KeyLightDirIntensity.w * shadow;
#endif

    float3 color = lerp( body, context.user.skyColor, fresnel ) + specular * g_KeyLightColor.rgb;
    float  alpha = saturate( max( context.user.waterColor.a, fresnel ) + specular );

    // 물가 거품 — 얕을수록 희다. 잔물결 무늬로 끊어 띠가 아니라 거품처럼 보이게 한다.
    const float foamPattern = saturate( 0.5f + 0.5f * sin( context.worldPosition.x * 7.0f + context.worldPosition.z * 5.0f + context.user.shading.y * 1.5f ) );
    const float foam        = context.user.shading.x * ( 0.55f + 0.45f * foamPattern );
    color                   = lerp( color, float3( 0.92f, 0.95f, 0.97f ), foam );
    alpha                   = max( alpha, foam );
    return float4( color, alpha );
}

#include "materialshadermain.hlsli"
