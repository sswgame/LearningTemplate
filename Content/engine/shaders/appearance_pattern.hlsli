/**
 * appearance_pattern.hlsli — 외형 머티리얼(appearance_lit.hlsl)의 절차 무늬: 직조(면 · 데님 · 울) · 그물(망사 · 레이스) · 나뭇결(생목 · 칠) · 녹 · 비늘 · 돌 · 가죽 · 털 가닥.
 *
 * 무늬는 머티리얼 define(MATERIAL_PATTERN_*) 하나가 정의를 고른다 — 프리셋은 값만 바꾼다(같은 셰이더 · PSO). 텍스처(_albedoMap · _normalMap · _ormMap · _maskMap)는
 * 무늬와 함께 곱해지므로, 스캔 텍스처를 쓰는 아티스트는 무늬 define 없는 바탕 머티리얼에서 시작한다. 언리얼이면 머티리얼 함수(텍스처 없는 절차 무늬)의 자리다.
 * 값 뜻(무늬마다): `_patternScale` 칸 수 / UV 1(나뭇결 · 돌은 m 당), `_patternWidth` 선 · 실 반폭(칸 비), `_patternAmount` 무늬의 세기(아래 표), `_secondaryColor` 둘째 색,
 * `_layerColor` 위층 색(칠 · 녹 · 레이스 아래 피부).
 *
 * | define                     | `_patternAmount`              | 둘째 색 · 위층 색                 |
 * |----------------------------|-------------------------------|-----------------------------------|
 * | MATERIAL_PATTERN_WEAVE     | 0 평직(면) ~ 1 능직(데님)     | 씨실 색 · —                       |
 * | MATERIAL_PATTERN_NET       | 레이스 꽃무늬 고리의 세기      | — · 아래 피부(UNDERLAYER 일 때)  |
 * | MATERIAL_PATTERN_WOOD      | 칠이 덮은 정도(0 생목)        | 늦은 나이테 · 칠                  |
 * | MATERIAL_PATTERN_RUST      | 녹이 덮은 정도                | 모서리 닳음 금속 · 녹             |
 * | MATERIAL_PATTERN_SCALES    | 비늘 가장자리 어둡기          | 가장자리 색 · —                   |
 * | MATERIAL_PATTERN_STONE     | 무늬 대비                     | 어두운 결 · —                     |
 * | MATERIAL_PATTERN_LEATHER   | 주름 깊이                     | 주름 색 · —                       |
 */

#ifndef SW_ENGINE_APPEARANCEPATTERN_HLSLI
#define SW_ENGINE_APPEARANCEPATTERN_HLSLI

/** @brief 무늬의 잡음 — 해시 · 값 잡음 · fbm · 세포. 결정적이다(같은 좌표 같은 값). */
struct AppearanceNoise
{
    static float hash( float2 position );
    static float valueNoise( float2 position );
    static float computeFractal( float2 position ); // 4 옥타브 fbm, 0..1
    static float3 computeCell( float2 position );   // (가장 가까운 점까지 거리, 그 칸의 해시, 둘째로 가까운 점과의 거리 차)
};

static float AppearanceNoise::hash( float2 position )
{
    // 정수 해시(Hugo Elias 꼴) — sin 해시는 큰 좌표에서 GPU 마다 값이 갈라진다.
    uint2 cell  = (uint2)( int2( floor( position ) ) + int2( 32768, 32768 ) );
    uint  value = cell.x * 1597334677u ^ cell.y * 3812015801u;
    value       = ( value ^ ( value >> 16u ) ) * 2246822519u;
    value ^= value >> 13u;
    return (float)( value & 0x00FFFFFFu ) / 16777215.0f;
}

static float AppearanceNoise::valueNoise( float2 position )
{
    const float2 cell  = floor( position );
    const float2 local = position - cell;
    const float2 fade  = local * local * ( 3.0f - 2.0f * local );
    const float  a     = AppearanceNoise::hash( cell );
    const float  b     = AppearanceNoise::hash( cell + float2( 1.0f, 0.0f ) );
    const float  c     = AppearanceNoise::hash( cell + float2( 0.0f, 1.0f ) );
    const float  d     = AppearanceNoise::hash( cell + float2( 1.0f, 1.0f ) );
    return lerp( lerp( a, b, fade.x ), lerp( c, d, fade.x ), fade.y );
}

static float AppearanceNoise::computeFractal( float2 position )
{
    float sum       = 0.0f;
    float amplitude = 0.5f;
    SW_UNROLL for ( uint octave = 0u; octave < 4u; ++octave )
    {
        sum += AppearanceNoise::valueNoise( position ) * amplitude;
        position = position * 2.03f + float2( 17.1f, 9.3f );
        amplitude *= 0.5f;
    }
    return sum / 0.9375f;
}

static float3 AppearanceNoise::computeCell( float2 position )
{
    const float2 cell     = floor( position );
    float        nearest  = 8.0f;
    float        second   = 8.0f;
    float        cellHash = 0.0f;
    SW_UNROLL for ( int y = -1; y <= 1; ++y )
    {
        SW_UNROLL for ( int x = -1; x <= 1; ++x )
        {
            const float2 neighbor = cell + float2( (float)x, (float)y );
            const float  jitter   = AppearanceNoise::hash( neighbor );
            const float2 feature  = neighbor + float2( jitter, AppearanceNoise::hash( neighbor + float2( 41.0f, 7.0f ) ) );
            const float  gap      = length( position - feature );
            const bool   bCloser  = gap < nearest;
            second                = bCloser ? nearest : min( second, gap );
            cellHash              = bCloser ? jitter : cellHash;
            nearest               = bCloser ? gap : nearest;
        }
    }
    return float3( nearest, cellHash, second - nearest );
}

/** @brief 무늬 — define 이 apply 의 정의를 고른다(무늬가 없으면 아무것도 하지 않는다). */
struct AppearancePattern
{
    static void apply( inout AppearanceSurface surface, PixelContext context );
    static float computeNetCoverage( float2 position, float halfWidth, float roseAmount, bool bMinimumPixelWidth );
    static float computeWeaveHeight( float2 position, float twill, out float outWarp );
};

/**
 * @brief 그물 덮임 — 대각선 두 무리(마름모 망사)와 칸 가운데 고리(레이스 꽃)를 픽셀 발자국으로 거른다(ShadingMath::computeLineCoverage).
 * @param bMinimumPixelWidth 오려 내는 그물(알파 컷)은 선이 한 픽셀보다 가늘어지면 문턱 아래로 사라진다 — 반폭을 발자국 반으로 받친다(언리얼 머리카락 · 철망의 최소 폭과 같은 생각).
 *        섞는 그물(피부 위 스타킹)은 받치지 않는다 — 밀도로 수렴하는 것이 맞다.
 */
static float AppearancePattern::computeNetCoverage( float2 position, float halfWidth, float roseAmount, bool bMinimumPixelWidth )
{
    const float diagonalA  = position.x + position.y;
    const float diagonalB  = position.x - position.y;
    const float footprintA = max( fwidth( diagonalA ), 1e-4f );
    const float footprintB = max( fwidth( diagonalB ), 1e-4f );
    const float widthA     = bMinimumPixelWidth ? max( halfWidth, footprintA * 0.5f ) : halfWidth;
    const float widthB     = bMinimumPixelWidth ? max( halfWidth, footprintB * 0.5f ) : halfWidth;
    const float coverageA  = ShadingMath::computeLineCoverage( diagonalA, widthA, footprintA );
    const float coverageB  = ShadingMath::computeLineCoverage( diagonalB, widthB, footprintB );
    // 레이스 꽃: 마름모 칸(대각 좌표의 칸) 가운데의 고리.
    const float2 diamond    = float2( diagonalA, diagonalB );
    const float  ring       = length( diamond - floor( diamond ) - 0.5f ) - 0.28f;
    const float  footprintR = max( fwidth( ring ), 1e-4f );
    const float  widthR     = bMinimumPixelWidth ? max( halfWidth * 0.8f, footprintR * 0.5f ) : halfWidth * 0.8f;
    const float  coverageR  = ShadingMath::computeLineCoverage( ring, widthR, footprintR ) * saturate( roseAmount );
    const float  lines      = coverageA + coverageB - coverageA * coverageB;
    return lines + coverageR - lines * coverageR;
}

/** @brief 직조 실의 높이 — 날실(u 방향)과 씨실이 칸마다 위아래를 바꾼다. 능직은 한 줄마다 한 칸씩 밀린다(데님의 사선). */
static float AppearancePattern::computeWeaveHeight( float2 position, float twill, out float outWarp )
{
    const float2 cell     = floor( position );
    const float2 local    = position - cell;
    const float  shift    = floor( cell.y * twill + 0.5f );
    const float  period   = lerp( 2.0f, 3.0f, step( 0.5f, twill ) );
    const float  phase    = cell.x + shift;
    const bool   bWarpUp  = ( phase - period * floor( phase / period ) ) < 1.0f;
    const float  warpBump = sqrt( saturate( 1.0f - ( 2.0f * local.y - 1.0f ) * ( 2.0f * local.y - 1.0f ) ) );
    const float  weftBump = sqrt( saturate( 1.0f - ( 2.0f * local.x - 1.0f ) * ( 2.0f * local.x - 1.0f ) ) );
    outWarp               = bWarpUp ? 1.0f : 0.0f;
    return bWarpUp ? warpBump : weftBump * 0.8f;
}

#if defined( MATERIAL_PATTERN_WEAVE )
static void AppearancePattern::apply( inout AppearanceSurface surface, PixelContext context )
{
    const float2 position = context._uv * surface._patternScale;
    const float  twill    = saturate( surface._patternAmount );
    const float  epsilon  = 0.02f;
    float        warp;
    float        unused;
    const float  height  = AppearancePattern::computeWeaveHeight( position, twill, warp );
    const float  heightU = AppearancePattern::computeWeaveHeight( position + float2( epsilon, 0.0f ), twill, unused );
    const float  heightV = AppearancePattern::computeWeaveHeight( position + float2( 0.0f, epsilon ), twill, unused );
    // 실이 픽셀보다 가늘면 두 색의 평균으로 — 멀리서 무아레가 나지 않는다.
    const float detail   = saturate( 1.0f - length( fwidth( position ) ) * 1.5f );
    const float colorMix = lerp( 0.5f, warp, detail );
    surface._albedo.rgb  = lerp( surface._secondaryColor, surface._albedo.rgb, colorMix ) * lerp( 1.0f, 0.75f + 0.25f * height, detail );
    surface.perturbNormal( float2( heightU - height, heightV - height ) / epsilon * 0.08f * detail, context );
}
#elif defined( MATERIAL_PATTERN_NET )
    #if defined( MATERIAL_NET_UNDERLAYER )
/** @brief 피부 위 그물(스타킹) — 덮임으로 실(천 시엔)과 아래 피부(감싼 확산)를 섞는다. 불투명이라 정렬 · 알파가 없고, 멀리서는 밀도의 평균색이 된다. */
static void AppearancePattern::apply( inout AppearanceSurface surface, PixelContext context )
{
    const float coverage = AppearancePattern::computeNetCoverage( context._uv * surface._patternScale, surface._patternWidth, surface._patternAmount, false );
    surface._albedo.rgb  = lerp( surface._layerColor, surface._albedo.rgb, coverage );
    surface._sheenWeight *= coverage;
    surface._subsurfaceWrap *= 1.0f - coverage;
    surface._roughness = lerp( surface._layerRoughness, surface._roughness, coverage );
}
    #else
/** @brief 오려 내는 그물(레이스 · 망사 커튼) — 덮임이 알파다(alphaMode Mask). 선은 한 픽셀 아래로 가늘어지지 않는다. */
static void AppearancePattern::apply( inout AppearanceSurface surface, PixelContext context )
{
    const float coverage = AppearancePattern::computeNetCoverage( context._uv * surface._patternScale, surface._patternWidth, surface._patternAmount, true );
    surface._albedo.a *= coverage;
}
    #endif
#elif defined( MATERIAL_PATTERN_WOOD )
/** @brief 나뭇결 — 오브젝트 Y 축을 둘러싼 나이테(잡음으로 흔든다) · 결 줄무늬, 칠(위층 색)이 잡음 문턱으로 벗겨진다. */
static void AppearancePattern::apply( inout AppearanceSurface surface, PixelContext context )
{
    const float3 local    = context._user._localPosition * surface._patternScale;
    const float  wobble   = AppearanceNoise::computeFractal( local.xy * 1.7f + local.zz * 0.3f ) * 0.6f;
    const float  ring     = frac( length( local.xz ) * 4.0f + wobble );
    const float  latewood = smoothstep( 0.65f, 0.95f, ring );
    const float  streak   = AppearanceNoise::valueNoise( float2( local.y * 6.0f, atan2( local.z, local.x ) * 24.0f ) );
    surface._albedo.rgb   = lerp( surface._albedo.rgb, surface._secondaryColor, saturate( latewood * 0.8f + streak * 0.25f ) );
    surface._roughness    = saturate( surface._roughness + latewood * 0.1f );
    // 칠: 잡음 + 양이 문턱을 넘는 곳. 칠은 매끈하고 니스(코트)를 받는다.
    const float paint   = smoothstep( 0.45f, 0.55f, AppearanceNoise::computeFractal( context._uv * 9.0f ) + surface._patternAmount - 0.5f ) * step( 0.001f, surface._patternAmount );
    surface._albedo.rgb = lerp( surface._albedo.rgb, surface._layerColor, paint );
    surface._roughness  = lerp( surface._roughness, surface._layerRoughness, paint );
    surface._coatWeight = lerp( surface._coatWeight * 0.3f, surface._coatWeight, paint );
}
#elif defined( MATERIAL_PATTERN_RUST )
/** @brief 녹 — 잡음 덩어리가 금속을 덮고(금속도 0 · 거칠다), 모서리(곡률이 큰 곳)는 닳아 밝은 금속이 드러난다. */
static void AppearancePattern::apply( inout AppearanceSurface surface, PixelContext context )
{
    const float  blotch    = AppearanceNoise::computeFractal( context._uv * surface._patternScale );
    const float  curvature = saturate( length( fwidth( context._normal ) ) / max( length( fwidth( context._worldPosition ) ), 1e-5f ) * 0.05f );
    const float  rust      = smoothstep( 0.55f, 0.65f, blotch + surface._patternAmount * 0.6f - 0.3f ) * ( 1.0f - curvature );
    const float  wear      = saturate( curvature * 2.0f );
    const float3 rustTone  = surface._layerColor * lerp( 0.6f, 1.2f, AppearanceNoise::valueNoise( context._uv * surface._patternScale * 6.0f ) );
    surface._albedo.rgb    = lerp( lerp( surface._albedo.rgb, surface._secondaryColor, wear ), rustTone, rust );
    surface._metallic      = lerp( surface._metallic, 0.0f, rust );
    surface._roughness     = lerp( lerp( surface._roughness, surface._roughness * 0.6f, wear ), surface._layerRoughness, rust );
}
#elif defined( MATERIAL_PATTERN_SCALES )
/** @brief 비늘 — 엇갈린 줄의 원판. 비늘마다 아래 가장자리로 기울고 가장자리가 어둡다. */
static void AppearancePattern::apply( inout AppearanceSurface surface, PixelContext context )
{
    float2      position = context._uv * surface._patternScale;
    const float row      = floor( position.y );
    position.x += frac( row * 0.5f ) * 1.0f;
    const float2 local  = frac( position ) - float2( 0.5f, 0.15f );
    const float  radial = saturate( length( local ) / 0.75f );
    const float  edge   = smoothstep( 0.6f, 1.0f, radial ) * saturate( surface._patternAmount );
    surface._albedo.rgb = lerp( surface._albedo.rgb, surface._secondaryColor, edge ) * lerp( 0.9f, 1.1f, AppearanceNoise::hash( floor( position ) ) );
    surface.perturbNormal( local * 0.6f * saturate( 1.0f - length( fwidth( position ) ) ), context );
}
#elif defined( MATERIAL_PATTERN_STONE )
/** @brief 돌 — fbm 의 얼룩과 결, 잡음의 기울기로 거친 면. */
static void AppearancePattern::apply( inout AppearanceSurface surface, PixelContext context )
{
    const float2 position = context._uv * surface._patternScale;
    const float  height   = AppearanceNoise::computeFractal( position );
    const float  heightU  = AppearanceNoise::computeFractal( position + float2( 0.05f, 0.0f ) );
    const float  heightV  = AppearanceNoise::computeFractal( position + float2( 0.0f, 0.05f ) );
    const float  vein     = 1.0f - smoothstep( 0.0f, 0.04f, abs( AppearanceNoise::computeFractal( position * 0.5f + 3.0f ) - 0.5f ) );
    surface._albedo.rgb   = lerp( surface._albedo.rgb, surface._secondaryColor, saturate( ( height - 0.5f ) * 2.0f * surface._patternAmount + vein * 0.6f ) );
    surface.perturbNormal( float2( heightU - height, heightV - height ) / 0.05f * 0.15f, context );
}
#elif defined( MATERIAL_PATTERN_LEATHER )
/** @brief 가죽 — 세포 잡음의 오톨도톨한 결(칸 가운데가 솟는다)과 칸 경계의 주름. */
static void AppearancePattern::apply( inout AppearanceSurface surface, PixelContext context )
{
    const float2 position = context._uv * surface._patternScale;
    const float3 cell     = AppearanceNoise::computeCell( position );
    const float3 cellU    = AppearanceNoise::computeCell( position + float2( 0.03f, 0.0f ) );
    const float3 cellV    = AppearanceNoise::computeCell( position + float2( 0.0f, 0.03f ) );
    const float  crease   = 1.0f - smoothstep( 0.0f, 0.08f, cell.z );
    surface._albedo.rgb   = lerp( surface._albedo.rgb * lerp( 0.9f, 1.05f, cell.y ), surface._secondaryColor, crease * saturate( surface._patternAmount ) );
    surface._roughness    = saturate( surface._roughness + crease * 0.15f );
    surface.perturbNormal( float2( cellU.z - cell.z, cellV.z - cell.z ) / 0.03f * 0.1f * saturate( surface._patternAmount ), context );
}
#else
static void AppearancePattern::apply( inout AppearanceSurface surface, PixelContext context )
{
}
#endif

#endif // SW_ENGINE_APPEARANCEPATTERN_HLSLI
