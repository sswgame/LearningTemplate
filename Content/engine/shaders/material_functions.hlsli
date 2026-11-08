/**
 * material_functions.hlsli — 머티리얼 함수 묶음(언리얼 Material Function 라이브러리 · 유니티 Shader Graph 서브그래프 자리). 표면 훅(`computeMaterialSurface`)이 부르는 순수 함수다.
 *
 * 쓰는 곳: terrain.hlsl(3 축 · 높이 섞기 · 규칙 · 젖음 · 눈), environment_surface.hlsl(무늬 · 마모 · 녹 · 때 · 변색 · 날씨), gimmick_surface.hlsl(프레넬 · 녹아 사라짐 · 홀로그램 · 흐름),
 * surface_lit.hlsl(몸의 젖음 · 흙 — 같은 값을 따로 두지 않는다).
 * 머티리얼 셰이더 뼈대의 `material_shader.hlsli` 뒤에 include 한다(MaterialTexture · View · 픽셀 미분을 쓴다). 상태가 없고 텍스처 슬롯을 새로 두지 않는다 — 텍스처는 머티리얼 번호로만 읽는다.
 *
 * CPU 짝: 3 축 가중치 · 타일 변형 해시는 `MaterialProjection`(C++), 높이 섞기는 `SurfaceLayerWeights::computeHeightBlended`, 규칙 띠는 `TerrainLayerRule::computeBand`.
 * 같은 식이다 — 바꾸면 둘을 함께 바꾸고 단위 테스트(`MaterialProjectionTest` · `TerrainLayerRuleTest`)의 값을 다시 계산한다.
 *
 * 젖음 · 웅덩이 · 눈의 세기는 머티리얼 파라미터 묶음 `engine/materials/weather.matparams.xml` 의 `Weather`(x 젖음, y 웅덩이, z 눈, w 설선 높이 m)에
 * 머티리얼 칸 `_weather` 를 묶어 받는다 — 날씨가 값을 바꾸면 묶은 모든 머티리얼이 함께 바뀐다(언리얼 MPC 로 날씨를 돌리는 방식).
 *
 * W17(셰이딩 모델 · G버퍼 v3) 합치는 자리: 클리어 코트와 이방성은 지금 기본 PBR 위의 근사(`applyClearCoat` · `computeBrushedRoughness`)다.
 * 셰이딩 모델 열거와 CustomData 가 들어오면 이 두 함수의 몸만 "모델 = ClearCoat/Anisotropic, CustomData = (코트, 코트 거칠기, 이방성, 방향)" 으로 바꾼다 — 부르는 쪽은 그대로다.
 */

#ifndef SW_ENGINE_MATERIALFUNCTIONS_HLSLI
#define SW_ENGINE_MATERIALFUNCTIONS_HLSLI

// 타일 해시 곱수 · 소금 — C++ MaterialProjection 의 kTileHashX · kTileHashY · kTileHashSalt 와 같은 값이다.
static const uint kTileHashX    = 0x8DA6B343u;
static const uint kTileHashY    = 0xD8163841u;
static const uint kTileHashZ    = 0xCB1AB31Fu;
static const uint kTileHashSalt = 0x68E31DA4u;
// 해시 상위 24 비트를 [0, 1) 로(하위 비트는 품질이 낮다).
static const float kHashToUnit = 1.0f / 16777216.0f;
// 3 축 축 성분의 아래 한계(pow( 0, s ) 의 log( 0 ) 을 피한다) · 날카로움의 아래 한계 — C++ 와 같다.
static const float kMinTriplanarAxis      = 1.0e-5f;
static const float kMinTriplanarSharpness = 1.0f;
// 규칙 띠 페이드 폭의 아래 한계 — C++ TerrainLayerRule::kMinFade 와 같다.
static const float kMinRuleFade = 1.0e-4f;
// 젖음: 다 젖었을 때 알베도 배율(다공성 1 기준)과 거칠기(Lagarde 의 젖은 표면 관찰). 몸(surface_lit.hlsl)과 땅 · 벽이 이 한 벌을 쓴다.
static const float kWetAlbedoScale = 0.55f;
static const float kWetRoughness   = 0.15f;
// 웅덩이 물막: 거칠기 · 알베도 배율 — 고인 물은 거의 거울이고 밑바닥이 어둡다.
static const float kPuddleRoughness   = 0.03f;
static const float kPuddleAlbedoScale = 0.35f;
// 눈: 색(약간 푸른 흰색) · 거칠기 · 반짝이 칸의 거칠기.
static const float3 kSnowColor        = float3( 0.86f, 0.90f, 0.96f );
static const float  kSnowRoughness    = 0.65f;
static const float  kSparkleRoughness = 0.04f;
static const float  kSparkleTiltScale = 0.6f;
// 녹 · 때 · 변색 색(선형). 녹은 거친 비금속, 은 변색은 갈흑색 황화은 막이다.
static const float3 kRustColor     = float3( 0.32f, 0.12f, 0.04f );
static const float3 kRustDarkColor = float3( 0.12f, 0.05f, 0.02f );
static const float  kRustRoughness = 0.85f;
// 흙 · 때 — 벽의 때(computeDirtMask)와 몸의 흙(surface_lit.hlsl)이 같은 색이다.
static const float3 kDirtColor        = float3( 0.16f, 0.12f, 0.08f );
static const float  kDirtRoughness    = 0.95f;
static const float3 kTarnishColor     = float3( 0.18f, 0.13f, 0.09f );
static const float  kTarnishRoughness = 0.55f;
// 클리어 코트 근사: 코트가 밑 거칠기를 덮는 비율(나머지는 밑 무늬가 비친다).
static const float kClearCoatCover = 0.8f;
// 미분 · 정규화의 0 나누기 방지.
static const float kDerivativeEpsilon = 1.0e-5f;

/** @brief 타일 하나의 변형 — C++ `MaterialProjection::TileVariation` 과 같은 칸. */
struct MaterialTileVariation
{
    float2 _offset;     ///< UV 오프셋(0..1)
    uint   _rotation;   ///< 90 도 회전 횟수(0..3)
    float  _brightness; ///< 밝기 흔들림(0..1, 0.5 가 중립)
};

/** @brief 무늬 한 점 — 홈(줄눈 · 틈) 마스크와 칸 번호, 칸 가장자리까지의 거리. */
struct MaterialPattern
{
    float  _groove;       ///< 1 = 홈(줄눈 · 틈 · 판자 이음), 0 = 칸 안
    float2 _cell;         ///< 칸 번호(칸마다의 색 흔들림 해시)
    float  _edgeDistance; ///< 칸 가장자리까지의 거리(무늬 단위) — 홈의 둥근 어깨(높이)
};

/** @brief 머티리얼 함수 묶음(전부 static). 선언은 여기, 정의는 아래(FXC 는 이름공간이 없다 — NamingScripts HLSL 절). */
struct SurfaceFunction
{
    // 해시 · 노이즈
    static uint mixHash( uint value );
    static float hashToUnit( uint hash );
    static uint hashTile( int2 tile, uint seed );
    static uint hashCell( int3 cell );
    static MaterialTileVariation computeTileVariation( int2 tile, uint seed );
    static float computeValueNoise( float3 position );
    static float computeFbm( float3 position, uint octaveCount );
    static float2 computeCellDistance( float2 position, uint seed );

    // 투영
    static float3 computeTriplanarWeights( float3 normal, float sharpness );
    static float4 sampleTriplanar( uint textureIndex, float3 position, float3 weights, float tiling );
    static float2 projectDominantAxis( float3 position, float3 normal );
    static float4 sampleNoTile( uint textureIndex, float2 uv, uint seed );
    static float2 rotateQuarter( float2 uv, uint rotation );

    // 섞기 · 규칙
    static float4 blendByHeight4( float4 weights, float4 heights, float depth );
    static void blendByHeight8( inout float4 inoutLow, inout float4 inoutHigh, float4 heightLow, float4 heightHigh, float depth );
    static float computeBand( float value, float low, float high, float fade );
    static float computeSlopeDegrees( float normalY );
    static float computeSlopeRadians( float normalY );
    static float computeMacroVariation( float3 position, float scale );

    // 무늬
    static MaterialPattern computeBrickPattern( float2 uv, float2 brickSize, float grooveWidth, float rowOffset );
    static MaterialPattern computePlankPattern( float2 uv, float2 plankSize, float grooveWidth, uint seed );
    static MaterialPattern computeCellPattern( float2 uv, float cellSize, float grooveWidth, uint seed );
    static float computeVeins( float3 position, float scale, float sharpness );
    static float computeStripes( float2 uv, float frequency, float skew );
    static float computeHexEdge( float2 uv );
    static float3 perturbNormalByHeight( float3 normal, float3 position, float height, float strength );
    static float computeCurvature( float3 normal, float3 position );

    // 마모 · 때 · 녹 · 변색
    static float computeEdgeWearMask( float3 position, float3 normal, float amount, float scale );
    static float computeRustMask( float3 position, float3 normal, float amount, float scale );
    static float computeDirtMask( float3 position, float3 normal, float amount, float scale );
    static float computeTarnishMask( float3 position, float3 normal, float amount, float scale );

    // 날씨
    static void applyWetness( inout float3 inoutAlbedo, inout float inoutRoughness, float porosity, float wetness );
    static float computePuddleMask( float3 position, float3 normal, float amount, float scale );
    static void applyPuddle( inout float3 inoutAlbedo, inout float inoutRoughness, inout float inoutMetallic, inout float3 inoutNormal, float puddle );
    static float computeSnowMask( float3 position, float3 normal, float amount, float snowLine );
    static void applySnow( inout float3 inoutAlbedo, inout float inoutRoughness, inout float inoutMetallic, inout float3 inoutNormal, float snow );
    static void applySparkle( inout float inoutRoughness, inout float3 inoutNormal, float3 position, float density, float amount );

    // 코트 · 이방성(W17 합치는 자리 — 파일 머리)
    static void applyClearCoat( inout float inoutRoughness, float coat, float coatRoughness );
    static float computeBrushedRoughness( float roughness, float2 uv, float anisotropy, float frequency );

    // 장치(기믹)
    static float computeFresnel( float3 normal, float3 toView, float power );
    static float computeScanlines( float3 position, float density, float speed, float time );
    static float4 sampleFlow( uint textureIndex, float2 uv, float2 flow, float time );
};

// ------------------------------------------------------------------------------
// 해시 · 노이즈
// ------------------------------------------------------------------------------

/** @brief 32 비트 섞기(lowbias32) — C++ `HashUtil::mix32` 와 같은 식. */
static uint SurfaceFunction::mixHash( uint value )
{
    value = ( value ^ ( value >> 16 ) ) * 0x7feb352du;
    value = ( value ^ ( value >> 15 ) ) * 0x846ca68bu;
    return value ^ ( value >> 16 );
}

/** @brief 해시의 상위 24 비트를 [0, 1) 로. */
static float SurfaceFunction::hashToUnit( uint hash )
{
    return float( hash >> 8 ) * kHashToUnit;
}

/** @brief 타일 해시 — C++ `MaterialProjection::hashTile` 과 같은 식(음수 좌표는 비트 그대로 uint 로). */
static uint SurfaceFunction::hashTile( int2 tile, uint seed )
{
    return SurfaceFunction::mixHash( ( asuint( tile.x ) * kTileHashX ) ^ SurfaceFunction::mixHash( ( asuint( tile.y ) * kTileHashY ) ^ ( seed + kTileHashSalt ) ) );
}

/** @brief 3 차원 격자점 해시(값 노이즈의 모서리). */
static uint SurfaceFunction::hashCell( int3 cell )
{
    return SurfaceFunction::mixHash( ( asuint( cell.x ) * kTileHashX ) ^ SurfaceFunction::mixHash( ( asuint( cell.y ) * kTileHashY ) ^ SurfaceFunction::mixHash( asuint( cell.z ) * kTileHashZ + kTileHashSalt ) ) );
}

/** @brief 타일 변형 — C++ `MaterialProjection::computeTileVariation` 과 같은 식. */
static MaterialTileVariation SurfaceFunction::computeTileVariation( int2 tile, uint seed )
{
    const uint            hash0 = SurfaceFunction::hashTile( tile, seed );
    const uint            hash1 = SurfaceFunction::mixHash( hash0 ^ kTileHashX );
    const uint            hash2 = SurfaceFunction::mixHash( hash1 ^ kTileHashY );
    MaterialTileVariation result;
    result._offset     = float2( SurfaceFunction::hashToUnit( hash0 ), SurfaceFunction::hashToUnit( hash1 ) );
    result._rotation   = hash2 & 3u;
    result._brightness = SurfaceFunction::hashToUnit( hash2 );
    return result;
}

/** @brief 3 차원 값 노이즈(0..1) — 격자점 해시를 부드러운 삼선형으로 섞는다. UV 가 없어도 이음매 없이 물체를 감싼다. */
static float SurfaceFunction::computeValueNoise( float3 position )
{
    const float3 cellFloor = floor( position );
    const float3 local     = position - cellFloor;
    const float3 fade      = local * local * ( 3.0f - 2.0f * local );
    const int3   cell      = int3( cellFloor );
    const float  n000      = SurfaceFunction::hashToUnit( SurfaceFunction::hashCell( cell + int3( 0, 0, 0 ) ) );
    const float  n100      = SurfaceFunction::hashToUnit( SurfaceFunction::hashCell( cell + int3( 1, 0, 0 ) ) );
    const float  n010      = SurfaceFunction::hashToUnit( SurfaceFunction::hashCell( cell + int3( 0, 1, 0 ) ) );
    const float  n110      = SurfaceFunction::hashToUnit( SurfaceFunction::hashCell( cell + int3( 1, 1, 0 ) ) );
    const float  n001      = SurfaceFunction::hashToUnit( SurfaceFunction::hashCell( cell + int3( 0, 0, 1 ) ) );
    const float  n101      = SurfaceFunction::hashToUnit( SurfaceFunction::hashCell( cell + int3( 1, 0, 1 ) ) );
    const float  n011      = SurfaceFunction::hashToUnit( SurfaceFunction::hashCell( cell + int3( 0, 1, 1 ) ) );
    const float  n111      = SurfaceFunction::hashToUnit( SurfaceFunction::hashCell( cell + int3( 1, 1, 1 ) ) );
    const float  nearPlane = lerp( lerp( n000, n100, fade.x ), lerp( n010, n110, fade.x ), fade.y );
    const float  farPlane  = lerp( lerp( n001, n101, fade.x ), lerp( n011, n111, fade.x ), fade.y );
    return lerp( nearPlane, farPlane, fade.z );
}

/** @brief 옥타브 @p octaveCount 개의 프랙털 노이즈(0..1). 옥타브마다 두 배 주파수 · 반 세기, 격자 정렬이 겹치지 않게 조금 어긋낸다. */
static float SurfaceFunction::computeFbm( float3 position, uint octaveCount )
{
    float sum       = 0.0f;
    float amplitude = 0.5f;
    float total     = 0.0f;
    for ( uint octave = 0; octave < octaveCount; ++octave )
    {
        sum += amplitude * SurfaceFunction::computeValueNoise( position );
        total += amplitude;
        position  = position * 2.03f + float3( 17.1f, 3.7f, 11.3f );
        amplitude = amplitude * 0.5f;
    }
    return sum / max( total, kDerivativeEpsilon );
}

/** @brief 2 차원 보로노이 — x = 가장 가까운 점까지, y = 둘째 점과의 차(칸 경계까지의 근사 거리). 균열 유약 · 돌담 · 용암 틈이 쓴다. */
static float2 SurfaceFunction::computeCellDistance( float2 position, uint seed )
{
    const float2 cellFloor = floor( position );
    const float2 local     = position - cellFloor;
    float        nearest   = 8.0f;
    float        second    = 8.0f;
    for ( int offsetY = -1; offsetY <= 1; ++offsetY )
    {
        for ( int offsetX = -1; offsetX <= 1; ++offsetX )
        {
            const int2   neighbor        = int2( offsetX, offsetY );
            const uint   hash            = SurfaceFunction::hashTile( int2( cellFloor ) + neighbor, seed );
            const float2 feature         = float2( SurfaceFunction::hashToUnit( hash ), SurfaceFunction::hashToUnit( SurfaceFunction::mixHash( hash ) ) );
            const float  featureDistance = length( float2( neighbor ) + feature - local );
            second                       = featureDistance < nearest ? nearest : min( second, featureDistance );
            nearest                      = min( nearest, featureDistance );
        }
    }
    return float2( nearest, second - nearest );
}

// ------------------------------------------------------------------------------
// 투영
// ------------------------------------------------------------------------------

/** @brief 3 축 가중치 |n|^s / 합 — C++ `MaterialProjection::computeTriplanarWeights` 와 같은 식. */
static float3 SurfaceFunction::computeTriplanarWeights( float3 normal, float sharpness )
{
    const float3 weights = pow( max( abs( normal ), kMinTriplanarAxis ), max( sharpness, kMinTriplanarSharpness ) );
    return weights / ( weights.x + weights.y + weights.z );
}

/** @brief 텍스처를 세 축(x 면 = zy, y 면 = xz, z 면 = xy)에서 찍어 가중치로 섞는다(언리얼 WorldAlignedTexture). */
static float4 SurfaceFunction::sampleTriplanar( uint textureIndex, float3 position, float3 weights, float tiling )
{
    const float4 sideX = MaterialTexture::sampleIndex( textureIndex, position.zy * tiling );
    const float4 sideY = MaterialTexture::sampleIndex( textureIndex, position.xz * tiling );
    const float4 sideZ = MaterialTexture::sampleIndex( textureIndex, position.xy * tiling );
    return sideX * weights.x + sideY * weights.y + sideZ * weights.z;
}

/** @brief 노멀이 가장 큰 축의 면으로 월드 위치를 투영한 2 차원 좌표(벽은 위가 +v). 절차 무늬를 UV 없는 벽 · 바닥에 찍는다. */
static float2 SurfaceFunction::projectDominantAxis( float3 position, float3 normal )
{
    const float3 axis = abs( normal );
    const float2 onX  = float2( position.z, position.y );
    const float2 onY  = float2( position.x, position.z );
    const float2 onZ  = float2( position.x, position.y );
    return ( axis.y >= axis.x && axis.y >= axis.z ) ? onY : ( ( axis.x >= axis.z ) ? onX : onZ );
}

/** @brief UV 를 타일 가운데(0.5, 0.5) 기준으로 90 도 × @p rotation 돌린다. */
static float2 SurfaceFunction::rotateQuarter( float2 uv, uint rotation )
{
    const float2 centered = uv - 0.5f;
    const float2 turned1  = float2( -centered.y, centered.x );
    const float2 turned   = ( rotation == 0u ) ? centered : ( ( rotation == 1u ) ? turned1 : ( ( rotation == 2u ) ? -centered : -turned1 ) );
    return turned + 0.5f;
}

/**
 * @brief 타일 반복 깨기 — 반 타일씩 어긋난 격자 넷의 타일 변형(오프셋 · 회전)으로 네 번 찍고, 각 격자 타일 경계에서 0 이 되는 삼각 가중치로 섞는다.
 * @details 네 가중치의 합은 어디서나 1 이고 각 표본은 자기 타일 경계에서 가중치 0 이라, 경계의 UV 점프(밉 선택이 튀는 자리)가 그림에 나오지 않는다
 *          (Inigo Quilez 의 "texture repetition" 방식 — 언리얼 TextureVariation, 유니티 Stochastic 노드와 같은 자리). 표본 넷이라 지형처럼 표본이 많은 곳보다
 *          벽 · 바닥 한 장에 쓴다. 회전은 방향 없는 무늬(돌 · 흙 · 콘크리트)에만 맞는다.
 */
static float4 SurfaceFunction::sampleNoTile( uint textureIndex, float2 uv, uint seed )
{
    float4 result = float4( 0.0f, 0.0f, 0.0f, 0.0f );
    for ( uint grid = 0; grid < 4u; ++grid )
    {
        const float2                gridShift = float2( ( grid & 1u ) != 0u ? 0.5f : 0.0f, ( grid & 2u ) != 0u ? 0.5f : 0.0f );
        const float2                shifted   = uv + gridShift;
        const float2                tile      = floor( shifted );
        const float2                local     = shifted - tile;
        const float2                tent      = 1.0f - abs( local * 2.0f - 1.0f );
        const MaterialTileVariation variation = SurfaceFunction::computeTileVariation( int2( tile ), seed + grid );
        const float2                tileUv    = SurfaceFunction::rotateQuarter( local, variation._rotation ) + variation._offset;
        result += MaterialTexture::sampleIndex( textureIndex, tileUv ) * ( tent.x * tent.y );
    }
    return result;
}

// ------------------------------------------------------------------------------
// 섞기 · 규칙
// ------------------------------------------------------------------------------

/**
 * @brief 높이 섞기(레이어 넷) — 키 = 가중치 × (1 + 높이), 가장 큰 키에서 @p depth 아래까지의 몫을 합 1 로. @p depth 0 이하는 가중치를 합으로만 나눈다.
 * @details C++ `SurfaceLayerWeights::computeHeightBlended` 와 같은 식이다(그쪽은 여덟을 한꺼번에 — 여기 넷은 머티리얼 층 넷처럼 따로 쓰는 경우).
 */
static float4 SurfaceFunction::blendByHeight4( float4 weights, float4 heights, float depth )
{
    const float4 keys    = weights * ( 1.0f + heights );
    const float  maxKey  = max( max( keys.x, keys.y ), max( keys.z, keys.w ) );
    const float4 shares  = max( keys - ( maxKey - max( depth, 0.0f ) ), 0.0f );
    const float4 blended = ( depth > 0.0f ) ? shares : weights;
    const float  total   = blended.x + blended.y + blended.z + blended.w;
    return ( total > kDerivativeEpsilon ) ? blended / total : float4( 1.0f, 0.0f, 0.0f, 0.0f );
}

/** @brief 높이 섞기(레이어 여덟, 지형) — 입력은 합 1 로 나눈 가중치다. C++ `SurfaceLayerWeights::computeHeightBlended` 와 같은 식. */
static void SurfaceFunction::blendByHeight8( inout float4 inoutLow, inout float4 inoutHigh, float4 heightLow, float4 heightHigh, float depth )
{
    const float4 keyLow    = inoutLow * ( 1.0f + heightLow );
    const float4 keyHigh   = inoutHigh * ( 1.0f + heightHigh );
    const float4 maxPair   = max( keyLow, keyHigh );
    const float  maxKey    = max( max( maxPair.x, maxPair.y ), max( maxPair.z, maxPair.w ) );
    const float  threshold = maxKey - depth;
    const float4 shareLow  = max( keyLow - threshold, 0.0f );
    const float4 shareHigh = max( keyHigh - threshold, 0.0f );
    const float  total     = dot( shareLow, float4( 1.0f, 1.0f, 1.0f, 1.0f ) ) + dot( shareHigh, float4( 1.0f, 1.0f, 1.0f, 1.0f ) );
    const bool   bBlend    = depth > 0.0f && total > kDerivativeEpsilon;
    inoutLow               = bBlend ? shareLow / total : inoutLow;
    inoutHigh              = bBlend ? shareHigh / total : inoutHigh;
}

/** @brief 띠 [low, high] 의 사다리꼴(안이 1, 바깥으로 페이드 폭만큼 0 까지) — C++ `TerrainLayerRule::computeBand` 와 같은 식. */
static float SurfaceFunction::computeBand( float value, float low, float high, float fade )
{
    const float width = max( fade, kMinRuleFade );
    return saturate( ( value - low ) / width + 1.0f ) * saturate( ( high - value ) / width + 1.0f );
}

/** @brief 노멀 y 로 수평에서 잰 경사(라디안) — C++ `TerrainLayerRule::computeSlopeRadians` 와 같은 식(지형 규칙). */
static float SurfaceFunction::computeSlopeRadians( float normalY )
{
    return acos( clamp( normalY, -1.0f, 1.0f ) );
}

/** @brief 노멀 y 로 수평에서 잰 경사(도) — 머티리얼 그래프 노드(SlopeMask)가 도로 적는 문턱과 견준다. */
static float SurfaceFunction::computeSlopeDegrees( float normalY )
{
    return degrees( acos( clamp( normalY, -1.0f, 1.0f ) ) );
}

/** @brief 큰 무늬(0..1, 0.5 근처) — 길이 @p scale(m)의 노이즈 둘을 겹쳐 타일 반복이 보이는 거리에서 밝기 · 거칠기를 흔든다. */
static float SurfaceFunction::computeMacroVariation( float3 position, float scale )
{
    const float3 scaled = position / max( scale, kDerivativeEpsilon );
    return SurfaceFunction::computeValueNoise( scaled ) * 0.6f + SurfaceFunction::computeValueNoise( scaled * 3.7f + 5.3f ) * 0.4f;
}

// ------------------------------------------------------------------------------
// 무늬
// ------------------------------------------------------------------------------

/**
 * @brief 벽돌 · 타일 — 칸 크기 @p brickSize(UV 단위), 줄눈 폭 @p grooveWidth, 줄마다 x 를 @p rowOffset 칸만큼 민다(벽돌 0.5, 타일 0).
 */
static MaterialPattern SurfaceFunction::computeBrickPattern( float2 uv, float2 brickSize, float grooveWidth, float rowOffset )
{
    float2      scaled = uv / brickSize;
    const float row    = floor( scaled.y );
    scaled.x += row * rowOffset;
    const float2    cell    = floor( scaled );
    const float2    local   = ( scaled - cell ) * brickSize;
    const float2    toEdge  = min( local, brickSize - local );
    const float     edge    = min( toEdge.x, toEdge.y );
    const float     halfGap = grooveWidth * 0.5f;
    MaterialPattern result;
    result._groove       = 1.0f - smoothstep( halfGap, halfGap + grooveWidth * 0.35f, edge );
    result._cell         = cell;
    result._edgeDistance = edge;
    return result;
}

/** @brief 판자 마루 — 줄마다 판자 길이를 해시로 어긋낸 벽돌 무늬(같은 이음이 줄 지어 서지 않는다). */
static MaterialPattern SurfaceFunction::computePlankPattern( float2 uv, float2 plankSize, float grooveWidth, uint seed )
{
    const float row      = floor( uv.y / plankSize.y );
    const float rowShift = SurfaceFunction::hashToUnit( SurfaceFunction::hashTile( int2( 0, int( row ) ), seed ) ) * plankSize.x;
    return SurfaceFunction::computeBrickPattern( float2( uv.x + rowShift, uv.y ), plankSize, grooveWidth, 0.0f );
}

/** @brief 보로노이 칸(돌담 · 자갈 · 균열 유약) — 칸 크기 @p cellSize, 경계 폭 @p grooveWidth(칸 단위). 폭이 좁으면 균열, 넓으면 줄눈 있는 돌담이다. */
static MaterialPattern SurfaceFunction::computeCellPattern( float2 uv, float cellSize, float grooveWidth, uint seed )
{
    const float2    scaled       = uv / max( cellSize, kDerivativeEpsilon );
    const float2    cellDistance = SurfaceFunction::computeCellDistance( scaled, seed );
    MaterialPattern result;
    result._groove       = 1.0f - smoothstep( grooveWidth * 0.5f, grooveWidth + 0.02f, cellDistance.y );
    result._cell         = floor( scaled );
    result._edgeDistance = cellDistance.y;
    return result;
}

/** @brief 대리석 결(0..1, 1 = 결 한가운데) — 노이즈로 휜 사인 띠. @p sharpness 가 클수록 결이 가늘다. */
static float SurfaceFunction::computeVeins( float3 position, float scale, float sharpness )
{
    const float3 scaled     = position / max( scale, kDerivativeEpsilon );
    const float  turbulence = SurfaceFunction::computeFbm( scaled, 4u ) * 6.0f;
    const float  wave       = abs( sin( ( scaled.x + scaled.y * 0.5f + turbulence ) * 3.14159f ) );
    return pow( saturate( 1.0f - wave ), max( sharpness, 1.0f ) );
}

/** @brief 줄무늬(0..1) — 주파수 @p frequency(UV 단위당 줄 수), @p skew 는 v 에 따라 줄을 기울인다(밧줄 꼬임 · 골판지 · 고무 홈 · 헤어라인). */
static float SurfaceFunction::computeStripes( float2 uv, float frequency, float skew )
{
    return 0.5f + 0.5f * sin( ( uv.x + uv.y * skew ) * frequency * 6.28318f );
}

/** @brief 육각 격자 경계까지의 거리(0 = 경계, 0.5 = 칸 가운데) — 역장 무늬. */
static float SurfaceFunction::computeHexEdge( float2 uv )
{
    const float2 ratio     = float2( 1.0f, 1.7320508f );
    const float2 halfRatio = ratio * 0.5f;
    const float2 a         = fmod( abs( uv ), ratio ) - halfRatio;
    const float2 b         = fmod( abs( uv ) + halfRatio, ratio ) - halfRatio;
    const float2 nearer    = dot( a, a ) < dot( b, b ) ? a : b;
    const float2 folded    = abs( nearer );
    return 0.5f - max( dot( folded, normalize( ratio ) ), folded.x );
}

/**
 * @brief 절차 높이 @p height 로 노멀을 기울인다(픽셀 미분 범프 — Mikkelsen 2010 "Bump Mapping Unparametrized Surfaces"). 탄젠트 없이 줄눈 · 균열 · 칩을 돋운다.
 * @details 픽셀 단계에서만 쓴다(ddx · ddy). @p strength 는 높이 1 이 몇 m 인지다.
 */
static float3 SurfaceFunction::perturbNormalByHeight( float3 normal, float3 position, float height, float strength )
{
    const float3 positionDx  = ddx( position );
    const float3 positionDy  = ddy( position );
    const float  heightDx    = ddx( height ) * strength;
    const float  heightDy    = ddy( height ) * strength;
    const float3 crossY      = cross( positionDy, normal );
    const float3 crossX      = cross( normal, positionDx );
    const float  determinant = dot( positionDx, crossY );
    const float3 gradient    = sign( determinant ) * ( heightDx * crossY + heightDy * crossX );
    const float3 bumped      = max( abs( determinant ), kDerivativeEpsilon * kDerivativeEpsilon ) * normal - gradient;
    return normalize( bumped + normal * kDerivativeEpsilon );
}

/** @brief 화면 미분으로 잰 곡률(1/m 근사) — 노멀이 위치에 비해 빨리 바뀌는 곳(둥근 모서리)이 크다. 단단한 모서리(정점이 갈린 노멀)는 잡지 못한다. */
static float SurfaceFunction::computeCurvature( float3 normal, float3 position )
{
    return length( fwidth( normal ) ) / max( length( fwidth( position ) ), kDerivativeEpsilon );
}

// ------------------------------------------------------------------------------
// 마모 · 때 · 녹 · 변색
// ------------------------------------------------------------------------------

/** @brief 모서리 마모 · 칠 벗겨짐(0..1) — 곡률이 큰 곳과 노이즈 문턱이 만나는 자리. @p amount 0 이면 없고 1 이면 넓다. 칩은 가장자리가 날카롭다. */
static float SurfaceFunction::computeEdgeWearMask( float3 position, float3 normal, float amount, float scale )
{
    const float curvature = saturate( SurfaceFunction::computeCurvature( normal, position ) * 0.15f );
    const float chips     = SurfaceFunction::computeFbm( position / max( scale, kDerivativeEpsilon ), 4u );
    const float wear      = chips + curvature * 0.6f + amount - 1.0f;
    return amount > 0.0f ? smoothstep( 0.0f, 0.06f, wear ) : 0.0f;
}

/** @brief 녹(0..1) — 노이즈 반점과 아래로 흘러내린 줄(y 로 늘인 노이즈). 위를 보는 면에 물이 고여 더 슨다. */
static float SurfaceFunction::computeRustMask( float3 position, float3 normal, float amount, float scale )
{
    const float3 scaled  = position / max( scale, kDerivativeEpsilon );
    const float  patches = SurfaceFunction::computeFbm( scaled, 5u );
    const float  streaks = SurfaceFunction::computeValueNoise( scaled * float3( 4.0f, 0.35f, 4.0f ) );
    const float  pooling = saturate( normal.y ) * 0.15f;
    const float  rust    = patches * 0.7f + streaks * 0.3f + pooling + amount - 1.0f;
    return amount > 0.0f ? smoothstep( 0.0f, 0.15f, rust ) : 0.0f;
}

/** @brief 때 · 먼지(0..1) — 위를 보는 면에 쌓이고 아래를 보는 면엔 튄 흙이 붙는다. */
static float SurfaceFunction::computeDirtMask( float3 position, float3 normal, float amount, float scale )
{
    const float noiseValue = SurfaceFunction::computeFbm( position / max( scale, kDerivativeEpsilon ), 4u );
    const float facing     = saturate( normal.y ) * 0.3f + saturate( -normal.y ) * 0.2f;
    const float dirt       = noiseValue * 0.8f + facing + amount - 1.0f;
    return amount > 0.0f ? smoothstep( 0.0f, 0.3f, dirt ) : 0.0f;
}

/** @brief 은 변색(0..1) — 손이 닿아 닦이는 볼록한 곳(곡률 큼)은 밝게 남고 오목 · 평평한 곳에 황화막이 낀다. */
static float SurfaceFunction::computeTarnishMask( float3 position, float3 normal, float amount, float scale )
{
    const float rubbed     = saturate( SurfaceFunction::computeCurvature( normal, position ) * 0.1f );
    const float noiseValue = SurfaceFunction::computeFbm( position / max( scale, kDerivativeEpsilon ), 3u );
    const float tarnish    = noiseValue * 0.6f - rubbed * 0.5f + amount - 0.5f;
    return amount > 0.0f ? smoothstep( 0.0f, 0.35f, tarnish ) : 0.0f;
}

// ------------------------------------------------------------------------------
// 날씨
// ------------------------------------------------------------------------------

/** @brief 젖음 — 다공성 @p porosity(흙 · 벽돌 1, 금속 · 유약 0)만큼 어두워지고, 젖음 @p wetness 만큼 번들거린다(Lagarde "Water drop" 의 2 단계). */
static void SurfaceFunction::applyWetness( inout float3 inoutAlbedo, inout float inoutRoughness, float porosity, float wetness )
{
    const float wet = saturate( wetness );
    inoutAlbedo *= lerp( 1.0f, kWetAlbedoScale, wet * saturate( porosity ) );
    inoutRoughness = lerp( inoutRoughness, min( inoutRoughness, kWetRoughness ), wet );
}

/** @brief 웅덩이 마스크(0..1) — 평평하고 위를 보는 면의 낮은 노이즈 자리부터 찬다. @p amount 가 클수록 넓다. */
static float SurfaceFunction::computePuddleMask( float3 position, float3 normal, float amount, float scale )
{
    const float level  = smoothstep( 0.92f, 0.99f, normal.y );
    const float hollow = 1.0f - SurfaceFunction::computeFbm( position / max( scale, kDerivativeEpsilon ), 3u );
    const float fill   = hollow + amount - 1.0f;
    return amount > 0.0f ? level * smoothstep( 0.0f, 0.08f, fill ) : 0.0f;
}

/** @brief 웅덩이 물막 — 거의 거울(거칠기 0.03), 바닥은 어둡고, 노멀은 평평하게(물 표면) 펴진다. 금속도 물에 덮인 만큼 유전체다. */
static void SurfaceFunction::applyPuddle( inout float3 inoutAlbedo, inout float inoutRoughness, inout float inoutMetallic, inout float3 inoutNormal, float puddle )
{
    const float water = saturate( puddle );
    inoutAlbedo *= lerp( 1.0f, kPuddleAlbedoScale, water );
    inoutRoughness = lerp( inoutRoughness, kPuddleRoughness, water );
    inoutMetallic  = lerp( inoutMetallic, 0.0f, water );
    inoutNormal    = normalize( lerp( inoutNormal, float3( 0.0f, 1.0f, 0.0f ), water ) );
}

/** @brief 눈 덮임(0..1) — 위를 보는 정도와 양 @p amount, 설선 @p snowLine(m) 위에서 쌓인다. 가장자리는 노이즈로 깨진다. */
static float SurfaceFunction::computeSnowMask( float3 position, float3 normal, float amount, float snowLine )
{
    const float facing   = saturate( normal.y );
    const float altitude = saturate( ( position.y - snowLine ) / 2.0f + 0.5f );
    const float breakup  = SurfaceFunction::computeValueNoise( position * 1.7f ) * 0.25f;
    const float cover    = facing - ( 1.0f - amount ) * 0.9f + breakup - 0.1f;
    return amount > 0.0f ? smoothstep( 0.0f, 0.12f, cover ) * altitude : 0.0f;
}

/** @brief 눈을 얹는다 — 흰 비금속, 거칠기 0.65, 노멀은 위로 조금 부드러워진다(눈이 굴곡을 메운다). */
static void SurfaceFunction::applySnow( inout float3 inoutAlbedo, inout float inoutRoughness, inout float inoutMetallic, inout float3 inoutNormal, float snow )
{
    const float cover = saturate( snow );
    inoutAlbedo       = lerp( inoutAlbedo, kSnowColor, cover );
    inoutRoughness    = lerp( inoutRoughness, kSnowRoughness, cover );
    inoutMetallic     = lerp( inoutMetallic, 0.0f, cover );
    inoutNormal       = normalize( lerp( inoutNormal, float3( 0.0f, 1.0f, 0.0f ), cover * 0.5f ) );
}

/**
 * @brief 반짝이(눈 결정 · 서리 · 운모) — 격자 칸 가운데 일부(밀도 @p density 칸/m, 양 @p amount)를 매끈한 작은 거울로 만들고 노멀을 해시 방향으로 기울인다.
 * @details 빛 · 시선을 따로 보지 않는다 — 기본 PBR 의 정반사가 기운 거울 칸에서 해 방향과 맞을 때 반짝인다(카메라가 움직이면 다른 칸이 빛난다).
 */
static void SurfaceFunction::applySparkle( inout float inoutRoughness, inout float3 inoutNormal, float3 position, float density, float amount )
{
    const int3   cell = int3( floor( position * density ) );
    const uint   hash = SurfaceFunction::hashCell( cell );
    const bool   bLit = SurfaceFunction::hashToUnit( hash ) < saturate( amount );
    const float3 tilt = float3( SurfaceFunction::hashToUnit( SurfaceFunction::mixHash( hash ) ), SurfaceFunction::hashToUnit( SurfaceFunction::mixHash( hash + 1u ) ),
                                SurfaceFunction::hashToUnit( SurfaceFunction::mixHash( hash + 2u ) ) ) *
                            2.0f -
                        1.0f;
    inoutNormal    = bLit ? normalize( inoutNormal + tilt * kSparkleTiltScale ) : inoutNormal;
    inoutRoughness = bLit ? kSparkleRoughness : inoutRoughness;
}

// ------------------------------------------------------------------------------
// 코트 · 이방성 — W17 합치는 자리(파일 머리)
// ------------------------------------------------------------------------------

/**
 * @brief 클리어 코트(유약 · 니스 · 자동차 칠) — 지금은 기본 PBR 한 겹이라 코트 거칠기 쪽으로 거칠기를 끌어당기는 근사다.
 * @details shortcut: 두 겹 정반사(밑 무늬의 흐릿한 반사 + 코트의 날카로운 반사)가 아니다 — W17 의 ClearCoat 셰이딩 모델이 들어오면 이 몸을 "모델 = ClearCoat,
 *          CustomData.xy = (coat, coatRoughness)" 로 바꾸고 밑 거칠기는 그대로 둔다.
 */
static void SurfaceFunction::applyClearCoat( inout float inoutRoughness, float coat, float coatRoughness )
{
    inoutRoughness = lerp( inoutRoughness, coatRoughness, saturate( coat ) * kClearCoatCover );
}

/**
 * @brief 헤어라인(브러시드 금속) 거칠기 — 한 방향으로 늘인 고주파 줄을 거칠기에 새긴다.
 * @details shortcut: 이방성 하이라이트(결 방향으로 늘어난 반사)가 아니라 결 무늬만이다 — W17 의 Anisotropic 모델이 들어오면 CustomData.z = anisotropy,
 *          방향은 탄젠트로 넘기고 이 함수는 결 무늬만 남긴다.
 */
static float SurfaceFunction::computeBrushedRoughness( float roughness, float2 uv, float anisotropy, float frequency )
{
    const float streak = SurfaceFunction::computeValueNoise( float3( uv.x * frequency, uv.y * 2.0f, 0.0f ) );
    return saturate( roughness + ( streak - 0.5f ) * 0.25f * saturate( anisotropy ) );
}

// ------------------------------------------------------------------------------
// 장치(기믹)
// ------------------------------------------------------------------------------

/** @brief 프레넬 테두리(0..1) — 시선과 노멀이 비스듬할수록 1. @p power 가 클수록 가장자리만. */
static float SurfaceFunction::computeFresnel( float3 normal, float3 toView, float power )
{
    return pow( saturate( 1.0f - abs( dot( normal, toView ) ) ), max( power, kDerivativeEpsilon ) );
}

/** @brief 주사선(0..1) — 월드 y 를 따라 흐르는 가는 밝은 줄(홀로그램). */
static float SurfaceFunction::computeScanlines( float3 position, float density, float speed, float time )
{
    const float wave = frac( position.y * density - time * speed );
    return smoothstep( 0.0f, 0.15f, wave ) * ( 1.0f - smoothstep( 0.35f, 0.5f, wave ) );
}

/**
 * @brief 흐름 — 같은 텍스처를 반 주기 어긋난 두 위상으로 흐름 @p flow(UV/s) 방향으로 밀고 위상이 되감기는 순간을 서로 가린다(Valve 2010 흐름 맵).
 */
static float4 SurfaceFunction::sampleFlow( uint textureIndex, float2 uv, float2 flow, float time )
{
    const float  phaseA  = frac( time );
    const float  phaseB  = frac( time + 0.5f );
    const float4 sampleA = MaterialTexture::sampleIndex( textureIndex, uv - flow * phaseA );
    const float4 sampleB = MaterialTexture::sampleIndex( textureIndex, uv - flow * phaseB + 0.5f );
    const float  blend   = abs( phaseA * 2.0f - 1.0f );
    return lerp( sampleA, sampleB, blend );
}

#endif // SW_ENGINE_MATERIALFUNCTIONS_HLSLI
