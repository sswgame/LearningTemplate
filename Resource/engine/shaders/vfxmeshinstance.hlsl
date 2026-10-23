#include "common.hlsli"
#include "instancedata.hlsli"
#include "vfxsimulate.hlsli"

/**
 * vfxmeshinstance.hlsl — GPU 메시 입자의 인스턴스 쓰기 (컴퓨트). 시뮬레이션(vfxsimulate.hlsl)이 쓴 렌더 입력을 메시 인스턴스 배치의 인스턴스 칸으로 옮긴다
 * (언리얼 Niagara 메시 렌더러가 GPU 시뮬레이션의 입자를 인스턴스 데이터로 읽는 자리).
 *
 * 스레드 하나 = 풀 자리 하나. 칸은 GT 가 그 프레임의 배치 구성에서 찾아 실었다(`GPUSceneBuilder::appendVfxMeshEmitter` — 불투명 구간만).
 * 살아 있는 자리는 C++ `VfxComponent::writeMeshEntries` 와 같은 식(크기 × 입자 위치 이동 × 방출기 행렬, 바운드 = 메시 반지름 × 크기 × 방출기 배율, 색 RGBA8)이고,
 * 나머지 자리는 크기 0 행렬 · 바운드 0 이다. 행렬 · 바운드 · 색만 바꾸고 나머지 칸(배치 번호 · 머티리얼 · 블렌드 · 스프라이트 UV)은 CPU 가 올린 그대로 둔다.
 * 인스턴스 애니메이션과 컬링 **앞에** 돈다 — 컬링이 이 바운드로 거르고 간접 인자를 만든다.
 *
 * 바인딩 계약: 읽기 t0 매개변수(g_VfxParams), t1 렌더 입력(g_VfxParticles), t2 간접 인자(살아 있는 수, DX11 은 RAW), 쓰기 u0 인스턴스(g_VfxInstancesRW).
 * 루트 상수 g_VfxMeshParamBase. C++: VfxGPUSimulation::recordDispatches — 메시 방출기마다 dispatchCompute( 풀 / 64, 1, 1 ).
 */

struct GPUParticle
{
    float4 positionSize;
    float4 color;
    float4 rotationLife;
};

SW_DECLARE_STRUCTURED_BUFFER( uint4, g_VfxParams, 0 );
SW_DECLARE_STRUCTURED_BUFFER( GPUParticle, g_VfxParticles, 1 );
#if defined( DX11 )
SW_DECLARE_BYTE_ADDRESS_BUFFER( g_VfxArgs, 2 );
#else
SW_DECLARE_STRUCTURED_BUFFER( uint, g_VfxArgs, 2 );
#endif
SW_DECLARE_RW_STRUCTURED_BUFFER( InstanceData, g_VfxInstancesRW, 0 );

SW_ROOT_CONSTANTS_BEGIN
{
    uint g_VfxMeshParamBase; // 이 방출기 메시 구간의 첫 행
};
SW_ROOT_CONSTANTS_END

uint loadArgsWord( uint word )
{
#if defined( DX11 )
    return g_VfxArgs.Load( word * 4u );
#else
    return g_VfxArgs[word];
#endif
}

uint4 loadMeshRow( uint row )
{
    return g_VfxParams[SW_ROOT( g_VfxMeshParamBase ) + row];
}

/** @brief 색을 RGBA8 unorm 으로 묶습니다(r 이 아래 바이트) — C++ GPUSpriteInstanceData 의 반올림과 같다. */
uint packColor( float4 color )
{
    const uint4 bytes = uint4( saturate( color ) * 255.0f + 0.5f );
    return bytes.x | ( bytes.y << 8u ) | ( bytes.z << 16u ) | ( bytes.w << 24u );
}

SW_NUMTHREADS( SW_VFX_MESH_INSTANCE_THREAD_COUNT, 1, 1 )
void CSMain( uint3 dispatchThreadId : SV_DispatchThreadID )
{
    const uint4 header    = loadMeshRow( SW_VFX_MESH_ROW_HEADER );
    const uint  poolIndex = dispatchThreadId.x;
    if ( poolIndex >= header.x )
        return;
    const uint slot = loadMeshRow( SW_VFX_MESH_ROW_FIRST_SLOT + poolIndex / 4u )[poolIndex % 4u];
    if ( slot == SW_VFX_INVALID_SLOT )
        return; // 이번 프레임 배치에 실리지 않은 자리

    const float4 world0   = asfloat( loadMeshRow( SW_VFX_MESH_ROW_WORLD ) );
    const float4 world1   = asfloat( loadMeshRow( SW_VFX_MESH_ROW_WORLD + 1u ) );
    const float4 world2   = asfloat( loadMeshRow( SW_VFX_MESH_ROW_WORLD + 2u ) );
    const float4 world3   = asfloat( loadMeshRow( SW_VFX_MESH_ROW_WORLD + 3u ) );
    InstanceData instance = g_VfxInstancesRW[slot];
    if ( poolIndex < loadArgsWord( SW_VFX_ARGS_WORD_ALIVE ) )
    {
        const GPUParticle particle = g_VfxParticles[poolIndex];
        const float       size     = particle.positionSize.w;
        const float3      local    = particle.positionSize.xyz;
        // 크기 × 이동 × 방출기 행렬(행벡터 규약) — 0~2 행은 축에 크기를 곱한 것, 3 행은 입자 위치를 옮긴 점이다.
        instance.world[0]     = world0 * size;
        instance.world[1]     = world1 * size;
        instance.world[2]     = world2 * size;
        instance.world[3]     = local.x * world0 + local.y * world1 + local.z * world2 + world3;
        instance.boundsCenter = instance.world[3].xyz;
        instance.boundsRadius = asfloat( header.z ) * size * asfloat( header.w );
        instance.tint         = packColor( particle.color );
    }
    else
    {
        // 죽은 자리 — 크기 0 행렬(CPU 경로와 같다).
        instance.world[0]     = float4( 0.0f, 0.0f, 0.0f, 0.0f );
        instance.world[1]     = float4( 0.0f, 0.0f, 0.0f, 0.0f );
        instance.world[2]     = float4( 0.0f, 0.0f, 0.0f, 0.0f );
        instance.world[3]     = world3;
        instance.boundsCenter = world3.xyz;
        instance.boundsRadius = 0.0f;
    }
    g_VfxInstancesRW[slot] = instance;
}
