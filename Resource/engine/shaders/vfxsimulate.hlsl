#include "binding.hlsli"
#include "vfxsimulate.hlsli"

/**
 * vfxsimulate.hlsl — 입자 시뮬레이션의 GPU 쪽 (컴퓨트). CPU `VfxCPUSimulation` 과 같은 모듈 의미로 돌린다(언리얼 Niagara GPU Compute Sim).
 *
 * 그룹 하나 = 방출기 하나(디스패치 하나). 디스패치 하나가 이번 프레임의 단계를 차례로 돌고, 단계 하나는 CPU 와 같은 순서다.
 *   1) 생성 — 일정(C++ VfxEmitterClock 이 정한 단계마다의 수와 첫 번호)대로 풀 한도까지 살아 있는 끝에 붙인다. 버린 수는 인자 버퍼에 쌓는다.
 *      난수는 CPU 와 같은 splitmix64 수열이다(64 비트 정수가 없는 SM5 를 위해 uint2 로 흉내 낸다 — x 가 아래 32 비트).
 *   2) 적분 · 깊이 충돌 · 수명 제거 — 중력 · 속도 · 나이를 더하고, 깊이 충돌(SW_VFX_FLAG_COLLIDE_*)이면 주 시점의 지난 프레임 깊이와 견줘 튀거나 없앤다.
 *      살아남은 입자를 다른 반쪽에 **안정 압축**한다(덩어리마다 그룹 공유 메모리의 접두사 합). 버퍼는 늘 태어난 순서다 — 띠가 정렬하지 않고 이웃을 잇는다.
 * 단계가 끝나면 살아 있는 입자의 렌더 입력(GPUParticle — 크기 · 색은 수명 커브, 커브는 키와 접선을 올려 C++ FloatCurve 와 같은 식)을 쓰고,
 * 간접 인자(정점 수, 인스턴스 수)와 다음 디스패치가 읽을 살아 있는 수 · 지금 반쪽을 쓴다. 메시 방출기는 같은 렌더 입력을 vfxmeshinstance.hlsl 이 인스턴스 칸으로 옮긴다.
 *
 * 깊이 충돌(VfxCollisionMethod::SceneDepth): 입자의 월드 점을 히스토리 시점의 뷰-투영으로 투영해 그 텍셀의 깊이보다 뒤이고 깊이 면에서 두께 안쪽이면
 * 닿은 것이다. 법선은 이웃 텍셀 둘의 깊이 점이 이루는 면이다(눈 쪽으로 돌린다). 화면 밖 · 카메라 뒤는 보지 않는다(VfxCollisionDef 설명의 한계).
 * 히스토리는 R32 실수 RW 텍스처다 — 컴퓨트가 에뮬 백엔드(DX11 · GL)에서 샘플 텍스처를 읽지 못해 UAV 로 읽는다(Bindless::loadRwTexture2D).
 *
 * DX11: 간접 인자 버퍼는 구조 버퍼가 될 수 없어(DRAWINDIRECT_ARGS 와 BUFFER_STRUCTURED 를 함께 못 건다) RAW UAV 다 — 그래서 인자를 바이트 주소로 쓴다.
 * 나머지 백엔드는 uint 구조 버퍼다. 입자 상태와 렌더 입력은 따로 둔 구조 버퍼라 복사 단계가 필요 없다.
 *
 * 바인딩 계약: 읽기 t0 매개변수(g_VfxParams), 쓰기 u0 입자 상태(풀 크기 × 2 — 두 반쪽), u1 렌더 입력(풀 크기), u2 간접 인자, 깊이 히스토리는 RW 텍스처
 * 슬롯 0(SW_SLOT_COMPUTE_TEXUAV0, 네이티브는 bindless 번호). 루트 상수 g_VfxParamBase. C++: VfxGPUSimulation::recordDispatches — 방출기마다 dispatchCompute( 1, 1, 1 ).
 *
 * 병렬도 한계: 그룹 하나(256 스레드)가 방출기 하나를 돈다 — 풀이 수만 개면 한 그룹이 덩어리를 차례로 돌아 느리다. 여러 그룹 압축(그룹마다 개수 → 접두사 합 →
 * 쓰기, 디스패치 셋)은 계획 문서 `Vfx` 의 후속 설계로 두고 측정 뒤 정한다.
 */

/** @brief 입자 하나의 시뮬레이션 상태입니다(float4 SW_VFX_STATE_FLOAT4_COUNT 개 — C++ 는 크기만 안다). */
struct VfxParticleState
{
    float4 _positionAge;      // xyz 방출기 로컬 위치, w 나이(초)
    float4 _velocityLifetime; // xyz 속도, w 수명(초)
    uint4  _particleIndex;    // xy 입자 번호(64 비트, x 가 아래), zw 빈칸
};

struct GPUParticle
{
    float4 _positionSize;
    float4 _color;
    float4 _rotationLife;
};

SW_DECLARE_STRUCTURED_BUFFER( uint4, g_VfxParams, 0 );
SW_DECLARE_RW_STRUCTURED_BUFFER( VfxParticleState, g_VfxStateRW, 0 );
SW_DECLARE_RW_STRUCTURED_BUFFER( GPUParticle, g_VfxParticlesRW, 1 );
#if defined( DX11 )
SW_DECLARE_RW_BYTE_ADDRESS_BUFFER( g_VfxArgsRW, 2 );
#else
SW_DECLARE_RW_STRUCTURED_BUFFER( uint, g_VfxArgsRW, 2 );
#endif

SW_ROOT_CONSTANTS_BEGIN
{
    uint g_VfxParamBase; // 이 방출기 매개변수 구간의 첫 행
};
SW_ROOT_CONSTANTS_END

groupshared uint s_arrScan[SW_VFX_SIMULATE_THREAD_COUNT];

uint loadArgsWord( uint word )
{
#if defined( DX11 )
    return g_VfxArgsRW.Load( word * 4u );
#else
    return g_VfxArgsRW[word];
#endif
}

void storeArgsWord( uint word, uint value )
{
#if defined( DX11 )
    g_VfxArgsRW.Store( word * 4u, value );
#else
    g_VfxArgsRW[word] = value;
#endif
}

uint4 loadParamRow( uint row )
{
    return g_VfxParams[SW_ROOT( g_VfxParamBase ) + row];
}

/** @brief 행 @p firstRow 부터의 행렬(행벡터 규약)로 점을 옮깁니다(w 로 나누지 않는다). */
float4 transformPoint4( uint firstRow, float3 position )
{
    return position.x * asfloat( loadParamRow( firstRow ) ) + position.y * asfloat( loadParamRow( firstRow + 1u ) ) +
           position.z * asfloat( loadParamRow( firstRow + 2u ) ) + asfloat( loadParamRow( firstRow + 3u ) );
}

/** @brief 행 @p firstRow 부터의 행렬로 방향을 옮깁니다(이동 없음). */
float3 transformVector( uint firstRow, float3 direction )
{
    return ( direction.x * asfloat( loadParamRow( firstRow ) ) + direction.y * asfloat( loadParamRow( firstRow + 1u ) ) +
             direction.z * asfloat( loadParamRow( firstRow + 2u ) ) )
        .xyz;
}

// ------------------------------------------------------------------------------
// 64 비트 정수 흉내(uint2, x = 아래 32 비트) — C++ HashUtil::combine · mix64 · splitMix64Next 와 같은 식.
// ------------------------------------------------------------------------------
uint2 addUint64( uint2 left, uint2 right )
{
    const uint low   = left.x + right.x;
    const uint carry = low < left.x ? 1u : 0u;
    return uint2( low, left.y + right.y + carry );
}

/** @brief 32 비트 둘의 곱 전체(64 비트)입니다. 16 비트로 나눠 곱한다. */
uint2 multiplyFullUint32( uint left, uint right )
{
    const uint leftLow    = left & 0xFFFFu;
    const uint leftHigh   = left >> 16u;
    const uint rightLow   = right & 0xFFFFu;
    const uint rightHigh  = right >> 16u;
    const uint lowLow     = leftLow * rightLow;
    const uint lowHigh    = leftLow * rightHigh;
    const uint highLow    = leftHigh * rightLow;
    const uint highHigh   = leftHigh * rightHigh;
    const uint middle     = ( lowLow >> 16u ) + ( lowHigh & 0xFFFFu ) + ( highLow & 0xFFFFu );
    const uint resultLow  = ( middle << 16u ) | ( lowLow & 0xFFFFu );
    const uint resultHigh = highHigh + ( lowHigh >> 16u ) + ( highLow >> 16u ) + ( middle >> 16u );
    return uint2( resultLow, resultHigh );
}

/** @brief 64 비트 곱의 아래 64 비트입니다(C++ uint64 곱과 같다 — 넘침은 버린다). */
uint2 multiplyUint64( uint2 left, uint2 right )
{
    uint2 product = multiplyFullUint32( left.x, right.x );
    product.y += left.x * right.y + left.y * right.x;
    return product;
}

/** @brief value ^ ( value >> shift ) 입니다(0 < shift < 32). */
uint2 xorShiftRightUint64( uint2 value, uint shift )
{
    const uint2 shifted = uint2( ( value.x >> shift ) | ( value.y << ( 32u - shift ) ), value.y >> shift );
    return value ^ shifted;
}

uint2 mixUint64( uint2 value )
{
    const uint4 mixer = loadParamRow( SW_VFX_PARAM_ROW_MIXER );
    value             = multiplyUint64( xorShiftRightUint64( value, 30u ), mixer.xy );
    value             = multiplyUint64( xorShiftRightUint64( value, 27u ), mixer.zw );
    return xorShiftRightUint64( value, 31u );
}

/** @brief 수열의 다음 값을 [0, 1) 로 돌려줍니다 — 위 24 비트(C++ VfxCPUSimulationInternal::nextUnitFloat 와 같다). */
float nextUnitFloat( inout uint2 inoutState, uint2 goldenRatio )
{
    inoutState        = addUint64( inoutState, goldenRatio );
    const uint2 mixed = mixUint64( inoutState );
    return (float)( mixed.y >> 8u ) / 16777216.0f;
}

/** @brief HashUtil::combine( seed, value ) — seed ^ ( value + 황금비 + ( seed << 6 ) + ( seed >> 2 ) ). */
uint2 combineUint64( uint2 seed, uint2 value, uint2 goldenRatio )
{
    const uint2 shiftedLeft  = uint2( seed.x << 6u, ( seed.y << 6u ) | ( seed.x >> 26u ) );
    const uint2 shiftedRight = uint2( ( seed.x >> 2u ) | ( seed.y << 30u ), seed.y >> 2u );
    return seed ^ addUint64( addUint64( addUint64( value, goldenRatio ), shiftedLeft ), shiftedRight );
}

float lerpFloat( float from, float to, float ratio )
{
    return from + ( to - from ) * ratio;
}

// ------------------------------------------------------------------------------
// 수명 커브 — C++ FloatCurve::evaluate 와 같은 식(양 끝은 끝 키 값, 구간은 상수 · 직선 · 에르미트).
// 이른 반환 없이 끝에서 한 번에 고른다 — 이른 반환 판은 DXC -O3 SPIR-V(spirv-opt --simplify-instructions 를 거친 것)에서 Vulkan · GL 이
// 첫 키 값을 냈다(DXIL · -Od SPIR-V 는 맞음, Vfx.md V5). RenderPassGPUTest.CPUAndGPUParticleSimulationsDrawTheSameImage 가 지킨다.
// ------------------------------------------------------------------------------
float evaluateCurve( uint firstRow, uint keyCount, float time )
{
    const float4 firstKey = asfloat( loadParamRow( firstRow ) );
    const float4 lastKey  = asfloat( loadParamRow( firstRow + ( keyCount - 1u ) * SW_VFX_PARAM_ROWS_PER_KEY ) );
    // time 보다 늦은 첫 키 — 그 앞 키와 구간을 이룬다(키는 시각 순이라 C++ 의 이분 탐색과 같은 구간이다).
    uint toIndex = 1u;
    while ( toIndex + 1u < keyCount && asfloat( loadParamRow( firstRow + toIndex * SW_VFX_PARAM_ROWS_PER_KEY ).x ) <= time )
    {
        ++toIndex;
    }
    toIndex               = min( toIndex, keyCount - 1u );
    const uint   fromRow  = firstRow + ( max( toIndex, 1u ) - 1u ) * SW_VFX_PARAM_ROWS_PER_KEY;
    const float4 fromKey  = asfloat( loadParamRow( fromRow ) );
    const uint4  fromMode = loadParamRow( fromRow + 1u );
    const float4 toKey    = asfloat( loadParamRow( firstRow + toIndex * SW_VFX_PARAM_ROWS_PER_KEY ) );
    const float  span     = toKey.x - fromKey.x;
    const float  ratio    = span > 0.0f ? saturate( ( time - fromKey.x ) / span ) : 0.0f;
    const float  ratio2   = ratio * ratio;
    const float  ratio3   = ratio2 * ratio;
    const float  h00      = 2.0f * ratio3 - 3.0f * ratio2 + 1.0f;
    const float  h10      = ratio3 - 2.0f * ratio2 + ratio;
    const float  h01      = -2.0f * ratio3 + 3.0f * ratio2;
    const float  h11      = ratio3 - ratio2;
    const float  hermite  = h00 * fromKey.y + h10 * span * fromKey.w + h01 * toKey.y + h11 * span * toKey.z;
    const float  lerped   = fromKey.y + ( toKey.y - fromKey.y ) * ratio;
    float        value    = fromMode.y != 0u ? lerped : hermite;
    value                 = ( fromMode.x != 0u || span <= 0.0f ) ? fromKey.y : value;
    value                 = time >= lastKey.x ? lastKey.y : value;
    value                 = ( keyCount == 1u || time <= firstKey.x ) ? firstKey.y : value;
    return value;
}

/** @brief 입자 번호 @p particleIndex 의 처음 상태입니다 — 수명 → 속도 x → y → z 순으로 난수를 하나씩 쓴다(CPU 와 같은 순서). */
VfxParticleState makeParticle( uint2 particleIndex )
{
    const uint4  seedRow     = loadParamRow( SW_VFX_PARAM_ROW_SEED );
    const float4 velocityMin = asfloat( loadParamRow( SW_VFX_PARAM_ROW_VELOCITY_MIN ) );
    const float4 velocityMax = asfloat( loadParamRow( SW_VFX_PARAM_ROW_VELOCITY_MAX ) );
    uint2        state       = combineUint64( seedRow.xy, particleIndex, seedRow.zw );

    VfxParticleState particle;
    particle._velocityLifetime.w = lerpFloat( velocityMin.w, velocityMax.w, nextUnitFloat( state, seedRow.zw ) );
    particle._velocityLifetime.x = lerpFloat( velocityMin.x, velocityMax.x, nextUnitFloat( state, seedRow.zw ) );
    particle._velocityLifetime.y = lerpFloat( velocityMin.y, velocityMax.y, nextUnitFloat( state, seedRow.zw ) );
    particle._velocityLifetime.z = lerpFloat( velocityMin.z, velocityMax.z, nextUnitFloat( state, seedRow.zw ) );
    particle._positionAge        = float4( 0.0f, 0.0f, 0.0f, 0.0f );
    particle._particleIndex      = uint4( particleIndex, 0u, 0u );
    return particle;
}

// ------------------------------------------------------------------------------
// 깊이 충돌 — 주 시점의 지난 프레임 깊이(FrameRenderer 의 깊이 히스토리)와 견준다.
// ------------------------------------------------------------------------------
/** @brief 히스토리 텍셀 @p texel 의 깊이를 월드 점으로 되돌립니다(텍셀 가운데). */
float3 computeDepthWorldPoint( int2 texel, float2 size, uint historyIndex )
{
    const float  depth = Bindless::loadRwTexture2D( historyIndex, uint2( texel ) ).x;
    const float2 uv    = ( float2( texel ) + 0.5f ) / size;
    const float2 ndc   = float2( uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f );
    const float4 world = transformPoint4( SW_VFX_PARAM_ROW_DEPTH_INVERSE_VIEW_PROJ, float3( ndc, depth ) );
    return world.xyz / world.w;
}

/**
 * @brief 입자 하나의 깊이 충돌입니다. 닿지 않았으면 그대로 두고, 없애기면 @p outKilled 를 켜고, 튀기면 위치 · 속도를 고칩니다(방출기 로컬).
 * @details 튀기는 CPU 와 같은 식이다 — 법선 성분을 반발 계수만큼 뒤집고 접선 성분을 마찰만큼 줄인다. 면에서 멀어지는 중이면 튀기지 않는다(같은 면에 다시 걸린다).
 */
void resolveDepthCollision( inout VfxParticleState inoutParticle, uint flags, out bool outKilled )
{
    outKilled          = false;
    const float3 world = transformPoint4( SW_VFX_PARAM_ROW_WORLD, inoutParticle._positionAge.xyz ).xyz;
    const float4 clip  = transformPoint4( SW_VFX_PARAM_ROW_DEPTH_VIEW_PROJ, world );
    if ( clip.w <= 1e-4f )
        return; // 카메라 뒤
    const float3 ndc       = clip.xyz / clip.w;
    const bool   bOnScreen = abs( ndc.x ) <= 1.0f && abs( ndc.y ) <= 1.0f && 0.0f <= ndc.z && ndc.z <= 1.0f;
    if ( bOnScreen == false )
        return; // 화면 밖은 깊이가 없다
    const uint4  info  = loadParamRow( SW_VFX_PARAM_ROW_DEPTH_INFO );
    const float2 size  = float2( info.xy );
    const float2 uv    = float2( ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f );
    const int2   texel = clamp( int2( uv * size ), int2( 0, 0 ), int2( info.xy ) - 1 );
    const float  depth = Bindless::loadRwTexture2D( info.z, uint2( texel ) ).x;
    if ( ndc.z <= depth )
        return; // 깊이 면 앞이다

    const float3 surface     = computeDepthWorldPoint( texel, size, info.z );
    const float4 eyeRow      = asfloat( loadParamRow( SW_VFX_PARAM_ROW_DEPTH_EYE ) );
    const float4 collision   = asfloat( loadParamRow( SW_VFX_PARAM_ROW_COLLISION ) );
    const float  penetration = length( world - eyeRow.xyz ) - length( surface - eyeRow.xyz );
    if ( penetration > collision.w )
        return; // 두께보다 뒤 — 다른 물체 뒤에 가려진 자리라 닿은 것이 아니다
    if ( ( flags & SW_VFX_FLAG_COLLIDE_KILL ) != 0u )
    {
        outKilled = true;
        return;
    }

    // 법선 — 이웃 텍셀 둘의 깊이 점이 이루는 면. 가장자리는 안쪽 이웃을 쓴다.
    const int2   stepX     = int2( texel.x + 1 < int( info.x ) ? 1 : -1, 0 );
    const int2   stepY     = int2( 0, texel.y + 1 < int( info.y ) ? 1 : -1 );
    const float3 alongX    = ( computeDepthWorldPoint( texel + stepX, size, info.z ) - surface ) * float( stepX.x );
    const float3 alongY    = ( computeDepthWorldPoint( texel + stepY, size, info.z ) - surface ) * float( stepY.y );
    float3       normal    = cross( alongX, alongY );
    const float  normalLen = length( normal );
    if ( normalLen <= 1e-8f )
        return; // 기울기가 없다(깊이가 같은 텍셀) — 면을 정할 수 없다
    normal /= normalLen;
    if ( dot( normal, eyeRow.xyz - surface ) < 0.0f )
        normal = -normal;

    float3      velocity    = transformVector( SW_VFX_PARAM_ROW_WORLD, inoutParticle._velocityLifetime.xyz );
    const float normalSpeed = dot( velocity, normal );
    if ( normalSpeed >= 0.0f )
        return; // 이미 면에서 멀어진다
    const float3 normalPart             = normal * normalSpeed;
    const float3 tangentPart            = velocity - normalPart;
    velocity                            = tangentPart * ( 1.0f - collision.y ) - normalPart * collision.x;
    const float3 bouncedWorld           = surface + normal * collision.z;
    inoutParticle._positionAge.xyz      = transformPoint4( SW_VFX_PARAM_ROW_INVERSE_WORLD, bouncedWorld ).xyz;
    inoutParticle._velocityLifetime.xyz = transformVector( SW_VFX_PARAM_ROW_INVERSE_WORLD, velocity );
}

SW_NUMTHREADS( SW_VFX_SIMULATE_THREAD_COUNT, 1, 1 )
void CSMain( uint3 groupThreadId : SV_GroupThreadID )
{
    const uint   thread        = groupThreadId.x;
    const uint4  header        = loadParamRow( SW_VFX_PARAM_ROW_HEADER );
    const uint   poolSize      = header.x;
    const uint   stepCount     = header.y;
    const uint   flags         = header.z;
    const float4 gravity       = asfloat( loadParamRow( SW_VFX_PARAM_ROW_GRAVITY ) );
    const bool   bReset        = ( flags & SW_VFX_FLAG_RESET ) != 0u;
    const bool   bCollideDepth = ( flags & ( SW_VFX_FLAG_COLLIDE_BOUNCE | SW_VFX_FLAG_COLLIDE_KILL ) ) != 0u && ( flags & SW_VFX_FLAG_DEPTH_READY ) != 0u;
    // 살아 있는 수 · 반쪽 · 버린 수는 모든 스레드가 같은 값을 레지스터에 든다(같은 식으로 고친다) — 그룹 공유 메모리로 나누지 않는다.
    uint aliveCount   = bReset ? 0u : loadArgsWord( SW_VFX_ARGS_WORD_ALIVE );
    uint currentHalf  = bReset ? 0u : loadArgsWord( SW_VFX_ARGS_WORD_HALF );
    uint droppedCount = bReset ? 0u : loadArgsWord( SW_VFX_ARGS_WORD_DROPPED );
    GroupMemoryBarrierWithGroupSync();

    for ( uint stepIndex = 0u; stepIndex < stepCount; ++stepIndex )
    {
        // 1) 생성 — 풀이 차면 뒤쪽 번호를 버린다(번호는 일정이 이미 썼다).
        const uint4 spawn        = loadParamRow( SW_VFX_PARAM_ROW_FIRST_STEP + stepIndex );
        const uint  createdCount = min( spawn.x, poolSize - aliveCount );
        const uint  sourceBase   = currentHalf * poolSize;
        droppedCount += spawn.x - createdCount;
        for ( uint spawnIndex = thread; spawnIndex < createdCount; spawnIndex += SW_VFX_SIMULATE_THREAD_COUNT )
        {
            g_VfxStateRW[sourceBase + aliveCount + spawnIndex] = makeParticle( addUint64( spawn.yz, uint2( spawnIndex, 0u ) ) );
        }
        DeviceMemoryBarrierWithGroupSync();

        // 2) 적분 · 충돌 · 수명 제거 — 다른 반쪽에 태어난 순서 그대로 압축한다.
        const uint totalCount = aliveCount + createdCount;
        const uint targetBase = ( 1u - currentHalf ) * poolSize;
        uint       written    = 0u;
#if defined( DX11 )
        // FXC(cs_5_0)는 UAV 에서 읽은 수(살아 있는 수)로 끝나는 반복 안의 동기화를 받지 않는다(X3663) — 풀 전체(매개변수 SRV 의 값)를 돌고
        // 수 밖의 칸은 죽은 입자로 센다(위 slot < totalCount). 결과는 같고 덩어리 수만 풀 크기를 따른다.
        const uint chunkLimit = poolSize;
#else
        const uint chunkLimit = totalCount;
#endif
        for ( uint chunkFirst = 0u; chunkFirst < chunkLimit; chunkFirst += SW_VFX_SIMULATE_THREAD_COUNT )
        {
            const uint       slot   = chunkFirst + thread;
            bool             bAlive = false;
            VfxParticleState particle;
            particle._positionAge      = float4( 0.0f, 0.0f, 0.0f, 0.0f );
            particle._velocityLifetime = float4( 0.0f, 0.0f, 0.0f, 1.0f );
            particle._particleIndex    = uint4( 0u, 0u, 0u, 0u );
            if ( slot < totalCount )
            {
                particle = g_VfxStateRW[sourceBase + slot];
                particle._velocityLifetime.xyz += gravity.xyz * gravity.w;
                particle._positionAge.xyz += particle._velocityLifetime.xyz * gravity.w;
                particle._positionAge.w += gravity.w;
                bool bKilled = false;
                if ( bCollideDepth )
                    resolveDepthCollision( particle, flags, bKilled );
                bAlive = particle._positionAge.w < particle._velocityLifetime.w && bKilled == false;
            }
            // 포함 접두사 합(Hillis-Steele). 읽기와 쓰기 사이에 동기화를 둔다.
            s_arrScan[thread] = bAlive ? 1u : 0u;
            GroupMemoryBarrierWithGroupSync();
            for ( uint offset = 1u; offset < SW_VFX_SIMULATE_THREAD_COUNT; offset <<= 1u )
            {
                const uint added = thread >= offset ? s_arrScan[thread - offset] : 0u;
                GroupMemoryBarrierWithGroupSync();
                s_arrScan[thread] += added;
                GroupMemoryBarrierWithGroupSync();
            }
            if ( bAlive )
                g_VfxStateRW[targetBase + written + s_arrScan[thread] - 1u] = particle;
            written += s_arrScan[SW_VFX_SIMULATE_THREAD_COUNT - 1u];
            GroupMemoryBarrierWithGroupSync();
        }
        DeviceMemoryBarrierWithGroupSync();
        aliveCount  = written;
        currentHalf = 1u - currentHalf;
    }

    // 렌더 입력 — 크기 · 색은 수명 비율의 커브다(CPU VfxCPUSimulation::writeParticles 와 같다).
    const uint4 curveSizeRed   = loadParamRow( SW_VFX_PARAM_ROW_CURVE_SIZE_RED );
    const uint4 curveGreenBlue = loadParamRow( SW_VFX_PARAM_ROW_CURVE_GREEN_BLUE );
    const uint4 curveAlpha     = loadParamRow( SW_VFX_PARAM_ROW_CURVE_ALPHA );
    const uint  currentBase    = currentHalf * poolSize;
    for ( uint outputIndex = thread; outputIndex < aliveCount; outputIndex += SW_VFX_SIMULATE_THREAD_COUNT )
    {
        const VfxParticleState particle  = g_VfxStateRW[currentBase + outputIndex];
        const float            lifeRatio = particle._positionAge.w / particle._velocityLifetime.w;
        GPUParticle            output;
        output._positionSize          = float4( particle._positionAge.xyz, evaluateCurve( curveSizeRed.x, curveSizeRed.y, lifeRatio ) );
        output._color                 = float4( evaluateCurve( curveSizeRed.z, curveSizeRed.w, lifeRatio ), evaluateCurve( curveGreenBlue.x, curveGreenBlue.y, lifeRatio ),
                                                evaluateCurve( curveGreenBlue.z, curveGreenBlue.w, lifeRatio ), evaluateCurve( curveAlpha.x, curveAlpha.y, lifeRatio ) );
        output._rotationLife          = float4( 0.0f, lifeRatio, 0.0f, 0.0f );
        g_VfxParticlesRW[outputIndex] = output;
    }

    if ( thread == 0u )
    {
        const bool bRibbon       = ( flags & SW_VFX_FLAG_RIBBON ) != 0u;
        const uint instanceCount = bRibbon ? ( aliveCount >= 2u ? aliveCount - 1u : 0u ) : aliveCount;
        storeArgsWord( 0u, header.w ); // 정점 수
        storeArgsWord( 1u, instanceCount );
        storeArgsWord( 2u, 0u );
        storeArgsWord( 3u, 0u );
        storeArgsWord( SW_VFX_ARGS_WORD_ALIVE, aliveCount );
        storeArgsWord( SW_VFX_ARGS_WORD_HALF, currentHalf );
        storeArgsWord( SW_VFX_ARGS_WORD_DROPPED, droppedCount );
    }
}
