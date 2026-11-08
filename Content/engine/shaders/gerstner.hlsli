/**
 * gerstner.hlsli — 거스트너 파도 변위 · 노멀. C++ `WaterWaveMath`(Source/Engine/Environment/Water)와 **같은 식 · 같은 계산 순서**다 — 부력 ·
 * 수면 질의가 CPU 에서 이 값을 다시 계산하므로 한쪽만 고치면 배가 수면에서 떠오르거나 가라앉는다.
 * RenderPassGPUTest.WaterWaveShaderMatchesCPU 가 컴퓨트(common/shaders/water_wave_probe.hlsl)로 이 함수를 돌려 CPU 값과 대조한다.
 *
 * 파도 하나 = float4( 방향(라디안, +x 에서 +z 쪽), 파장, 진폭, 가파름 0..1 ). 진폭이나 파장이 0 이하면 빈 칸이다.
 * 파수 k = 2π / 파장, 각진동수 ω = √(g k)(깊은 물 분산 — g 는 설정된 물리 중력을 머티리얼 · 루트 상수로 받는다, 숫자를 두지 않는다), Q = 가파름 / (k × 진폭 × 파도 수) — 가파름 1 에서도 물마루가 고리를 만들지 않는다.
 */

#ifndef SW_ENGINE_GERSTNER_HLSLI
#define SW_ENGINE_GERSTNER_HLSLI

#include "common.hlsli"

#if SW_GERSTNER_WAVE_COUNT != 4
    #error "GerstnerWaves holds four named waves - update the struct and the four calls in Gerstner when SW_GERSTNER_WAVE_COUNT changes"
#endif

/**
 * @brief 파도 넷. 배열(`float4 [4]`)이 아니라 이름 있는 칸 넷이다 — 배열을 함수 인자로 값 복사하면 NVIDIA OpenGL(ARB_gl_spirv) 이 칸 1..3 을 NaN 으로 읽어
 *        정점이 사라졌다(DX12 · Vulkan 은 같은 소스로 바르게 그렸다). 같은 모양을 쓰지 못하게 API 에서 배열을 뺀다.
 */
struct GerstnerWaves
{
    float4 _wave0;
    float4 _wave1;
    float4 _wave2;
    float4 _wave3;
};

struct GerstnerTerm
{
    float2 _direction;
    float  _waveNumber;
    float  _sharpness;
    float  _phase;
    float  _amplitude;
};

struct Gerstner
{
    static uint countWave( float4 wave );
    static uint countWaves( GerstnerWaves waves );
    static GerstnerTerm computeTerm( float4 wave, uint activeCount, float2 origin, float time, float gravity );
    static float3 computeDisplacementTerm( float4 wave, uint activeCount, float2 origin, float time, float gravity );
    static void addNormalTerm( float4 wave, uint activeCount, float2 origin, float time, float gravity, inout float3 ioTangentX, inout float3 ioTangentZ );
    static float3 computeDisplacement( float2 origin, float time, float gravity, GerstnerWaves waves );
    static float3 computeNormal( float2 origin, float time, float gravity, GerstnerWaves waves );
};

/** @brief 진폭 · 파장이 0 보다 크면 1 — 빈 칸은 0. */
static uint Gerstner::countWave( float4 wave )
{
    return ( wave.z > 0.0f && wave.y > 0.0f ) ? 1u : 0u;
}

/** @brief 진폭 · 파장이 0 보다 큰 파도 수 — Q 를 나누는 수다. */
static uint Gerstner::countWaves( GerstnerWaves waves )
{
    return Gerstner::countWave( waves._wave0 ) + Gerstner::countWave( waves._wave1 ) + Gerstner::countWave( waves._wave2 ) + Gerstner::countWave( waves._wave3 );
}

/**
 * @brief 파도 하나의 항 — 방향 · 파수 · Q · 위상 · 진폭. 빈 칸이면 진폭 · 파수 · Q 가 0 이라 항이 0 이다.
 * @details 분기 · 조기 반환 · 0 나누기를 두지 않는다. 빈 칸(진폭 0 · 파장 1)에서 Q = 가파름 / (k × 0 × n) 가 0/0 이 되는 식을 분기로 가리면 NVIDIA OpenGL(ARB_gl_spirv)
 *          이 두 갈래를 섞어 NaN 을 냈다(수면 정점이 사라지고 노멀이 NaN — DX12 · Vulkan 은 같은 소스로 바르게 그렸다). 나누는 수를 늘 양수로 두고 빈 칸은 0 을 곱해 끈다 —
 *          켠 칸의 값은 CPU `WaterWaveMath` 와 같고(max 가 그대로 둔다), 빈 칸이 더하는 +0 은 합을 바꾸지 않는다.
 */
static GerstnerTerm Gerstner::computeTerm( float4 wave, uint activeCount, float2 origin, float time, float gravity )
{
    const float  active     = ( wave.z > 0.0f && wave.y > 0.0f && activeCount != 0u ) ? 1.0f : 0.0f;
    const float  waveNumber = kTwoPi / max( wave.y, 1e-6f );
    const float2 direction  = float2( cos( wave.x ), sin( wave.x ) );
    GerstnerTerm term;
    term._direction  = direction;
    term._waveNumber = active * waveNumber;
    term._amplitude  = active * wave.z;
    term._sharpness  = active * wave.w / ( waveNumber * max( wave.z, 1e-6f ) * (float)max( activeCount, 1u ) );
    term._phase      = active * ( waveNumber * ( direction.x * origin.x + direction.y * origin.y ) - sqrt( max( gravity * waveNumber, 0.0f ) ) * time );
    return term;
}

/** @brief 파도 하나가 더하는 변위(x, y, z). 빈 칸이면 0. */
static float3 Gerstner::computeDisplacementTerm( float4 wave, uint activeCount, float2 origin, float time, float gravity )
{
    const GerstnerTerm term   = Gerstner::computeTerm( wave, activeCount, origin, time, gravity );
    const float        cosine = cos( term._phase );
    return float3( term._sharpness * term._amplitude * term._direction.x * cosine, term._amplitude * sin( term._phase ),
                   term._sharpness * term._amplitude * term._direction.y * cosine );
}

/** @brief 수평 원점 (x, z) 의 점이 시간 @p time 에 옮겨 간 변위(x, y, z). 파도 0..3 순서로 더한다(CPU `WaterWaveMath` 와 같은 순서). */
static float3 Gerstner::computeDisplacement( float2 origin, float time, float gravity, GerstnerWaves waves )
{
    const uint activeCount  = Gerstner::countWaves( waves );
    float3     displacement = Gerstner::computeDisplacementTerm( waves._wave0, activeCount, origin, time, gravity );
    displacement += Gerstner::computeDisplacementTerm( waves._wave1, activeCount, origin, time, gravity );
    displacement += Gerstner::computeDisplacementTerm( waves._wave2, activeCount, origin, time, gravity );
    displacement += Gerstner::computeDisplacementTerm( waves._wave3, activeCount, origin, time, gravity );
    return displacement;
}

/** @brief 파도 하나가 두 접선(원점 x · z 편미분)에 더하는 몫. 빈 칸이면 0 을 더한다. */
static void Gerstner::addNormalTerm( float4 wave, uint activeCount, float2 origin, float time, float gravity, inout float3 ioTangentX, inout float3 ioTangentZ )
{
    const GerstnerTerm term      = Gerstner::computeTerm( wave, activeCount, origin, time, gravity );
    const float2       direction = term._direction;
    const float        slope     = term._waveNumber * term._amplitude;
    const float        crest     = term._sharpness * slope * sin( term._phase );
    const float        cosine    = cos( term._phase );
    ioTangentX += float3( -crest * direction.x * direction.x, slope * direction.x * cosine, -crest * direction.x * direction.y );
    ioTangentZ += float3( -crest * direction.x * direction.y, slope * direction.y * cosine, -crest * direction.y * direction.y );
}

/** @brief 수평 원점 (x, z) 의 옮겨 간 점의 수면 노멀(정규화) — 옮겨 간 면의 두 접선(원점 x · z 편미분)의 외적이다. */
static float3 Gerstner::computeNormal( float2 origin, float time, float gravity, GerstnerWaves waves )
{
    const uint activeCount = Gerstner::countWaves( waves );
    float3     tangentX    = float3( 1.0f, 0.0f, 0.0f );
    float3     tangentZ    = float3( 0.0f, 0.0f, 1.0f );
    Gerstner::addNormalTerm( waves._wave0, activeCount, origin, time, gravity, tangentX, tangentZ );
    Gerstner::addNormalTerm( waves._wave1, activeCount, origin, time, gravity, tangentX, tangentZ );
    Gerstner::addNormalTerm( waves._wave2, activeCount, origin, time, gravity, tangentX, tangentZ );
    Gerstner::addNormalTerm( waves._wave3, activeCount, origin, time, gravity, tangentX, tangentZ );
    return normalize( cross( tangentZ, tangentX ) );
}

#endif // SW_ENGINE_GERSTNER_HLSLI
