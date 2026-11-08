/**
 * binding_output.hlsli — binding.hlsli 의 12 절: 픽셀 출력(`SurfaceOutput`)과 보기 모드.
 * binding.hlsli 앞 절에 기대므로 binding.hlsli 만 이 파일을 include 한다.
 */
#ifndef SW_ENGINE_BINDING_OUTPUT_HLSLI
#define SW_ENGINE_BINDING_OUTPUT_HLSLI

// ------------------------------------------------------------------------------
// 12) 픽셀 출력 — **같은 머티리얼 셰이더가 포워드와 G버퍼 양쪽에 쓰인다.**
//     머티리얼이 셰이더 경로를 정하므로(usesMaterialShader) G버퍼 패스도 머티리얼의 .hlsl 로 그린다.
//     그 셰이더가 SV_TARGET 하나만 내면 **G버퍼의 노멀 타깃이 클리어 값 그대로 남아**, 디퍼드 조명이 모든 픽셀을
//     같은 노멀로 계산한다(오류도 경고도 없이).
//     언리얼이 같은 머티리얼을 패스별 셰이더 **타입**으로 감싸는 것에 해당한다. 여기서는 패스가 define 을
//     추가하고(SW_PASS_GBUFFER), 출력 서명이 그 define 을 따라간다.
// ------------------------------------------------------------------------------
// 양쪽 다 **구조체**다. 포워드 쪽을 `float4` 로 두면 `SurfaceOutput PSMain(...)` 에 반환
// 시맨틱이 사라져 DXC 가 "Semantic must be defined for all outputs" 로 거절한다 — 그러면 머티리얼
// 셰이더가 통째로 컴파일되지 않아 화면이 빈다(실제로 한 번 그랬다).
struct SurfaceOutput
{
#if defined( SW_PASS_GBUFFER )
    float4 _albedo : SV_TARGET0;
    float2 _normal : SV_TARGET1;   // R16G16_SNORM — Octahedral::encode(월드 노멀)
    float4 _material : SV_TARGET2; // SurfaceOutput::encodeGbufferMaterial
#else
    float4 _color : SV_TARGET0;
#endif

    static SurfaceOutput store( float4 litColor, float4 albedo, float3 worldNormal, float3 worldPosition, float roughness, float metallic );
    static SurfaceOutput pack( float4 litColor, float4 albedo, float3 worldNormal, float roughness, float metallic ); // 패스의 출력 서명대로 담는다
    static float4 encodeGbufferMaterial( float roughness, float metallic );
};

// ------------------------------------------------------------------------------
// 라이트 목록과 라이트 그리드 읽기 — 조명 식(lighting.hlsli 의 커서)과 보기 모드 Light Complexity(G버퍼 · 포워드 표면)가 같은 함수를 쓴다.
// 라이트 원소 구조체와 버퍼(g_SwLights)는 lighting.hlsli 다. 여기는 개수와 그리드(g_SwLightGrid, t12)만 안다.
// ------------------------------------------------------------------------------
// 뷰마다의 라이트 그리드(32 비트 단어, 배치는 binding_slots.hlsli 10 절). CPU 생산자와 GPU 생산자가 같은 슬롯에 건다 — 읽는 함수는 LightGrid::loadWord 하나다.
SW_DECLARE_STRUCTURED_BUFFER( uint, g_SwLightGrid, SW_SLOT_LIGHT_GRID_SRV );

/** @brief 이번 패스에 바인딩된 라이트 수입니다. 라이트 버퍼가 바인딩되지 않았으면 0 이고, 그 씬은 앰비언트만 받습니다(키 라이트로 대신하지 않습니다). */
static uint LightGrid::computeLightCount()
{
    return ( g_SwLightsIndex == kInvalidIndex ) ? 0u : g_SwLightCount;
}

/**
 * @brief 지금 뷰의 라이트 그리드 단어 하나를 읽습니다. @p wordIndex 는 그리드 첫 단어 기준입니다(binding_slots.hlsli 10절의 배치).
 * @details 이 함수만 그리드의 위치를 안다(g_SwLightGrid, t12 — CPU · GPU 생산자가 같은 슬롯에 건다).
 */
static uint LightGrid::loadWord( uint wordIndex )
{
    return g_SwLightGrid[g_SwLightGridBase + wordIndex];
}

/** @brief 지금 뷰에 라이트 그리드가 있는지 — 버퍼가 걸려 있고 이 뷰의 그리드 첫 단어가 있어야 한다. 없으면 커서는 목록 전체를 돈다. */
static bool LightGrid::hasGrid()
{
    return g_SwLightGridIndex != kInvalidIndex && g_SwLightGridBase != kInvalidIndex;
}

/**
 * @brief 이 월드 위치가 든 클러스터 번호입니다. C++ `LightGrid::computeClusterIndex` 와 같은 식입니다(LightGridTest 가 C++ 쪽으로 확인한다).
 * @details 클러스터는 지금 뷰의 NDC 를 X · Y 칸으로, 깊이를 조각으로 나눈 것이다. 원근은 클립 w 의 로그 조각, 직교는 NDC z 의 선형 조각이다.
 *          NDC 로 정의하므로 화면 크기 · 해상도 배율과 무관하다. 화면 밖 위치는 가장자리 칸으로 자른다.
 */
static uint LightGrid::computeCluster( float3 worldPosition )
{
    const uint   dimX          = LightGrid::loadWord( SW_LIGHT_GRID_WORD_DIM_X );
    const uint   dimY          = LightGrid::loadWord( SW_LIGHT_GRID_WORD_DIM_Y );
    const uint   dimZ          = LightGrid::loadWord( SW_LIGHT_GRID_WORD_DIM_Z );
    const bool   bPerspective  = LightGrid::loadWord( SW_LIGHT_GRID_WORD_PERSPECTIVE ) != 0u;
    const float  nearDepth     = asfloat( LightGrid::loadWord( SW_LIGHT_GRID_WORD_NEAR_DEPTH ) );
    const float  sliceScale    = asfloat( LightGrid::loadWord( SW_LIGHT_GRID_WORD_SLICE_SCALE ) );
    const float4 clip          = mul( float4( worldPosition, 1.0f ), g_ViewProj );
    const float3 ndc           = clip.xyz / clip.w;
    const uint   tileX         = LightGrid::computeTile( ndc.x, dimX );
    const uint   tileY         = LightGrid::computeTile( ndc.y, dimY );
    const float  slicePosition = LightGrid::computeSlicePosition( bPerspective ? clip.w : ndc.z, bPerspective, nearDepth, sliceScale );
    const uint   slice         = min( (uint)max( slicePosition, 0.0f ), dimZ - 1u );
    return ( slice * dimY + tileY ) * dimX + tileX;
}

/**
 * @brief 이 월드 위치를 비추는 라이트 수입니다(전역 구간 포함). 그리드가 없으면 목록 전체의 수입니다. 보기 모드 Light Complexity 가 색으로 보입니다.
 * @details 커서가 도는 항목 수와 같다 — 조명 식이 실제로 도는 비용이다(언리얼 Light Complexity, 유니티 Forward+ 디버그 보기).
 */
static uint LightGrid::computeVisitCount( float3 worldPosition )
{
    uint visitCount = LightGrid::computeLightCount();
    if ( visitCount > 0u && LightGrid::hasGrid() )
    {
        const uint clusterWord = LightGrid::loadWord( SW_LIGHT_GRID_WORD_CLUSTER_BASE ) + LightGrid::computeCluster( worldPosition ) * SW_LIGHT_GRID_WORDS_PER_CLUSTER;
        visitCount             = LightGrid::loadWord( SW_LIGHT_GRID_WORD_GLOBAL_COUNT ) + LightGrid::loadWord( clusterWord + 1u );
    }
    return visitCount;
}

// 보기 모드 Unlit · Normals · Depth · Overdraw — 조명 · 그림자 · 림 · 반사를 컴파일 아웃한다. 조명을 하는 셰이더는 **모두** 이 매크로로 가르고,
// 내보낼 색은 SurfaceOutput::store 가 보기 모드대로 바꾼다(ViewMode::computeColor). C++ 정본은 RenderView.h 의 보기 모드 표(kArrRenderViewModeInfo)다 —
// 이름이 어긋나면 컴파일은 되고 그림만 안 바뀐다(RenderPassGPUTest.ViewModesProduceDistinctPictures).
#if defined( SW_VIEWMODE_UNLIT ) || defined( SW_VIEWMODE_NORMALS ) || defined( SW_VIEWMODE_DEPTH ) || defined( SW_VIEWMODE_OVERDRAW ) ||               \
    defined( SW_VIEWMODE_ROUGHNESS ) || defined( SW_VIEWMODE_METALLIC ) || defined( SW_VIEWMODE_MIPMAPS ) || defined( SW_VIEWMODE_LIGHTCOMPLEXITY ) || \
    defined( SW_VIEWMODE_WIREOVERLAY ) || defined( SW_VIEWMODE_LODLEVEL ) || defined( SW_VIEWMODE_LODFADE ) || defined( SW_VIEWMODE_SHADINGPATH )
    #define SW_VIEWMODE_SKIPS_LIGHTING 1
#else
    #define SW_VIEWMODE_SKIPS_LIGHTING 0
#endif

// 보기 모드 LightingOnly — 조명은 그대로 계산하고 표면 색만 이 회색으로 바꾼다(언리얼 Lighting Only). 포워드는 Lighting::shadeLights 가,
// 디퍼드는 G버퍼 알베도 칸(SurfaceOutput::store)이 바꾼다 — 디퍼드 조명 패스는 보기 모드 변형이 없으므로 G버퍼에서 바꿔야 한다.
#define SW_VIEWMODE_LIGHTINGONLY_ALBEDO float3( 0.5f, 0.5f, 0.5f )

#if defined( SW_VIEWMODE_LIGHTINGONLY )
static float3 ViewMode::overrideLightingAlbedo( float3 albedo )
{
    return SW_VIEWMODE_LIGHTINGONLY_ALBEDO;
}
#else
static float3 ViewMode::overrideLightingAlbedo( float3 albedo )
{
    return albedo;
}
#endif

// 보기 모드 LODLevel · LODFade — 그리는 인스턴스의 가시 목록 항목(컬링이 쓴 LOD 단계 3 비트 + 크로스페이드 코드 5 비트)을 정점 단계가 픽셀로 넘긴 값이다.
// 항목을 못 받은 셰이더(머티리얼 셰이더가 아닌 것)는 kInvalidIndex 그대로라 회색으로 보인다. 정점 → 픽셀 칸은 머티리얼 셰이더(material_shader.hlsli)만 가진다.
// 매크로로 남긴 이유: 칸은 구조체 **필드 선언**이라 함수가 될 수 없다(SW_APPEARANCE_VARYING 과 같다). 퍼뮤테이션이 칸의 유무를 가른다.
#if defined( SW_VIEWMODE_LODLEVEL ) || defined( SW_VIEWMODE_LODFADE )
static uint g_ViewModeLODEntry = 0xFFFFFFFFu;
    #define SW_LODDEBUG_VARYING                       nointerpolation uint _lodEntryBits : TEXCOORD7;
    #define SW_LODDEBUG_WRITE( output, instanceSlot ) ( output )._lodEntryBits = InstanceData::resolveEntry( instanceSlot );
    #define SW_LODDEBUG_READ( input )                 g_ViewModeLODEntry = ( input )._lodEntryBits;
#else
    #define SW_LODDEBUG_VARYING
    #define SW_LODDEBUG_WRITE( output, instanceSlot )
    #define SW_LODDEBUG_READ( input )
#endif

static const float  kViewModeDepthRange         = 100.0f;                        // Depth 보기: 이 뷰 공간 깊이(m)에서 흰색
static const float  kViewModeOverdrawStep       = 0.08f;                         // Overdraw 보기: 한 번 그릴 때 더하는 밝기(12 겹이면 흰색)
static const float  kViewModeLightComplexityMax = 16.0f;                         // Light Complexity 보기: 이 라이트 수부터 빨강
static const float3 kViewModeWireOverlayColor   = float3( 0.02f, 0.02f, 0.02f ); // ShadedWireframe 의 선 색(셰이딩한 면 위에 겹친다)

/** @brief 라이트 수를 색 띠로 바꿉니다 — 0 은 검정, 1 부터 파랑 · 초록 · 노랑 · 빨강(kViewModeLightComplexityMax 이상)이다. */
static float3 ViewMode::computeLightComplexityColor( uint visitCount )
{
    const float  ratio  = saturate( (float)visitCount / kViewModeLightComplexityMax );
    const float3 cold   = lerp( float3( 0.0f, 0.1f, 0.8f ), float3( 0.0f, 0.9f, 0.2f ), saturate( ratio * 3.0f ) );
    const float3 warm   = lerp( float3( 1.0f, 0.9f, 0.0f ), float3( 1.0f, 0.0f, 0.0f ), saturate( ratio * 3.0f - 2.0f ) );
    const float3 middle = lerp( cold, float3( 1.0f, 0.9f, 0.0f ), saturate( ratio * 3.0f - 1.0f ) );
    return ( visitCount == 0u ) ? float3( 0.0f, 0.0f, 0.0f ) : ( ratio > 2.0f / 3.0f ? warm : middle );
}

static const float3 kViewModeLODLevelColor[8] = { float3( 0.10f, 0.80f, 0.20f ), float3( 0.60f, 0.85f, 0.15f ), float3( 0.95f, 0.90f, 0.10f ), float3( 0.95f, 0.50f, 0.10f ),
                                                  float3( 0.90f, 0.10f, 0.10f ), float3( 0.70f, 0.15f, 0.80f ), float3( 0.15f, 0.30f, 0.95f ), float3( 0.10f, 0.80f, 0.85f ) };

/** @brief LOD 단계(가시 항목의 3 비트)를 색으로 바꿉니다 — 0 초록 · 1 연두 · 2 노랑 · 3 주황 · 4 빨강 · 5 보라 · 6 파랑 · 7 청록. 항목을 못 받았으면(kInvalidIndex 칸) 회색이다. */
static float3 ViewMode::computeLODLevelColor( uint entry )
{
    return entry == 0xFFFFFFFFu ? float3( 0.5f, 0.5f, 0.5f ) : kViewModeLODLevelColor[( entry >> kVisibleEntryLevelShift ) & kVisibleEntryLevelMask];
}

/**
 * @brief 크로스페이드 코드(가시 항목의 위쪽 5비트 — 직전 단계 비트 16 | 진행 L = 1 ~ 15)를 색으로 바꿉니다.
 * @details 전환 없음(코드 0)은 어두운 회색, 새 단계는 초록 · 직전 단계는 빨강이고 진행 L 이 클수록 밝다(새 단계는 L / 16 의 픽셀이 남고 직전 단계는 상보 패턴이 남는다 — 두 색이 겹치지도 비지도 않으면 디더가 맞는 것이다).
 *          항목을 못 받았으면 회색이다.
 */
static float3 ViewMode::computeLODFadeColor( uint entry )
{
    if ( entry == 0xFFFFFFFFu )
        return float3( 0.5f, 0.5f, 0.5f );
    const uint  code     = entry >> kVisibleEntryFadeShift;
    const float progress = float( code & 15u ) / 15.0f;
    const float shade    = 0.3f + 0.7f * progress;
    if ( code == 0u )
        return float3( 0.15f, 0.15f, 0.15f );
    return ( code & 16u ) != 0u ? float3( 0.95f, 0.15f, 0.10f ) * shade : float3( 0.10f, 0.90f, 0.25f ) * shade;
}

/**
 * @brief 보기 모드 ShadingPath 의 색 — 이 셰이더 변형이 어느 경로로 그리는지는 컴파일 때 정해진다(패스 define · 머티리얼 define).
 * @details 디퍼드 G버퍼 조명 파랑 · G버퍼 조명 없음 청회색 · 포워드 전용(MATERIAL_FORWARD_ONLY — 디퍼드가 G버퍼에서 뺀 배치) 주황 · 반투명 보라 · 포워드 초록 · 포워드 조명 없음 짙은 초록.
 *          C++ 정본은 RenderViewSettings.h 의 `RenderViewMode::ShadingPath` 설명이다.
 */
static float3 ViewMode::computeShadingPathColor()
{
#if defined( SW_PASS_GBUFFER ) && defined( SW_MATERIAL_UNLIT )
    return float3( 0.35f, 0.55f, 0.60f );
#elif defined( SW_PASS_GBUFFER )
    return float3( 0.20f, 0.45f, 0.95f );
#elif defined( MATERIAL_BLEND_TRANSLUCENT )
    return float3( 0.75f, 0.35f, 0.90f );
#elif defined( MATERIAL_FORWARD_ONLY )
    return float3( 0.95f, 0.55f, 0.15f );
#elif defined( SW_MATERIAL_UNLIT )
    return float3( 0.10f, 0.45f, 0.20f );
#else
    return float3( 0.25f, 0.80f, 0.35f );
#endif
}

/**
 * @brief 보기 모드가 바꾼 표면 색이다. Lit 에서는 부르지 않는다(SW_VIEWMODE_SKIPS_LIGHTING 이 0).
 * @details Overdraw 는 알파 0 을 낸다 — PSO 가 프리멀티플라이 블렌드(색 One / InvSrcAlpha)라 알파 0 이 더하기가 된다.
 *          Depth 는 클립 w(원근 카메라의 뷰 공간 깊이)다. 직교 카메라는 w 가 1 이라 한 색이다.
 */
#if defined( SW_VIEWMODE_ROUGHNESS )
static float4 ViewMode::computeColor( float4 shadedColor, float4 albedo, float3 worldNormal, float3 worldPosition, float roughness, float metallic )
{
    return float4( saturate( roughness ).xxx, 1.0f );
}
#elif defined( SW_VIEWMODE_METALLIC )
static float4 ViewMode::computeColor( float4 shadedColor, float4 albedo, float3 worldNormal, float3 worldPosition, float roughness, float metallic )
{
    return float4( saturate( metallic ).xxx, 1.0f );
}
#elif defined( SW_VIEWMODE_NORMALS )
static float4 ViewMode::computeColor( float4 shadedColor, float4 albedo, float3 worldNormal, float3 worldPosition, float roughness, float metallic )
{
    return float4( normalize( worldNormal ) * 0.5f + 0.5f, 1.0f );
}
#elif defined( SW_VIEWMODE_DEPTH )
static float4 ViewMode::computeColor( float4 shadedColor, float4 albedo, float3 worldNormal, float3 worldPosition, float roughness, float metallic )
{
    const float viewDepth = Transform::computeClipPosition( float4( worldPosition, 1.0f ), g_ViewProj ).w;
    return float4( saturate( viewDepth / kViewModeDepthRange ).xxx, 1.0f );
}
#elif defined( SW_VIEWMODE_OVERDRAW )
static float4 ViewMode::computeColor( float4 shadedColor, float4 albedo, float3 worldNormal, float3 worldPosition, float roughness, float metallic )
{
    return float4( kViewModeOverdrawStep, kViewModeOverdrawStep * 0.5f, kViewModeOverdrawStep * 0.15f, 0.0f );
}
#elif defined( SW_VIEWMODE_LODLEVEL )
static float4 ViewMode::computeColor( float4 shadedColor, float4 albedo, float3 worldNormal, float3 worldPosition, float roughness, float metallic )
{
    return float4( ViewMode::computeLODLevelColor( g_ViewModeLODEntry ), 1.0f );
}
#elif defined( SW_VIEWMODE_LODFADE )
static float4 ViewMode::computeColor( float4 shadedColor, float4 albedo, float3 worldNormal, float3 worldPosition, float roughness, float metallic )
{
    return float4( ViewMode::computeLODFadeColor( g_ViewModeLODEntry ), 1.0f );
}
#elif defined( SW_VIEWMODE_WIREOVERLAY )
static float4 ViewMode::computeColor( float4 shadedColor, float4 albedo, float3 worldNormal, float3 worldPosition, float roughness, float metallic )
{
    // ShadedWireframe 의 두 번째 드로우 — PSO 가 삼각형을 선으로 그리므로 단색이면 외곽선이다.
    return float4( kViewModeWireOverlayColor, 1.0f );
}
#elif defined( SW_VIEWMODE_SHADINGPATH )
static float4 ViewMode::computeColor( float4 shadedColor, float4 albedo, float3 worldNormal, float3 worldPosition, float roughness, float metallic )
{
    return float4( ViewMode::computeShadingPathColor(), 1.0f );
}
#elif defined( SW_VIEWMODE_LIGHTCOMPLEXITY )
static float4 ViewMode::computeColor( float4 shadedColor, float4 albedo, float3 worldNormal, float3 worldPosition, float roughness, float metallic )
{
    // 조명 식이 이 위치에서 도는 라이트 수 — 커서(LightCursor::begin)와 같은 그리드를 읽는다. 표면에서 세므로 디퍼드도 G버퍼가 색을 넘긴다.
    return float4( ViewMode::computeLightComplexityColor( LightGrid::computeVisitCount( worldPosition ) ), 1.0f );
}
#else
static float4 ViewMode::computeColor( float4 shadedColor, float4 albedo, float3 worldNormal, float3 worldPosition, float roughness, float metallic )
{
    // Unlit — 셰이더가 조명 없이 만든 색(알베도) 그대로.
    return float4( albedo.rgb, shadedColor.a );
}
#endif

/** @brief 보기 모드가 표면의 색을 바꾼다 — 색을 덮는 모드(SW_VIEWMODE_SKIPS_LIGHTING)는 최종 색과 알베도를 그 모드의 색으로, LightingOnly 는 알베도만 회색으로. */
#if SW_VIEWMODE_SKIPS_LIGHTING
static void ViewMode::applyToSurface( inout float4 inoutLitColor, inout float4 inoutAlbedo, float3 worldNormal, float3 worldPosition, float roughness, float metallic )
{
    inoutLitColor = ViewMode::computeColor( inoutLitColor, inoutAlbedo, worldNormal, worldPosition, roughness, metallic );
    inoutAlbedo   = float4( inoutLitColor.rgb, inoutAlbedo.a );
}
#else
static void ViewMode::applyToSurface( inout float4 inoutLitColor, inout float4 inoutAlbedo, float3 worldNormal, float3 worldPosition, float roughness, float metallic )
{
    inoutAlbedo = float4( ViewMode::overrideLightingAlbedo( inoutAlbedo.rgb ), inoutAlbedo.a );
}
#endif

// G버퍼 알베도의 알파는 **셰이딩 모델**이다 — 1 은 조명, 0 은 조명 없이 알베도 그대로(deferred_lighting 이 읽는다).
// 디퍼드 조명 패스는 머티리얼 PSO 가 아니라 뷰 모드를 모르므로, 뷰 모드를 받는 G버퍼 패스가 픽셀마다 적어 넘긴다(언리얼 G버퍼의 ShadingModelID 와 같은 자리).
// G버퍼 명세의 판이다 — C++ `GBufferLayout::kVersion`(Renderer/Pipeline/GBufferLayout.h)과 같아야 한다(단위 테스트 GBufferLayoutTest 가 맞춘다).
// 첨부 · 채널 · 인코딩(아래 셰이딩 모델 · 그려짐 · 옥타헤드럴 노멀)을 바꾸면 두 곳을 함께 올린다.
#define SW_GBUFFER_LAYOUT_VERSION 2
#define SW_GBUFFER_SHADING_LIT    1.0f
#define SW_GBUFFER_SHADING_UNLIT  0.0f
#if SW_VIEWMODE_SKIPS_LIGHTING
    #define SW_GBUFFER_SHADING SW_GBUFFER_SHADING_UNLIT
#else
    #define SW_GBUFFER_SHADING SW_GBUFFER_SHADING_LIT
#endif

/**
 * @brief G버퍼 머티리얼 칸(첨부 GBufferMaterial, R8G8B8A8_UNORM)에 적을 값 — r 금속도, g 거칠기(지각 거칠기, 제곱 전), b 는 쓰지 않는다(0), a 는 "그려졌다" 표시(1).
 * @details 읽는 쪽은 11-3 절 PassTexture::loadGbufferMaterial 하나다. 셰이딩 모델은 알베도 칸의 알파(SW_GBUFFER_SHADING)에 그대로 둔다.
 *          클리어 값 0 과 갈라지는 알파가 "이 픽셀을 G버퍼 패스가 그렸다" 를 싣는다(노멀 칸은 옥타헤드럴 두 채널이라 알파가 없다).
 */
static float4 SurfaceOutput::encodeGbufferMaterial( float roughness, float metallic )
{
    return float4( saturate( metallic ), saturate( roughness ), 0.0f, 1.0f );
}

/**
 * @brief 표면을 **패스가 원하는 모양**으로 내보낸다.
 * @param roughness · metallic 보기 모드 Roughness · Metallic 이 보이는 값이다. 기본값을 두지 않는다 — 셰이더마다 자기 값(머티리얼 칸이나 그 셰이더의 정의)을 넘긴다.
 * @details 포워드는 셰이딩한 색 하나, G버퍼는 알베도(알파 = 셰이딩 모델) · 월드 노멀 · 머티리얼(금속도 · 거칠기) 셋. 노멀 인코딩은 한 군데뿐이어야
 *          한다 — 쓰는 쪽(여기)과 읽는 쪽(deferred_lighting)이 어긋나면 조명이 조용히 틀린다.
 *          보기 모드(Unlit · Normals · Depth · Overdraw)의 색도 여기서 바꾼다. G버퍼는 그 색을 알베도 칸에 셰이딩 모델 Unlit 으로 적어
 *          디퍼드 조명 패스가 그대로 낸다 — 조명 패스는 보기 모드 변형이 없다.
 */
static SurfaceOutput SurfaceOutput::store( float4 litColor, float4 albedo, float3 worldNormal, float3 worldPosition, float roughness, float metallic )
{
    ViewMode::applyToSurface( litColor, albedo, worldNormal, worldPosition, roughness, metallic );
    return SurfaceOutput::pack( litColor, albedo, worldNormal, roughness, metallic );
}

#if defined( SW_PASS_GBUFFER )
static SurfaceOutput SurfaceOutput::pack( float4 litColor, float4 albedo, float3 worldNormal, float roughness, float metallic )
{
    SurfaceOutput output;
    output._albedo   = float4( albedo.rgb, SW_GBUFFER_SHADING );
    output._normal   = Octahedral::encode( normalize( worldNormal ) );
    output._material = SurfaceOutput::encodeGbufferMaterial( roughness, metallic );
    return output;
}
#else
static SurfaceOutput SurfaceOutput::pack( float4 litColor, float4 albedo, float3 worldNormal, float roughness, float metallic )
{
    SurfaceOutput output;
    output._color = litColor;
    return output;
}
#endif

#endif // SW_ENGINE_BINDING_OUTPUT_HLSLI
