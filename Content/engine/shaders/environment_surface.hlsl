/**
 * @file environment_surface.hlsl
 * @brief 환경 · 소품 표면의 마스터 셰이더 — 배경(벽돌 · 콘크리트 · 회벽 · 기와 · 돌담 · 젖은 아스팔트), 소품(플라스틱 · 고무 · 종이 · 밧줄 · 타일 · 대리석 · 마루 · 칠한 금속 · 녹슨 관),
 *        일상 물건(자기 찻잔 · 스테인리스 포크 · 변색한 은 · 헤어라인 금속)을 값만 바꾼 자식 머티리얼(언리얼 Material Instance)로 낸다.
 * @details 언리얼 마스터 머티리얼 + Material Layers(밑 · 무늬 · 마모 · 때 · 날씨 층을 한 그래프에 쌓고 인스턴스가 세기만 바꾼다)와 같은 구조다.
 *          층은 순서가 고정이고 세기 0 이면 꺼진다 — 셰이더 하나 · 순열 하나라 프리셋을 늘려도 PSO 가 늘지 않는다(Substrate 의 Slab 쌓기처럼 층마다 셰이더를 가르지 않는다).
 *          무늬 종류(`_patternShape.x`)는 머티리얼 값 분기다 — 같은 드로우 안에서 갈리지 않아 분기 비용이 작고, 순열로 가르면 프리셋마다 셰이더가 하나씩 는다.
 *
 *          한 점의 순서: 밑(색 · 텍스처) → 무늬(둘째 색 · 홈 범프 · 칸마다 색 흔들림) → 잔무늬 · 큰 무늬(타일 반복 깨기) → 모서리 마모(밑 층이 드러난다) → 녹 → 변색 → 때
 *          → 헤어라인 · 클리어 코트 → 날씨(젖음 · 웅덩이 · 눈 · 반짝이). 식은 함수 묶음(material_functions.hlsli)의 것이다.
 *          날씨는 칸 `_weather` 가 파라미터 묶음 engine/materials/weather.matparams.xml 의 Weather 에 묶여 지형과 함께 젖는다 — 머티리얼 값(`_wetness` 등)은 거기에 더한다.
 *
 *          W17 합치는 자리: 클리어 코트 · 이방성은 `SurfaceFunction::applyClearCoat` · `computeBrushedRoughness` 의 근사다(머리 주석 — material_functions.hlsli).
 *          W17 의 셰이딩 모델 열거 · CustomData 가 들어오면 그 두 함수만 바꾸고 이 파일의 칸(`_clearCoat` · `_clearCoatRoughness` · `_anisotropy`)은 그대로 넘긴다.
 */
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
    float4 _weather;        // 파라미터 묶음 Weather: x 젖음, y 웅덩이, z 눈, w 설선 높이(m)
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
    // 원소 크기를 16 의 배수로 — DX 의 촘촘한 간격과 GL std430 배열 간격이 같아야 한다(ShaderBindingValidator::checkStructuredElementLayouts). 머티리얼 속성이 아니다
    uint _swPad0;
    uint _swPad1;
    uint _swPad2;
}
SW_MATERIAL_END

#if defined( MATERIAL_ALPHA_CUTOFF )
    #error "environment_surface.hlsl has no alpha cutoff property — opaque surfaces only (cut-out leaves use pbr_lit.hlsl)"
#endif

#define SW_MATERIAL_DEFAULT_LIGHTING
#define SW_MATERIAL_RIM_LIGHT
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

MaterialSurface computeMaterialSurface( PixelContext context )
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

    // 헤어라인 · 클리어 코트(W17 합치는 자리 — 머리 주석).
    roughness = SurfaceFunction::computeBrushedRoughness( roughness, uv, material._anisotropy, kBrushedFrequency );
    SurfaceFunction::applyClearCoat( roughness, coat, material._clearCoatRoughness );

    // 날씨 — 머티리얼 값 + 파라미터 묶음 Weather.
    const float4 weather    = material._weather;
    const float  wetness    = saturate( material._wetness + weather.x );
    const float  puddleRate = saturate( material._puddle + weather.y );
    const float  snowRate   = saturate( material._snow + weather.z );
    const float  snowLine   = weather.z > 0.0f ? weather.w : kNoSnowLine;
    SurfaceFunction::applyWetness( albedo, roughness, material._porosity, wetness );
    const float puddle = SurfaceFunction::computePuddleMask( position, normal, puddleRate, kPuddleNoiseScale );
    SurfaceFunction::applyPuddle( albedo, roughness, metallic, surfaceNormal, puddle );
    const float snow = SurfaceFunction::computeSnowMask( position, normal, snowRate, snowLine );
    SurfaceFunction::applySnow( albedo, roughness, metallic, surfaceNormal, snow );
    SurfaceFunction::applySparkle( roughness, surfaceNormal, position, kSparkleDensity, max( material._sparkle, snow * kSnowSparkle ) );

    MaterialSurface surface;
    surface._albedo    = float4( albedo, material._baseColor.a * albedoTexture.a );
    surface._normal    = surfaceNormal;
    surface._roughness = saturate( roughness );
    surface._metallic  = saturate( metallic );
    return surface;
}

#include "material_shader_main.hlsli"
