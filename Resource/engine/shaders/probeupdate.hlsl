#include "common.hlsli"
#include "lightdata.hlsli"

/**
 * probeupdate.hlsl — 실시간 라이트 프로브 갱신 컴퓨트(RenderPassType::ProbeUpdate, RealtimeGlobalIllumination.md R6). 유니티 APV 의 갱신, 언리얼 Lumen 의 프로브 갱신 자리다.
 *
 * 스레드 하나가 프로브 하나다(볼륨들의 프로브를 이어 붙인 번호). 프로브마다 프레임에 두 가지를 한다.
 *   (1) 이번 프레임의 조(프로브 번호 % 갱신 주기 == 프레임 번호 % 갱신 주기)면 광선 N 개를 쏘아 SH9 계수를 새로 구해 기록(history)에 섞는다.
 *   (2) 기록(원시 SH9 계수 27 개 → float4 일곱)을 소비 셰이더가 읽는 프로브 원소(SH9Color::makeGPUElement 와 같은 배치)로 바꿔 프로브 버퍼에 쓴다.
 *       프로브 버퍼는 CPU 가 프레임마다 0 으로 올리므로 갱신하지 않는 프레임에도 모든 프로브를 쓴다.
 *
 * 광선은 점유 복셀(SceneVoxelGrid — traceVoxelRay 가 C++ SceneVoxelGrid::traceRay 와 같은 식)을 걷는다. 닿으면 그 면이 받는 직접광(방향광 · 점광 · 스폿 — 라이트 버퍼의 앞 몇 개,
 * 감쇠는 LightProbeBaker::computeLightIncidence 와 같다, 가림은 같은 복셀로 본다) × 볼륨의 반사율이 그 방향의 복사휘도다. 닿지 않으면 하늘 SH 를 읽는다 — 베이커와 같은 규칙(한 번 튐 + 하늘).
 * 방향은 프로브 · 프레임마다 회전한 피보나치 구라서 갱신마다 달라 기록과 섞이며 잡음이 평균된다.
 *
 * 바인딩 계약: 읽기 t0 라이트(g_SwLights 와 같은 버퍼) · t1 입력 행(g_ProbeParams) · t2 복셀 단어(볼륨들의 비트 격자를 이어 붙임), 쓰기 u0 기록(float4, 프로브마다 일곱) · u1 프로브 버퍼.
 * 입력 행 배치는 아래 kRow*, C++: FrameRenderer::dispatchProbeUpdate(RealtimeProbeUpdater) — dispatchCompute( ceil(총 프로브 수 / 64), 1, 1 ).
 */

SW_DECLARE_STRUCTURED_BUFFER( LightData, g_ProbeLights, 0 );
SW_DECLARE_STRUCTURED_BUFFER( float4, g_ProbeParams, 1 );
SW_DECLARE_STRUCTURED_BUFFER( uint, g_ProbeVoxels, 2 );
SW_DECLARE_RW_STRUCTURED_BUFFER( float4, g_ProbeHistoryRW, 0 );
SW_DECLARE_RW_STRUCTURED_BUFFER( float4, g_ProbeOutRW, 1 );

// 입력 행 배치(C++ RealtimeProbeUpdater 가 같은 순서로 올린다).
static const uint kRowHeader      = 0u; // asuint( 볼륨 수, 프레임 번호, 라이트 수, 0 )
static const uint kRowSky         = 1u; // 하늘 SH 의 원시 계수 27 개를 float4 일곱에(1..7)
static const uint kRowFirstVolume = 8u;
static const uint kVolumeRowCount = 7u;
static const uint kVolOrigin      = 0u; // 프로브 시작점 xyz, 반사율
static const uint kVolSpacing     = 1u; // 프로브 간격 xyz, 새 값 섞는 비율
static const uint kVolCounts      = 2u; // asuint( 프로브 수 X, Y, Z, 프로브 버퍼 안의 첫 float4 번호 )
static const uint kVolVoxelOrigin = 3u; // 복셀 시작점 xyz, 칸 크기
static const uint kVolVoxelDim    = 4u; // asuint( 복셀 칸 수 X, Y, Z, 복셀 단어 버퍼 안의 첫 단어 )
static const uint kVolHistory     = 5u; // asuint( 기록의 첫 float4 번호, 첫 스레드 번호, 프로브 수, 광선 수 )
static const uint kVolSchedule    = 6u; // asuint( 갱신 주기, 기록이 쌓인 프레임 수, 0, 0 )

static const uint kProbeFloat4Count = 7u; // SW_LIGHT_PROBE_FLOAT4_COUNT 와 같다
static const uint kMaxDirectLights  = 8u; // 닿은 면을 비추는 라이트의 상한(앞에서부터)
// 원주율은 common.hlsli 의 kPi 를 쓴다.
static const float kSurfaceOffset   = 0.05f; // 닿은 칸 안쪽으로 파고들지 않게 면에서 띄우는 칸 크기 비율
static const float kParallelEpsilon = 1.0e-8f;
static const float kFarDistance     = 1.0e30f;

// SH9 기저 상수(SphericalHarmonics.cpp 의 Band0/1/2 — 1/(2√π), √3/(2√π), √15/(2√π), √5/(4√π), √15/(4√π)).
static const float kBand0  = 0.2820947918f;
static const float kBand1  = 0.4886025119f;
static const float kCross  = 1.0925484306f;
static const float kZonal  = 0.3153915653f;
static const float kSquare = 0.5462742153f;
// 소비 셰이더 배치용 곱(코사인 로브 Â_l / π 를 곱한 것 — SH9Color::makeGPUElement).
static const float kPackBand0  = 0.2820947918f; // kBand0 * 1
static const float kPackBand1  = 0.3257350079f; // kBand1 * 2/3
static const float kPackCross  = 0.2731371077f; // kCross * 1/4
static const float kPackZonal  = 0.0788478913f; // kZonal * 1/4
static const float kPackSquare = 0.1365685538f; // kSquare * 1/4

struct VoxelGrid
{
    float3 _origin;
    float  _cellSize;
    uint3  _dimension;
    uint   _wordOffset;
};

uint hashUint( uint value )
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

/** @brief 해시를 [0, 1) 로 바꿉니다(위 24 비트). */
float hashToUnit( uint value )
{
    return (float)( value >> 8u ) * ( 1.0f / 16777216.0f );
}

bool isVoxelOccupied( VoxelGrid grid, int3 cell )
{
    if ( any( cell < int3( 0, 0, 0 ) ) || any( cell >= (int3)grid._dimension ) )
        return false;
    const uint index = ( (uint)cell.z * grid._dimension.y + (uint)cell.y ) * grid._dimension.x + (uint)cell.x;
    return ( ( g_ProbeVoxels[grid._wordOffset + ( index >> 5u )] >> ( index & 31u ) ) & 1u ) != 0u;
}

/** @brief 광선이 처음 만나는 켜진 칸입니다. C++ SceneVoxelGrid::traceRay 와 같은 식이다(식을 고치면 두 곳을 함께 고친다). */
bool traceVoxelRay( VoxelGrid grid, float3 origin, float3 direction, float maxDistance, out float outHitDistance, out float3 outHitNormal )
{
    outHitDistance         = 0.0f;
    outHitNormal           = float3( 0.0f, 1.0f, 0.0f );
    const float3 local     = origin - grid._origin;
    const float3 extent    = (float3)grid._dimension * grid._cellSize;
    float        tNear     = 0.0f;
    float        tFar      = maxDistance;
    int          entryAxis = -1;
    // 펼친 루프 안에서 continue 를 쓰지 않는다 — FXC(DX11)는 그것을 바깥 [loop](CSMain 의 광선 루프)까지 펼치려다 X3531 로 진다.
    [unroll] for ( int axis = 0; axis < 3; ++axis )
    {
        const float component  = direction[axis];
        const float offset     = local[axis];
        const float extentAxis = extent[axis];
        if ( abs( component ) < kParallelEpsilon )
        {
            if ( offset < 0.0f || offset > extentAxis )
                return false;
        }
        else
        {
            const float inverse = 1.0f / component;
            float       tEnter  = ( 0.0f - offset ) * inverse;
            float       tExit   = ( extentAxis - offset ) * inverse;
            if ( tEnter > tExit )
            {
                const float swapped = tEnter;
                tEnter              = tExit;
                tExit               = swapped;
            }
            if ( tEnter > tNear )
            {
                tNear     = tEnter;
                entryAxis = axis;
            }
            tFar = min( tFar, tExit );
            if ( tNear > tFar )
                return false;
        }
    }

    int3 cell = (int3)clamp( floor( ( local + direction * tNear ) / grid._cellSize ), float3( 0.0f, 0.0f, 0.0f ), (float3)grid._dimension - 1.0f );
    if ( isVoxelOccupied( grid, cell ) )
    {
        int normalAxis = entryAxis;
        if ( normalAxis < 0 )
        {
            const float3 magnitude = abs( direction );
            normalAxis             = ( magnitude.x >= magnitude.y && magnitude.x >= magnitude.z ) ? 0 : ( ( magnitude.y >= magnitude.z ) ? 1 : 2 );
        }
        outHitDistance        = tNear;
        const float3 axisMask = float3( normalAxis == 0 ? 1.0f : 0.0f, normalAxis == 1 ? 1.0f : 0.0f, normalAxis == 2 ? 1.0f : 0.0f ); // 동적 첨자 쓰기 대신 마스크(FXC)
        outHitNormal          = axisMask * ( dot( direction, axisMask ) >= 0.0f ? -1.0f : 1.0f );
        return true;
    }

    int3   stepDirection = int3( 0, 0, 0 );
    float3 tMax          = float3( kFarDistance, kFarDistance, kFarDistance );
    float3 tDelta        = float3( kFarDistance, kFarDistance, kFarDistance );
    [unroll] for ( int stepAxis = 0; stepAxis < 3; ++stepAxis )
    {
        const float component = direction[stepAxis];
        if ( abs( component ) >= kParallelEpsilon )
        {
            const bool  bForward    = component > 0.0f;
            const float boundary    = (float)( bForward ? cell[stepAxis] + 1 : cell[stepAxis] ) * grid._cellSize;
            stepDirection[stepAxis] = bForward ? 1 : -1;
            tMax[stepAxis]          = ( boundary - local[stepAxis] ) / component;
            tDelta[stepAxis]        = grid._cellSize / abs( component );
        }
    }
    const uint maxStepCount = grid._dimension.x + grid._dimension.y + grid._dimension.z + 3u;
    // 고를 축은 축 마스크로 고르고 루프 가운데서 return 하지 않고 break 한다(하나의 출구 — 위 펼친 루프의 continue 와 같은 까닭으로 FXC 가 바깥 루프를 펼치지 않게).
    bool bHit = false;
    SW_LOOP for ( uint stepIndex = 0u; stepIndex < maxStepCount; ++stepIndex )
    {
        const bool  bAxisX        = tMax.x <= tMax.y && tMax.x <= tMax.z;
        const bool  bAxisY        = bAxisX == false && tMax.y <= tMax.z;
        const int3  axisMask      = int3( bAxisX ? 1 : 0, bAxisY ? 1 : 0, ( bAxisX || bAxisY ) ? 0 : 1 );
        const float enterDistance = dot( tMax, (float3)axisMask );
        if ( enterDistance > tFar )
            break;
        cell += stepDirection * axisMask;
        if ( any( cell < int3( 0, 0, 0 ) ) || any( cell >= (int3)grid._dimension ) )
            break;
        tMax += tDelta * (float3)axisMask;
        if ( isVoxelOccupied( grid, cell ) )
        {
            outHitDistance = enterDistance;
            outHitNormal   = -(float3)( stepDirection * axisMask );
            bHit           = true;
            break;
        }
    }
    return bHit;
}

/** @brief 방향의 SH9 기저 아홉 개입니다(SH9Color::computeBasis 와 같다). */
void computeBasis( float3 direction, out float outBasis[9] )
{
    outBasis[0] = kBand0;
    outBasis[1] = kBand1 * direction.y;
    outBasis[2] = kBand1 * direction.z;
    outBasis[3] = kBand1 * direction.x;
    outBasis[4] = kCross * direction.x * direction.y;
    outBasis[5] = kCross * direction.y * direction.z;
    outBasis[6] = kZonal * ( 3.0f * direction.z * direction.z - 1.0f );
    outBasis[7] = kCross * direction.x * direction.z;
    outBasis[8] = kSquare * ( direction.x * direction.x - direction.y * direction.y );
}

/** @brief 하늘 SH(원시 계수 27 개)가 이 방향에서 주는 복사휘도입니다. */
float3 evaluateSkyRadiance( float3 direction )
{
    float arrBasisWeight[9];
    computeBasis( direction, arrBasisWeight );
    float3 radiance = float3( 0.0f, 0.0f, 0.0f );
    [unroll] for ( uint coefficient = 0u; coefficient < 9u; ++coefficient )
    {
        const uint   first = coefficient * 3u;
        const float4 rowA  = g_ProbeParams[kRowSky + ( first >> 2u )];
        const float4 rowB  = g_ProbeParams[kRowSky + ( ( first + 1u ) >> 2u )];
        const float4 rowC  = g_ProbeParams[kRowSky + ( ( first + 2u ) >> 2u )];
        const float3 value = float3( rowA[first & 3u], rowB[( first + 1u ) & 3u], rowC[( first + 2u ) & 3u] );
        radiance += value * arrBasisWeight[coefficient];
    }
    return radiance;
}

/**
 * @brief 라이트 하나가 이 위치에 닿는 방향 · 감쇠 · 거리입니다(LightProbeBaker::computeLightIncidence 와 같다). 3D 빛이 아니면 false 입니다. 방향광의 거리는 음수(끝이 없다)입니다.
 */
bool computeIncidence( LightData light, float3 position, out float3 outToLight, out float outAttenuation, out float outDistance )
{
    const uint lightType = (uint)( light._directionType.w + 0.5f );
    outToLight           = -light._directionType.xyz;
    outAttenuation       = 1.0f;
    outDistance          = -1.0f;
    if ( lightType == SW_LIGHT_TYPE_DIRECTIONAL )
        return true;
    if ( lightType != SW_LIGHT_TYPE_POINT && lightType != SW_LIGHT_TYPE_SPOT )
        return false;
    const float3 delta     = light._positionRadius.xyz - position;
    outDistance            = length( delta );
    const float radius     = max( light._positionRadius.w, 1.0e-4f );
    outToLight             = delta / max( outDistance, 1.0e-4f );
    const float normalized = saturate( 1.0f - outDistance / radius );
    outAttenuation         = normalized * normalized;
    if ( lightType == SW_LIGHT_TYPE_SPOT )
    {
        const float cosAngle = dot( light._directionType.xyz, -outToLight );
        const float cone     = saturate( ( cosAngle - light._params.y ) / max( light._params.z - light._params.y, 1.0e-4f ) );
        outAttenuation *= cone * cone;
    }
    return true;
}

/** @brief 닿은 면(위치 @p position, 노멀 @p normal)이 직접 받는 빛입니다. 가림은 같은 복셀 격자로 본다(LightProbeBaker::computeDirectLight 와 같다). */
float3 computeDirectLight( VoxelGrid grid, uint lightCount, float3 position, float3 normal )
{
    float3     result = float3( 0.0f, 0.0f, 0.0f );
    const uint count  = min( lightCount, kMaxDirectLights );
    SW_LOOP for ( uint lightIndex = 0u; lightIndex < count; ++lightIndex )
    {
        const LightData light = g_ProbeLights[lightIndex];
        float3          toLight;
        float           attenuation;
        float           distance;
        if ( computeIncidence( light, position, toLight, attenuation, distance ) == false )
            continue;
        const float strength = max( dot( normal, toLight ), 0.0f ) * attenuation * light._colorIntensity.a;
        if ( strength <= 0.0f )
            continue;
        const float maxDistance = distance < 0.0f ? length( (float3)grid._dimension * grid._cellSize ) : max( distance - grid._cellSize * kSurfaceOffset, 0.0f );
        float       blockerDistance;
        float3      blockerNormal;
        if ( maxDistance > 0.0f && traceVoxelRay( grid, position, toLight, maxDistance, blockerDistance, blockerNormal ) )
            continue;
        result += light._colorIntensity.rgb * strength;
    }
    return result;
}

/** @brief 원시 SH9 계수(28 칸, 27 개 + 0)를 소비 셰이더의 프로브 원소 일곱으로 바꿉니다(SH9Color::makeGPUElement 와 같다). */
void packProbeElement( float coefficient[28], out float4 outElement[7] )
{
    [unroll] for ( uint channel = 0u; channel < 3u; ++channel )
    {
        outElement[channel]      = float4( coefficient[9u + channel] * kPackBand1, coefficient[3u + channel] * kPackBand1, coefficient[6u + channel] * kPackBand1,
                                           coefficient[channel] * kPackBand0 - coefficient[18u + channel] * kPackZonal );
        outElement[3u + channel] = float4( coefficient[12u + channel] * kPackCross, coefficient[15u + channel] * kPackCross, coefficient[18u + channel] * kPackZonal * 3.0f,
                                           coefficient[21u + channel] * kPackCross );
    }
    outElement[6] = float4( coefficient[24] * kPackSquare, coefficient[25] * kPackSquare, coefficient[26] * kPackSquare, 0.0f );
}

SW_NUMTHREADS( 64, 1, 1 )
void CSMain( uint3 dispatchId : SV_DispatchThreadID )
{
    const uint4 header      = asuint( g_ProbeParams[kRowHeader] );
    const uint  volumeCount = header.x;
    const uint  frameIndex  = header.y;
    const uint  lightCount  = header.z;

    // 이 스레드의 볼륨 — 첫 스레드 번호 이상 · 첫 스레드 번호 + 프로브 수 미만. 범위 밖(올림한 남는 스레드)은 아무것도 하지 않는다.
    uint volumeBase = 0u;
    bool bFound     = false;
    SW_LOOP for ( uint volumeIndex = 0u; volumeIndex < volumeCount; ++volumeIndex )
    {
        const uint  row     = kRowFirstVolume + volumeIndex * kVolumeRowCount;
        const uint4 history = asuint( g_ProbeParams[row + kVolHistory] );
        if ( dispatchId.x >= history.y && dispatchId.x < history.y + history.z )
        {
            volumeBase = row;
            bFound     = true;
        }
    }
    if ( bFound == false )
        return;

    const float4 originAlbedo = g_ProbeParams[volumeBase + kVolOrigin];
    const float4 spacingBlend = g_ProbeParams[volumeBase + kVolSpacing];
    const uint4  counts       = asuint( g_ProbeParams[volumeBase + kVolCounts] );
    const float4 voxelOrigin  = g_ProbeParams[volumeBase + kVolVoxelOrigin];
    const uint4  voxelDim     = asuint( g_ProbeParams[volumeBase + kVolVoxelDim] );
    const uint4  history      = asuint( g_ProbeParams[volumeBase + kVolHistory] );
    const uint4  schedule     = asuint( g_ProbeParams[volumeBase + kVolSchedule] );

    const uint probe     = dispatchId.x - history.y;
    const uint historyAt = history.x + probe * kProbeFloat4Count;
    const uint outAt     = counts.w + probe * kProbeFloat4Count;
    const uint period    = max( schedule.x, 1u );
    const bool bRefresh  = ( probe % period ) == ( frameIndex % period );

    // 기록(원시 계수 27 개 + 0).
    float arrCoefficient[28];
    [unroll] for ( uint slot = 0u; slot < kProbeFloat4Count; ++slot )
    {
        const float4 stored            = g_ProbeHistoryRW[historyAt + slot];
        arrCoefficient[slot * 4u]      = stored.x;
        arrCoefficient[slot * 4u + 1u] = stored.y;
        arrCoefficient[slot * 4u + 2u] = stored.z;
        arrCoefficient[slot * 4u + 3u] = stored.w;
    }

    if ( bRefresh )
    {
        VoxelGrid grid;
        grid._origin     = voxelOrigin.xyz;
        grid._cellSize   = voxelOrigin.w;
        grid._dimension  = voxelDim.xyz;
        grid._wordOffset = voxelDim.w;

        const uint   probeX   = probe % counts.x;
        const uint   probeY   = ( probe / counts.x ) % counts.y;
        const uint   probeZ   = probe / ( counts.x * counts.y );
        const float3 position = originAlbedo.xyz + spacingBlend.xyz * float3( (float)probeX, (float)probeY, (float)probeZ );
        const float  albedo   = originAlbedo.w;
        const uint   rayCount = max( history.w, 1u );
        const float  weight   = 4.0f * kPi / (float)rayCount;

        // 방향 수열: 피보나치 구를 프로브 · 갱신 횟수마다 회전한다(Cranley-Patterson). 갱신마다 방향이 달라 기록과 섞이며 평균된다.
        const uint  seed       = hashUint( dispatchId.x * 747796405u + ( frameIndex / period ) * 2891336453u );
        const float heightOff  = hashToUnit( seed );
        const float azimuthOff = hashToUnit( hashUint( seed + 1u ) ) * 2.0f * kPi;

        float arrFresh[28];
        [unroll] for ( uint clearIndex = 0u; clearIndex < 28u; ++clearIndex )
        {
            arrFresh[clearIndex] = 0.0f;
        }
        SW_LOOP for ( uint ray = 0u; ray < rayCount; ++ray )
        {
            const float  height    = 1.0f - 2.0f * frac( ( (float)ray + 0.5f ) / (float)rayCount + heightOff );
            const float  azimuth   = (float)ray * 2.39996323f + azimuthOff;
            const float  ring      = sqrt( max( 1.0f - height * height, 0.0f ) );
            const float3 direction = float3( ring * cos( azimuth ), ring * sin( azimuth ), height );

            float3 radiance;
            float  hitDistance;
            float3 hitNormal;
            if ( traceVoxelRay( grid, position, direction, kFarDistance, hitDistance, hitNormal ) )
            {
                const float3 surface = position + direction * hitDistance + hitNormal * ( grid._cellSize * kSurfaceOffset );
                radiance             = computeDirectLight( grid, lightCount, surface, hitNormal ) * albedo;
            }
            else
            {
                radiance = evaluateSkyRadiance( direction );
            }

            float arrBasisWeight[9];
            computeBasis( direction, arrBasisWeight );
            [unroll] for ( uint basisIndex = 0u; basisIndex < 9u; ++basisIndex )
            {
                const float factor = arrBasisWeight[basisIndex] * weight;
                arrFresh[basisIndex * 3u] += radiance.x * factor;
                arrFresh[basisIndex * 3u + 1u] += radiance.y * factor;
                arrFresh[basisIndex * 3u + 2u] += radiance.z * factor;
            }
        }

        // 기록이 막 생겼으면(쌓인 프레임 수 0) 새 값으로 바로 바꾼다. 쌓일수록 설정한 비율로 섞는다 - 1 / (쌓인 프레임 + 1) 이 더 크면 그 비율(처음 몇 번을 빨리 채운다).
        const float blend = max( spacingBlend.w, 1.0f / ( (float)schedule.y + 1.0f ) );
        [unroll] for ( uint mixIndex = 0u; mixIndex < 27u; ++mixIndex )
        {
            arrCoefficient[mixIndex] = lerp( arrCoefficient[mixIndex], arrFresh[mixIndex], blend );
        }
        [unroll] for ( uint writeSlot = 0u; writeSlot < kProbeFloat4Count; ++writeSlot )
        {
            g_ProbeHistoryRW[historyAt + writeSlot] = float4( arrCoefficient[writeSlot * 4u], arrCoefficient[writeSlot * 4u + 1u], arrCoefficient[writeSlot * 4u + 2u], arrCoefficient[writeSlot * 4u + 3u] );
        }
    }

    float4 arrElement[7];
    packProbeElement( arrCoefficient, arrElement );
    [unroll] for ( uint outSlot = 0u; outSlot < kProbeFloat4Count; ++outSlot )
    {
        g_ProbeOutRW[outAt + outSlot] = arrElement[outSlot];
    }
}
