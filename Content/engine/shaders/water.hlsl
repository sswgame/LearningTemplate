/**
 * water.hlsl — 물(WaterBodyComponent). 거스트너 파도로 정점을 옮기고, 깊이 색 · 프레넬 · 반사광 · 잔물결 · 물가 거품으로 칠한다.
 *
 * 머티리얼 셰이더 뼈대(material_shader.hlsli · material_shader_main.hlsli)의 사용자다. 이 파일은 월드 위치 오프셋(파도) · 사용자 보간 칸 · 표면 · 조명 훅만
 * 정의한다. 위치는 뼈대가 만든다 — 파도는 오프셋으로만 준다(물은 MATERIAL_VERTEX_DEFORM 을 가진다).
 *
 * 머티리얼은 **정점 셰이더만** 읽는다 — GL(ARB_gl_spirv)은 구조버퍼를 정점 · 픽셀 두 단계에서 읽으면 링크를 거절한다. 픽셀이 필요한 값은
 * 사용자 보간 칸으로 넘긴다. 굴절은 반투명 앞에 떠 둔 장면 색 사본(`g_SceneColorCopyIndex` — 파이프라인의 SceneColorCopy 패스)을 파도 노멀로 비껴 읽고,
 * 화면 공간 두께는 장면 깊이 사본(`g_SceneDepthCopyIndex` — SceneDepthCopy 패스)에서 구한다. 사본이 걸리지 않은 파이프라인(2D 등)은 굴절 없이 알파 블렌드이고,
 * 깊이 사본만 없으면 두께는 컴포넌트가 지형에서 재어 정점 색 알파에 구워 둔 깊이(m)를 쓴다. 정점 색 rg 는 강의 흐름 방향이다(호수는 0).
 */
#include "lighting.hlsli"

// 보기 모드 Roughness · Metallic 이 보이는 값 — 물은 매끈한 유전체다.
static const float kSurfaceRoughness = 0.0f;
static const float kSurfaceMetallic  = 0.0f;
// 물 몸통(Lighting::shadeLights)의 거칠기 — 몸통은 확산만 받고 반사는 프레넬 · 주광 반사가 따로 한다(같은 빛이 두 번 반짝이지 않게 가장 거친 값).
static const float kBodyRoughness = 1.0f;
// 굴절 — 파도 노멀의 xz 를 화면 uv 오프셋으로 쓰는 세기(uv 단위)와, 그 오프셋이 다 차는 물 두께(m). 얕은 물가는 오프셋이 0 에 가까워 가장자리가 튀지 않는다.
static const float kRefractionStrength  = 0.05f;
static const float kRefractionFullDepth = 1.5f;
// 물이 장면을 가리는 정도 = 1 - exp(-두께 × 이 값). 깊이 사본이 걸려 장면까지의 실제 두께를 알 때만 쓴다(1/m).
static const float kAbsorptionPerMeter = 0.8f;
// 깊이 비교의 여유(장치 깊이) — 물 표면과 같은 깊이의 장면(물가에 닿은 바닥)을 물 앞으로 오판하지 않는다.
static const float kDepthCompareBias = 1e-5f;
#include "gerstner.hlsli"

// water.material 의 프로퍼티와 같은 이름 · 타입이다.
SW_MATERIAL_BEGIN
{
    float4 _wave0;
    float4 _wave1;
    float4 _wave2;
    float4 _wave3;
    float4 _waterParams; // x = 파도 시간, y = 깊은 색이 다 차는 깊이, z = 거품 폭, w = 잔물결 세기
    float4 _shallowColor;
    float4 _deepColor;
    float4 _skyColor;
    float4 _waveParams; // x = 파도 분산의 중력(m/s²) — 컴포넌트가 설정된 물리 중력을 싣는다
}
SW_MATERIAL_END

#if defined( MATERIAL_ALPHA_CUTOFF )
    #error "water.hlsl has no alpha cutoff property (MaterialSurface._alphaCutoff) — add one to the material struct and fill it in computeMaterialSurface"
#endif

// 훅 선택(material_shader.hlsli) — 머티리얼은 정점에서만 읽고 픽셀이 쓸 값은 사용자 보간 칸으로 넘긴다. 조명(몸통 + 프레넬 + 주광 반사 + 거품)은 직접 정의한다.
#define SW_MATERIAL_READ_IN_VERTEX
#define SW_MATERIAL_CUSTOM_LIGHTING
#define SW_MATERIAL_CUSTOM_VARYINGS
struct MaterialVaryings
{
    float4 _waterColor : TEXCOORD8; // rgb = 깊이로 섞은 물 색, a = 불투명도
    float3 _toView : TEXCOORD9;     // 표면에서 카메라 쪽(정규화 전)
    float4 _shading : TEXCOORD10;   // x = 거품, y = 파도 시간, z = 잔물결 세기, w = 깊이(m)
    float3 _skyColor : TEXCOORD11;
    float2 _flow : TEXCOORD12;
    float3 _waveNormal : TEXCOORD13; // 거스트너 파도의 노멀(정규화 전 — 메시 노멀이 아니다)
};
#include "material_shader.hlsli"

/** @brief 거스트너 파도 넷(머티리얼 프로퍼티)입니다. */
void loadWaves( MaterialData material, out float4 outArrWave[SW_GERSTNER_WAVE_COUNT] )
{
    outArrWave[0] = material._wave0;
    outArrWave[1] = material._wave1;
    outArrWave[2] = material._wave2;
    outArrWave[3] = material._wave3;
}

/** @brief 월드 위치 오프셋 — 파도 변위. 파도는 월드 (x, z) 의 함수다(CPU 질의 WaterWaveMath 가 같은 원점으로 같은 값을 낸다). */
float3 computeWorldPositionOffset( VertexContext context )
{
    float4 arrWave[SW_GERSTNER_WAVE_COUNT];
    loadWaves( context._material, arrWave );
    return Gerstner::computeDisplacement( context._worldPosition.xz, context._material._waterParams.x, context._material._waveParams.x, arrWave );
}

MaterialVaryings computeMaterialVaryings( VertexContext context )
{
    const MaterialData material = context._material;
    float4             arrWave[SW_GERSTNER_WAVE_COUNT];
    loadWaves( material, arrWave );
    const float time = material._waterParams.x;

    MaterialVaryings varyings;
    // 파도는 월드 (x, z) 의 함수다 — 변위를 더하기 전의 원점으로 노멀을 구한다.
    varyings._waveNormal = Gerstner::computeNormal( context._worldPosition.xz, time, material._waveParams.x, arrWave );

    const float depth       = max( context._input._color.a, 0.0f );
    const float depthFactor = saturate( depth / max( material._waterParams.y, 1e-3f ) );
    varyings._waterColor    = lerp( material._shallowColor, material._deepColor, depthFactor );
    const float foamWidth   = material._waterParams.z;
    const float foam        = ( foamWidth > 0.0f ) ? saturate( 1.0f - depth / foamWidth ) : 0.0f;
    varyings._shading       = float4( foam, time, material._waterParams.w, depth );
    varyings._skyColor      = material._skyColor.rgb;
    varyings._flow          = context._input._color.rg;

    // 카메라 쪽 방향: 이 정점의 화면 자리를 가까운 면과 먼 면으로 되돌려 그 차를 쓴다 — 원근 · 직교 모두 맞다. 화면 자리는 뼈대가 만든 최종 클립 위치다.
    const float2 ndc       = context._clipPosition.xy / max( context._clipPosition.w, 1e-5f );
    const float4 nearPoint = mul( float4( ndc, 0.0f, 1.0f ), g_InvViewProj );
    const float4 farPoint  = mul( float4( ndc, 1.0f, 1.0f ), g_InvViewProj );
    varyings._toView       = nearPoint.xyz / nearPoint.w - farPoint.xyz / farPoint.w;
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
    const float3 rippleNormal = computeRippleNormal( context._worldPosition.xz, context._user._shading.y, context._user._flow );

    MaterialSurface surface;
    surface._albedo    = float4( context._user._waterColor.rgb, 1.0f );
    surface._normal    = normalize( lerp( context._user._waveNormal, normalize( context._user._waveNormal + rippleNormal - float3( 0.0f, 1.0f, 0.0f ) ), context._user._shading.z ) );
    surface._roughness = kSurfaceRoughness;
    surface._metallic  = kSurfaceMetallic;
    return surface;
}

#if SW_VIEWMODE_SKIPS_LIGHTING
void computeWaterLighting( PixelContext context, float3 normal, float3 toView, out float3 outBody, out float outSpecular )
{
    // 보기 모드 Unlit — 물 색 그대로, 주광 반사 없음(하늘 반사 · 거품은 조명이 아니라 표면 색이라 남긴다).
    outBody     = context._user._waterColor.rgb;
    outSpecular = 0.0f;
}
#else
void computeWaterLighting( PixelContext context, float3 normal, float3 toView, out float3 outBody, out float outSpecular )
{
    const float shadow = Shadow::sampleAtWorld( context._worldPosition, normal );
    outBody            = Lighting::shadeLights( context._user._waterColor.rgb, context._worldPosition, normal, shadow, kBodyRoughness, kSurfaceMetallic );

    // 주광의 반사(블린-퐁) — 물은 매끈하다.
    const float3 toLight = normalize( -g_KeyLightDirIntensity.xyz );
    const float3 halfway = normalize( toLight + toView );
    outSpecular          = pow( saturate( dot( normal, halfway ) ), 180.0f ) * g_KeyLightDirIntensity.w * shadow;
}
#endif

float4 computeMaterialLighting( PixelContext context, MaterialSurface surface )
{
    const float3 normal = surface._normal;
    const float3 toView = normalize( context._user._toView );

    // 프레넬(슐릭, 물 F0 = 0.02) — 비스듬히 볼수록 하늘이 비친다.
    const float facing  = saturate( dot( normal, toView ) );
    const float fresnel = 0.02f + 0.98f * pow( 1.0f - facing, 5.0f );
    float3      body;
    float       specular;
    computeWaterLighting( context, normal, toView, body, specular );

    float3 color = lerp( body, context._user._skyColor, fresnel ) + specular * g_KeyLightColor.rgb;
    float  alpha = saturate( max( context._user._waterColor.a, fresnel ) + specular );

    // 물가 거품 — 얕을수록 희다. 잔물결 무늬로 끊어 띠가 아니라 거품처럼 보이게 한다.
    const float foamPattern = saturate( 0.5f + 0.5f * sin( context._worldPosition.x * 7.0f + context._worldPosition.z * 5.0f + context._user._shading.y * 1.5f ) );
    const float foam        = context._user._shading.x * ( 0.55f + 0.45f * foamPattern );
    color                   = lerp( color, float3( 0.92f, 0.95f, 0.97f ), foam );
    alpha                   = max( alpha, foam );

    // 굴절 · 화면 공간 두께 — 장면 색 사본이 걸린 파이프라인은 물 뒤의 장면을 파도 노멀로 비껴 읽어 물 색과 섞고, 그 픽셀을 불투명하게 덮는다(장면 색은 이미 이 섞기의 한 항이다).
    // 사본은 반투명 전체 앞에 떠 둔 것이라 물보다 먼저 그려진 다른 반투명은 이 픽셀에서 지워진다. 인덱스는 패스 상수라 분기가 고르다.
    SW_BRANCH if ( g_SceneColorCopyIndex != kInvalidIndex )
    {
        const float2 screenUv  = context._position.xy * g_SourceTexel.xy;
        float        thickness = context._user._shading.w; // 정점에 구운 깊이(m) — 깊이 사본이 없을 때의 두께
        float2       refractUv = screenUv;
        SW_BRANCH if ( g_SceneDepthCopyIndex != kInvalidIndex )
        {
            // 장면까지의 실제 두께: 이 픽셀의 장면 깊이로 월드 위치를 복원해 물 표면과의 거리를 잰다. 물보다 앞에 있는 물체(장면 깊이가 더 작다)는 두께 0 이다.
            const float  sceneDepth    = Bindless::sampleIndexWith( g_SceneDepthCopyIndex, SW_SAMPLER_POINT_CLAMP, screenUv ).r;
            const float3 scenePosition = Transform::computeWorldPositionFromDepth( screenUv, sceneDepth );
            const bool   bSceneBehind  = sceneDepth + kDepthCompareBias >= context._position.z;
            thickness                  = bSceneBehind ? length( scenePosition - context._worldPosition ) : 0.0f;
            // 비껴 읽은 자리가 물 앞의 물체면 그 물체의 색을 끌어오지 않는다(굴절을 버리고 제자리를 읽는다).
            const float2 offsetUv       = normal.xz * kRefractionStrength * saturate( thickness / kRefractionFullDepth );
            const float2 candidateUv    = saturate( screenUv + offsetUv );
            const float  candidateDepth = Bindless::sampleIndexWith( g_SceneDepthCopyIndex, SW_SAMPLER_POINT_CLAMP, candidateUv ).r;
            refractUv                   = ( candidateDepth + kDepthCompareBias >= context._position.z ) ? candidateUv : screenUv;
            alpha                       = max( alpha, 1.0f - exp( -thickness * kAbsorptionPerMeter ) );
        }
        else
        {
            refractUv = saturate( screenUv + normal.xz * kRefractionStrength * saturate( thickness / kRefractionFullDepth ) );
        }
        const float3 refracted = Bindless::sampleIndex( g_SceneColorCopyIndex, refractUv ).rgb;
        color                  = lerp( refracted, color, alpha );
        alpha                  = 1.0f;
    }
    return float4( color, alpha );
}

#include "material_shader_main.hlsli"
