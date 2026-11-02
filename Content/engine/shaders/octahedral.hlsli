/**
 * octahedral.hlsli — 단위 벡터 ↔ 옥타헤드럴 좌표와 그 32 비트 묶음(snorm16 둘).
 *
 * G버퍼 노멀 첨부(R16G16_SNORM) · VAT 노멀 · 변형 결과(g_SwMorphVertices 의 법선 · 탄젠트 단어)가 쓴다.
 * C++ `OctahedralNormal`(Core/Math/OctahedralNormal.h)과 같은 식 · 같은 바이트다(_u 가 아래 16 비트).
 */

#ifndef SW_ENGINE_OCTAHEDRAL_HLSLI
#define SW_ENGINE_OCTAHEDRAL_HLSLI

/** @brief 단위 벡터 ↔ 옥타헤드럴 좌표 [-1, 1]^2 와 그 snorm16 둘 묶음입니다. */
struct Octahedral
{
    static float2 encode( float3 direction );
    static float3 decode( float2 coordinate );
    static uint pack( float3 direction );
    static float3 unpack( uint word );
};

/**
 * @brief 옥타헤드럴 좌표 [-1, 1]^2 를 단위 벡터로 푼다. C++ `OctahedralNormal::decodeOctant` 와 같은 식이다(0 은 양수 부호).
 */
static float3 Octahedral::decode( float2 coordinate )
{
    float3       normal = float3( coordinate, 1.0f - abs( coordinate.x ) - abs( coordinate.y ) );
    const float2 signs  = float2( normal.x >= 0.0f ? 1.0f : -1.0f, normal.y >= 0.0f ? 1.0f : -1.0f );
    const float2 folded = ( 1.0f - abs( normal.yx ) ) * signs;
    normal.xy           = ( normal.z < 0.0f ) ? folded : normal.xy;
    return normalize( normal );
}

/**
 * @brief 방향(길이는 상관없다)을 옥타헤드럴 좌표 [-1, 1]^2 로 접는다. C++ `OctahedralNormal::fromNormal` 과 같은 식이다
 *        (`sum = |x| + |y| + |z|`, z < 0 이면 접기, 0 은 양수 부호). 영벡터는 (0, 0) 이라 +z 로 풀린다.
 */
static float2 Octahedral::encode( float3 direction )
{
    const float  sum    = abs( direction.x ) + abs( direction.y ) + abs( direction.z );
    const float2 plane  = sum > 0.0f ? direction.xy / sum : float2( 0.0f, 0.0f );
    const float2 signs  = float2( plane.x >= 0.0f ? 1.0f : -1.0f, plane.y >= 0.0f ? 1.0f : -1.0f );
    const float2 folded = ( 1.0f - abs( plane.yx ) ) * signs;
    return ( direction.z < 0.0f ) ? folded : plane;
}

/** @brief 방향을 snorm16 둘(u 가 아래 16 비트)로 묶는다 — C++ `OctahedralNormal`(R16G16_SNORM)과 같은 바이트. 오차는 0.01 도 아래다. */
static uint Octahedral::pack( float3 direction )
{
    const int2 raw = (int2)round( clamp( Octahedral::encode( direction ), -1.0f, 1.0f ) * 32767.0f );
    return ( (uint)raw.x & 0xFFFFu ) | ( (uint)raw.y << 16u );
}

/** @brief `pack` 의 역 — snorm16 둘을 [-1, 1] 로 펴서(최솟값은 -1 로 자른다) 단위 벡터로 푼다. */
static float3 Octahedral::unpack( uint word )
{
    const float2 coordinate = max( float2( (float)( (int)( word << 16u ) >> 16 ), (float)( (int)word >> 16 ) ) / 32767.0f, -1.0f );
    return Octahedral::decode( coordinate );
}

#endif // SW_ENGINE_OCTAHEDRAL_HLSLI
