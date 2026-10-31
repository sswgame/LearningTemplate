/**
 * fog_common.hlsli — 안개의 식. 안개 패스(fog.hlsl)와 볼륨 안개 컴퓨트(volumetric_fog.hlsl)가 같은 식을 쓴다.
 *
 * C++ `FogSettings`(Engine/Environment/Fog/FogSettings.h)와 같은 식이다: 밀도(h) = 밀도 · exp( -높이 감쇠 · ( h - 기준 높이 ) ), 시선 위 광학 두께 = 밀도의 선적분,
 * 불투명도 = min( 최대 불투명도, 1 - exp( -광학 두께 ) ). 시작 거리 안쪽은 적분에서 뺀다. 두 파일이 상수 출처가 달라(패스 상수 대 컴퓨트 입력 버퍼) 값은 인자로 받는다 —
 * colorDensity = (안개 색 rgb, 밀도), heightParams = (높이 감쇠, 기준 높이, 시작 거리, 최대 불투명도).
 *
 * 볼륨(froxel)은 화면 칸 SW_FOG_VOLUME_DIM_X × SW_FOG_VOLUME_DIM_Y 와 시선 거리의 로그 조각 SW_FOG_VOLUME_DIM_Z 개다. 조각 경계 거리는
 * r(i) = SW_FOG_VOLUME_NEAR · ( 먼 거리 / SW_FOG_VOLUME_NEAR )^( i / 조각 수 ) 이다(i = 0 이 가까운 끝). 칸 수와 가까운 거리의 원본은 binding_slots.hlsli 10 절이다.
 */

#ifndef SW_ENGINE_FOGCOMMON_HLSLI
#define SW_ENGINE_FOGCOMMON_HLSLI

#include "common.hlsli"

static const float kFogMaxExponent   = 60.0f;   // exp 의 인자 상한 — 기준 높이보다 아주 아래의 점이 넘치지 않게 한다(C++ FogSettingsInternal::kMaxExponent)
static const float kFogSmallExponent = 1.0e-3f; // 이보다 작은 지수는 (1 - exp(-x)) / x 를 테일러 두 항으로 쓴다(C++ FogSettingsInternal::kSmallExponent)
static const float kFogTinyRate      = 1.0e-8f; // 나눗셈 보호

struct FogMath
{
    static float computeDensityAt( float4 colorDensity, float4 heightParams, float height );
    static float computeOpticalDepth( float4 colorDensity, float4 heightParams, float3 origin, float3 direction, float startDistance, float endDistance );
    static float computeSlicePosition( float distance, float farDistance );
    static float computeBoundaryDistance( float boundary, float farDistance );
    static float computePhase( float cosine, float anisotropy );
};

/** @brief 월드 높이 @p height 의 안개 밀도(1/m)입니다. */
static float FogMath::computeDensityAt( float4 colorDensity, float4 heightParams, float height )
{
    const float exponent = min( -heightParams.x * ( height - heightParams.y ), kFogMaxExponent );
    return colorDensity.w * exp( exponent );
}

/**
 * @brief 시선 @p origin + @p direction · s 의 구간 [@p startDistance, @p endDistance] 위 광학 두께(밀도의 선적분)입니다. @p direction 은 단위 벡터여야 합니다.
 * @details C++ `FogSettings::computeOpticalDepth` 와 같은 식이다. early-return 이 없다 — GL 드라이버가 early-return 모양을 잘못 컴파일한 적이 있다(binding.hlsli MorphPool::computeElement).
 */
static float FogMath::computeOpticalDepth( float4 colorDensity, float4 heightParams, float3 origin, float3 direction, float startDistance, float endDistance )
{
    const float clearedLength = max( endDistance - startDistance, 0.0f );
    const float startHeight   = origin.y + direction.y * startDistance;
    const float baseDensity   = FogMath::computeDensityAt( colorDensity, heightParams, startHeight );
    const float rate          = heightParams.x * direction.y;
    const float exponent      = rate * clearedLength;
    const float divisor       = abs( rate ) > kFogTinyRate ? rate : kFogTinyRate;
    const float integral      = abs( exponent ) < kFogSmallExponent ? clearedLength * ( 1.0f - 0.5f * exponent ) : ( 1.0f - exp( -max( exponent, -kFogMaxExponent ) ) ) / divisor;
    return max( baseDensity * integral, 0.0f );
}

/** @brief 시선 거리 @p distance 의 조각 경계 위치(0 이 가까운 끝, 조각 수가 먼 끝)입니다. 범위 밖은 양 끝으로 자른다. */
static float FogMath::computeSlicePosition( float distance, float farDistance )
{
    const float nearDistance = SW_FOG_VOLUME_NEAR;
    const float ratio        = log( max( distance, nearDistance ) / nearDistance ) / log( max( farDistance, nearDistance * 2.0f ) / nearDistance );
    return clamp( ratio * (float)SW_FOG_VOLUME_DIM_Z, 0.0f, (float)SW_FOG_VOLUME_DIM_Z );
}

/** @brief 조각 경계 @p boundary(0..조각 수)의 시선 거리입니다. `computeSlicePosition` 의 역함수다. */
static float FogMath::computeBoundaryDistance( float boundary, float farDistance )
{
    const float nearDistance = SW_FOG_VOLUME_NEAR;
    return nearDistance * pow( max( farDistance, nearDistance * 2.0f ) / nearDistance, boundary / (float)SW_FOG_VOLUME_DIM_Z );
}

/**
 * @brief 헨리-그린스타인 위상 함수 × 4π 입니다(등방일 때 1). @p cosine 은 시선 방향과 해 쪽 방향의 내적 — 해를 바라볼 때 1 이고 이방성이 0 보다 크면 앞으로 산란한다.
 */
static float FogMath::computePhase( float cosine, float anisotropy )
{
    const float squared     = anisotropy * anisotropy;
    const float denominator = max( 1.0f + squared - 2.0f * anisotropy * cosine, 1.0e-4f );
    return ( 1.0f - squared ) / ( denominator * sqrt( denominator ) );
}

#endif // SW_ENGINE_FOGCOMMON_HLSLI
