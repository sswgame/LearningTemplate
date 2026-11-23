/**
 * @file environment_surface.hlsl
 * @brief 환경 · 소품 표면의 마스터 셰이더 — 배경(벽돌 · 콘크리트 · 회벽 · 기와 · 돌담 · 젖은 아스팔트), 소품(플라스틱 · 고무 · 종이 · 밧줄 · 타일 · 대리석 · 마루 · 칠한 금속 · 녹슨 관),
 *        일상 물건(자기 찻잔 · 스테인리스 포크 · 변색한 은 · 헤어라인 금속)을 값만 바꾼 자식 머티리얼(언리얼 Material Instance)로 낸다.
 * @details 언리얼 마스터 머티리얼 + Material Layers(밑 · 무늬 · 마모 · 때 · 날씨 층을 한 그래프에 쌓고 인스턴스가 세기만 바꾼다)와 같은 구조다.
 *          층은 순서가 고정이고 세기 0 이면 꺼진다 — 셰이더 하나 · 순열 하나라 프리셋을 늘려도 PSO 가 늘지 않는다(Substrate 의 Slab 쌓기처럼 층마다 셰이더를 가르지 않는다).
 *          무늬 종류(`_patternShape.x`)는 머티리얼 값 분기다 — 같은 드로우 안에서 갈리지 않아 분기 비용이 작고, 순열로 가르면 프리셋마다 셰이더가 하나씩 는다.
 *
 *          한 점의 순서: 밑(색 · 텍스처) → 무늬(둘째 색 · 홈 범프 · 칸마다 색 흔들림) → 잔무늬 · 큰 무늬(타일 반복 깨기) → 모서리 마모(밑 층이 드러난다) → 녹 → 변색 → 때
 *          → 헤어라인 결 → 날씨(젖음 · 웅덩이 · 눈 · 반짝이). 식은 함수 묶음(material_functions.hlsli)의 것이다.
 *          날씨는 전역 값 g_MaterialGlobalWeather(파라미터 묶음 engine/materials/weather.matparams.xml 의 Weather)라 지형과 함께 젖는다 — 머티리얼 값(`_wetness` 등)은 거기에 더한다.
 *          웅덩이에는 g_MaterialGlobalRain.x(같은 묶음의 비 세기)만큼 빗방울 링이 돈다(`SurfaceFunction::applyRainRipples`).
 *
 *          셰이딩 모델(언리얼 머티리얼의 Shading Model 칸 — 인스턴스가 아니라 머티리얼이 고른다):
 *            define 없음                → Default Lit(기본 PBR, 기본 조명 훅).
 *            MATERIAL_LOBE_COAT         → Clear Coat — 둘째 반사층(코트 무게 `_clearCoat` × (1 - 모서리 마모), 코트 거칠기 `_clearCoatRoughness`). 유약 · 니스 · 칠.
 *            MATERIAL_LOBE_ANISOTROPY   → Anisotropic — 탄젠트(메시 UV u) 방향 비등방 GGX(`_anisotropy`). 헤어라인 금속.
 *          둘 다 켠 머티리얼은 G버퍼 사용자 칸(번호 하나에 엽 하나 — shading_model_ids.hlsli)에 들지 않아 MATERIAL_FORWARD_ONLY 가 아니면 쿠킹이 멈춘다.
 *          값과 define 의 짝(코트 · 비등방 값이 0 이 아니면 그 define)은 MaterialLibraryTest 가 데이터로 본다 — 셰이더는 값을 보지 못한다.
 */
#if defined( MATERIAL_LOBE_COAT ) || defined( MATERIAL_LOBE_ANISOTROPY )
    #define SW_SHADING_MODEL_LAYERED
    #define SW_ENVIRONMENT_LAYERED 1
#else
    #define SW_ENVIRONMENT_LAYERED 0
#endif
#include "lighting.hlsli"

// 무늬 종류(`_patternShape.x`) — environmentsurface.material 의 표와 같다.
static const float kPatternNone    = 0.0f;
static const float kPatternBrick   = 1.0f;
static const float kPatternPlank   = 2.0f;
static const float kPatternVeins   = 3.0f;
static const float kPatternCells   = 4.0f;
static const float kPatternStripes = 5.0f;
// 무늬 해시 씨앗(머티리얼마다 다를 필요가 없다 — 칸 번호가 이미 자리마다 다르다).
static const uint kPatternSeed = 7u;
// 헤어라인 줄의 주파수(UV 단위당) · 반짝이 칸 밀도(칸/m) · 날씨 눈의 반짝이 비율 · 웅덩이 노이즈 크기(m).
static const float kBrushedFrequency = 400.0f;
static const float kSparkleDensity   = 60.0f;
static const float kSnowSparkle      = 0.05f;
static const float kPuddleNoiseScale = 3.0f;
// 날씨 눈이 없을 때의 설선 — 머티리얼 값 눈은 높이와 무관하게 쌓인다.
static const float kNoSnowLine = -100000.0f;

// environmentsurface.material 의 프로퍼티와 이름 · 타입이 같다(머티리얼 패커는 리플렉션 오프셋으로 채운다).
SW_MATERIAL_BEGIN
{
    float4 _baseColor;      // 밑 색(알베도 맵에 곱한다)
    float4 _secondaryColor; // 무늬 둘째 색(줄눈 · 결 · 균열 · 판자 결 · 줄)
    float4 _underColor;     // 모서리 마모가 드러내는 밑 층 색(칠 밑 금속 · 유약 밑 소지)
    float4 _patternScale;   // x · y = 칸 크기(좌표 단위), z = 홈 폭, w = 줄 어긋남(벽돌 0.5) 또는 줄 기울기
    float4 _patternShape;   // x = 무늬 종류(0 없음 · 1 벽돌/타일 · 2 판자 · 3 결 · 4 칸 · 5 줄), y = 칸마다 색 흔들림, z = 홈 깊이(m), w = 좌표(0 메시 UV · 1 월드 축)
    float  _roughness;
    float  _metallic;
    float  _secondaryRoughness;
    float  _secondaryMetallic;
    float  _underRoughness;
    float  _underMetallic;
    float  _noiseStrength; // 잔무늬 세기(색 · 거칠기 · 범프)
    float  _noiseScale;    // 잔무늬 크기(m)
    float  _macroVariation;
    float  _macroScale; // 큰 무늬 크기(m)
    float  _edgeWear;
    float  _edgeWearScale; // 칩 노이즈 크기(m)
    float  _dirt;
    float  _rust;
    float  _tarnish;
    float  _wearScale; // 때 · 녹 · 변색 노이즈 크기(m)
    float  _clearCoat;
    float  _clearCoatRoughness;
    float  _anisotropy;
    float  _porosity; // 젖으면 어두워지는 정도(벽돌 · 흙 1, 금속 · 유약 0)
    float  _wetness;
    float  _puddle;
    float  _snow;
    float  _sparkle;
    float  _textureTiling; // 텍스처 반복(좌표 단위당)
    float  _normalScale;
    uint   _albedoMap;
    uint   _normalMap;
    uint   _ormMap;
    // 원소 크기를 16 의 배수로 — DX 의 촘촘한 간격과 GL std430 배열 간격이 같아야 한다(ShaderBindingValidator::validateStructuredElementLayouts). 머티리얼 속성이 아니다
    uint _swPad0;
    uint _swPad1;
    uint _swPad2;
}
SW_MATERIAL_END

#if defined( MATERIAL_ALPHA_CUTOFF )
    #error "environment_surface.hlsl has no alpha cutoff property — opaque surfaces only (cut-out leaves use pbr_lit.hlsl)"
#endif
#if defined( MATERIAL_LOBE_SHEEN ) || defined( MATERIAL_LOBE_SUBSURFACE ) || defined( MATERIAL_LOBE_TRANSMISSION ) || defined( MATERIAL_LOBE_THIN_FILM ) || defined( MATERIAL_LOBE_STRAND )
    #error "environment_surface.hlsl shades Default Lit, Clear Coat (MATERIAL_LOBE_COAT) or Anisotropic (MATERIAL_LOBE_ANISOTROPY) only — other lobes use appearance_lit.hlsl"
#endif
// 코트 + 비등방은 G버퍼 번호 하나에 들지 않는다 — define 을 빠뜨린 머티리얼이 G버퍼에서 조용히 한 엽을 잃지 않게 쿠킹에서 멈춘다.
#if defined( SW_PASS_GBUFFER ) && defined( MATERIAL_LOBE_COAT ) && defined( MATERIAL_LOBE_ANISOTROPY )
    #error "environment_surface.hlsl: Clear Coat with Anisotropy does not fit the G-buffer (one shading model per pixel): list MATERIAL_FORWARD_ONLY"
#endif

#if defined( MATERIAL_LOBE_ANISOTROPY )
    #define SW_ENVIRONMENT_SHADING_MODEL SW_SHADING_MODEL_ID_ANISOTROPIC
#elif defined( MATERIAL_LOBE_COAT )
    #define SW_ENVIRONMENT_SHADING_MODEL SW_SHADING_MODEL_ID_CLEAR_COAT
#endif

#if SW_ENVIRONMENT_LAYERED
    #define SW_MATERIAL_CUSTOM_LIGHTING
    #define SW_MATERIAL_CUSTOM_SHADING
#else
    #define SW_MATERIAL_DEFAULT_LIGHTING
    #define SW_MATERIAL_RIM_LIGHT
#endif
#define SW_MATERIAL_DEFAULT_VARYINGS
#if !defined( SW_PASS_SHADOW ) && !defined( SW_PASS_DEPTH_PREPASS )
    #define SW_MATERIAL_USES_TANGENT
#endif
#include "material_shader.hlsli"
#include "normal_map.hlsli"
#include "material_functions.hlsli"

/** @brief 무늬 한 점의 결과 — 둘째 색 비율, 범프 높이(1 = 칸 위, 0 = 홈 바닥), 칸마다의 밝기 흔들림(0.5 중립). */
struct EnvironmentPattern
{
    float _secondary;
    float _height;
    float _cellTint;
};

/** @brief 칸 번호 @p cell 의 밝기 흔들림(0..1) — 타일 변형 해시(C++ MaterialProjection 과 같은 식)의 밝기. */
float computeCellTint( float2 cell )
{
    return SurfaceFunction::computeTileVariation( int2( cell ), kPatternSeed )._brightness;
}

/** @brief 무늬 종류 @p kind 의 한 점. 좌표 @p uv 는 메시 UV 또는 월드 축 투영이고, 결 · 판자 결은 3 차원 노이즈라 @p position 도 받는다. */
EnvironmentPattern evaluatePattern( float kind, float4 scale, float2 uv, float3 position )
{
    EnvironmentPattern result;
    result._secondary = 0.0f;
    result._height    = 1.0f;
    result._cellTint  = 0.5f;
    if ( abs( kind - kPatternBrick ) < 0.5f )
    {
        const MaterialPattern brick = SurfaceFunction::computeBrickPattern( uv, scale.xy, scale.z, scale.w );
        result._secondary           = brick._groove;
        result._height              = 1.0f - brick._groove;
        result._cellTint            = computeCellTint( brick._cell );
    }
    else if ( abs( kind - kPatternPlank ) < 0.5f )
    {
        const MaterialPattern plank     = SurfaceFunction::computePlankPattern( uv, scale.xy, scale.z, kPatternSeed );
        const float           cellNoise = computeCellTint( plank._cell );
        // 판자 결 — 판자마다 다른 자리에서 시작하는 길쭉한 노이즈 줄(나이테가 판자 길이를 따라 흐른다).
        const float3 grainPosition = float3( uv.x * 1.5f / scale.x, uv.y * 24.0f / scale.y, cellNoise * 37.0f );
        const float  grain         = 0.5f + 0.5f * sin( ( uv.y / scale.y * 9.0f + SurfaceFunction::computeFbm( grainPosition, 3u ) * 5.0f ) * 6.28318f );
        result._secondary          = max( plank._groove, grain * 0.55f );
        result._height             = 1.0f - plank._groove;
        result._cellTint           = cellNoise;
    }
    else if ( abs( kind - kPatternVeins ) < 0.5f )
    {
        result._secondary = SurfaceFunction::computeVeins( position, scale.x, scale.y );
    }
    else if ( abs( kind - kPatternCells ) < 0.5f )
    {
        const MaterialPattern cells = SurfaceFunction::computeCellPattern( uv, scale.x, scale.z, kPatternSeed );
        result._secondary           = cells._groove;
        result._height              = 1.0f - cells._groove;
        result._cellTint            = computeCellTint( cells._cell );
    }
    else if ( abs( kind - kPatternStripes ) < 0.5f )
    {
        const float stripe = SurfaceFunction::computeStripes( uv, 1.0f / max( scale.x, 1.0e-4f ), scale.w );
        result._secondary  = smoothstep( 1.0f - scale.z, 1.0f, stripe );
        result._height     = 1.0f - result._secondary;
    }
    return result;
}

/** @brief 환경 표면 한 점 — 표면 훅의 값과 셰이딩 모델 엽의 값(코트 · 비등방). 표면 · 조명 · 셰이딩 훅이 같은 함수를 부른다(같은 식이라 컴파일러가 한 번만 계산한다). */
struct EnvironmentSurface
{
    MaterialSurface _surface;
    float           _coatWeight;    ///< 코트 무게(모서리 마모 자리는 0)
    float           _coatRoughness; ///< 코트 거칠기
    float           _anisotropy;    ///< 비등방(0..1 — 탄젠트 방향으로 늘어난다)
    float3          _tangent;       ///< 비등방 방향(메시 탄젠트 — 메시 UV u)

    static EnvironmentSurface make( PixelContext context );
    static float3 getTangent( PixelContext context );
};

#if defined( SW_MATERIAL_USES_TANGENT )
static float3 EnvironmentSurface::getTangent( PixelContext context )
{
    return context._tangent.xyz;
}
#else
static float3 EnvironmentSurface::getTangent( PixelContext context )
{
    return float3( 1.0f, 0.0f, 0.0f ); // 깊이 · 그림자 패스 — 조명을 하지 않는다
}
#endif

MaterialSurface computeMaterialSurface( PixelContext context )
{
    return EnvironmentSurface::make( context )._surface;
}

/**
 * @brief 층을 순서대로 쌓은 한 점.
 * @details shortcut: 월드 축 투영(`_patternShape.w`)이어도 비등방 방향은 메시 탄젠트다 — 헤어라인 결 무늬는 투영 UV 를 따라 둘이 어긋난다. 월드 투영 헤어라인 프리셋이 생기면
 *          pbr_lit.hlsl 의 computeWorldAlignedUV 처럼 투영의 dP/du 를 탄젠트로 낸다.
 */
static EnvironmentSurface EnvironmentSurface::make( PixelContext context )
{
    MaterialData material = context._material;
    const float3 position = context._worldPosition;
    const float3 normal   = context._normal;
    const float2 uv       = ( material._patternShape.w > 0.5f ) ? SurfaceFunction::projectDominantAxis( position, normal ) : context._uv;

    // 밑 — 색 × 텍스처(없으면 흰색이라 값 그대로). ORM 은 glTF 배치(R 가림 · G 거칠기 · B 금속도).
    const float2 textureUv     = uv * material._textureTiling;
    const float4 albedoTexture = MaterialTexture::sampleIndex( material._albedoMap, textureUv );
    const float3 orm           = MaterialTexture::sampleIndex( material._ormMap, textureUv ).rgb;
    float3       albedo        = context._color.rgb * material._baseColor.rgb * albedoTexture.rgb * orm.r;
    float        roughness     = material._roughness * orm.g;
    float        metallic      = material._metallic * orm.b;
#if defined( SW_MATERIAL_USES_TANGENT )
    float3 surfaceNormal = applyNormalMap( material._normalMap, material._normalScale, textureUv, normal, context._tangent );
#else
    float3 surfaceNormal = normal;
#endif

    // 무늬 — 둘째 색 · 홈 범프 · 칸마다 색 흔들림.
    const EnvironmentPattern pattern = evaluatePattern( material._patternShape.x, material._patternScale, uv, position );
    albedo                           = lerp( albedo, material._secondaryColor.rgb, pattern._secondary );
    roughness                        = lerp( roughness, material._secondaryRoughness, pattern._secondary );
    metallic                         = lerp( metallic, material._secondaryMetallic, pattern._secondary );
    albedo *= 1.0f + ( pattern._cellTint - 0.5f ) * 2.0f * material._patternShape.y;

    // 잔무늬 · 큰 무늬 — 3 차원 노이즈라 UV 이음매가 없다. 큰 무늬는 타일 반복이 보이는 거리의 밝기 · 거칠기를 흔든다.
    const float detail = SurfaceFunction::computeFbm( position / max( material._noiseScale, 1.0e-4f ), 4u );
    albedo *= lerp( 1.0f, 0.7f + detail * 0.6f, material._noiseStrength );
    roughness += ( detail - 0.5f ) * 0.3f * material._noiseStrength;
    const float macro = SurfaceFunction::computeMacroVariation( position, material._macroScale );
    albedo *= lerp( 1.0f, 0.75f + macro * 0.5f, material._macroVariation );
    roughness += ( macro - 0.5f ) * 0.2f * material._macroVariation;
    const float height = pattern._height + detail * material._noiseStrength * 0.25f;
    surfaceNormal      = SurfaceFunction::perturbNormalByHeight( surfaceNormal, position, height, material._patternShape.z );

    // 모서리 마모 — 밑 층(칠 밑 금속 · 유약 밑 소지)이 드러나고 그 자리엔 코트가 없다.
    const float wear = SurfaceFunction::computeEdgeWearMask( position, normal, material._edgeWear, material._edgeWearScale );
    albedo           = lerp( albedo, material._underColor.rgb, wear );
    roughness        = lerp( roughness, material._underRoughness, wear );
    metallic         = lerp( metallic, material._underMetallic, wear );
    const float coat = material._clearCoat * ( 1.0f - wear );

    // 녹 — 거친 비금속, 노이즈로 밝은 녹과 짙은 녹이 섞인다.
    const float rust      = SurfaceFunction::computeRustMask( position, normal, material._rust, material._wearScale );
    const float rustShade = SurfaceFunction::computeValueNoise( position / max( material._wearScale * 0.3f, 1.0e-4f ) );
    albedo                = lerp( albedo, lerp( kRustDarkColor, kRustColor, rustShade ), rust );
    roughness             = lerp( roughness, kRustRoughness, rust );
    metallic              = lerp( metallic, 0.0f, rust );

    // 변색 — 은의 황화막. 닦이는 볼록한 곳은 밝게 남는다.
    const float tarnish = SurfaceFunction::computeTarnishMask( position, normal, material._tarnish, material._wearScale );
    albedo              = lerp( albedo, albedo * kTarnishColor * 2.0f, tarnish );
    roughness           = lerp( roughness, kTarnishRoughness, tarnish );
    metallic            = lerp( metallic, metallic * 0.4f, tarnish );

    // 때 · 먼지.
    const float dirt = SurfaceFunction::computeDirtMask( position, normal, material._dirt, material._wearScale );
    albedo           = lerp( albedo, kDirtColor, dirt );
    roughness        = lerp( roughness, kDirtRoughness, dirt );
    metallic         = lerp( metallic, 0.0f, dirt );

    // 헤어라인 결 무늬(거칠기의 줄). 늘어난 하이라이트는 Anisotropic 모델의 엽이, 코트 반사는 Clear Coat 모델의 둘째 층이 낸다(머리 주석).
    roughness = SurfaceFunction::computeBrushedRoughness( roughness, uv, material._anisotropy, kBrushedFrequency );

    // 날씨 — 머티리얼 값 + 전역 값.
    const float4 weather    = g_MaterialGlobalWeather;
    const float  wetness    = saturate( material._wetness + weather.x );
    const float  puddleRate = saturate( material._puddle + weather.y );
    const float  snowRate   = saturate( material._snow + weather.z );
    const float  snowLine   = weather.z > 0.0f ? weather.w : kNoSnowLine;
    SurfaceFunction::applyWetness( albedo, roughness, material._porosity, wetness );
    const float puddle = SurfaceFunction::computePuddleMask( position, normal, puddleRate, kPuddleNoiseScale );
    SurfaceFunction::applyPuddle( albedo, roughness, metallic, surfaceNormal, puddle );
    SurfaceFunction::applyRainRipples( surfaceNormal, position.xz, puddle, g_SwMaterialTime, g_MaterialGlobalRain.x );
    const float snow = SurfaceFunction::computeSnowMask( position, normal, snowRate, snowLine );
    SurfaceFunction::applySnow( albedo, roughness, metallic, surfaceNormal, snow );
    SurfaceFunction::applySparkle( roughness, surfaceNormal, position, kSparkleDensity, max( material._sparkle, snow * kSnowSparkle ) );

    EnvironmentSurface result;
    result._surface._albedo    = float4( albedo, material._baseColor.a * albedoTexture.a );
    result._surface._normal    = surfaceNormal;
    result._surface._roughness = saturate( roughness );
    result._surface._metallic  = saturate( metallic );
    result._coatWeight         = saturate( coat );
    result._coatRoughness      = saturate( material._clearCoatRoughness );
    result._anisotropy         = saturate( material._anisotropy );
    result._tangent            = EnvironmentSurface::getTangent( context );
    return result;
}

#if SW_ENVIRONMENT_LAYERED
    #if SW_VIEWMODE_SKIPS_LIGHTING
float4 computeMaterialLighting( PixelContext context, MaterialSurface surface )
{
    return surface._albedo; // 보기 모드 Unlit — 조명 · 그림자 없이 표면 색
}
    #else
/** @brief 코트 · 비등방 엽을 켠 겹 BRDF 로 모든 빛 — 디퍼드(deferred_lighting.hlsl)와 같은 빛 고리 · 같은 림 항이다. */
float4 computeMaterialLighting( PixelContext context, MaterialSurface surface )
{
    const EnvironmentSurface environment = EnvironmentSurface::make( context );
    SurfaceBRDF              brdf        = SurfaceBRDF::makePBR( ViewMode::overrideLightingAlbedo( surface._albedo.rgb ), context._worldPosition, surface._normal, surface._roughness, surface._metallic );
    brdf.setCoat( environment._coatWeight, environment._coatRoughness );
    brdf.setAnisotropy( environment._tangent, environment._anisotropy );
    const float shadow = Shadow::sampleAtWorld( context._worldPosition, surface._normal );
    float3      lit    = brdf.shadeLights( context._worldPosition, shadow ) + Lighting::computeRimLight( surface._normal );
    context.addEmissive( lit );
    return float4( lit, surface._albedo.a );
}
    #endif

/** @brief G버퍼 셰이딩 값 — 번호와 사용자 칸(shading_model_ids.hlsli 표: Clear Coat = 코트 무게 · 거칠기, Anisotropic = 탄젠트 옥타 · 비등방). */
SurfaceShading computeMaterialShading( PixelContext context, MaterialSurface surface )
{
    const EnvironmentSurface environment = EnvironmentSurface::make( context );
    SurfaceShading           shading     = SurfaceShading::make( SW_ENVIRONMENT_SHADING_MODEL );
    #if defined( MATERIAL_LOBE_ANISOTROPY )
    shading._customData = float4( Octahedral::encode( environment._tangent ) * 0.5f + 0.5f, environment._anisotropy * 0.5f + 0.5f, 0.0f );
    #else
    shading._customData = float4( environment._coatWeight, environment._coatRoughness, 0.0f, 0.0f );
    #endif
    return shading;
}
#endif

#include "material_shader_main.hlsli"
