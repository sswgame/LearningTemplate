#include "common.hlsli"
#include "instance_data.hlsli"
#include "vfx_simulate.hlsli"

/**
 * vfx_mesh_instance.hlsl — GPU 메시 입자의 인스턴스 쓰기 (컴퓨트). 시뮬레이션(vfx_simulate.hlsl)이 쓴 렌더 입력을 메시 인스턴스 배치의 인스턴스 칸으로 옮긴다
 * (언리얼 Niagara 메시 렌더러가 GPU 시뮬레이션의 입자를 인스턴스 데이터로 읽는 자리).
 *
 * 스레드 하나 = 입자 인스턴스 칸(0 ~ 풀 - 1) 하나. 스레드 t 가 살아 있는 수보다 작으면 렌더 입력 t 의 입자이고 그 칸은 인자 버퍼의 살아 있는 칸 목록 t 번째,
 * 아니면 빈 칸 스택의 (t - 살아 있는 수) 번째 칸이다 — 살아 있는 수 + 스택 깊이 = 풀이라 칸마다 한 번씩 쓴다(vfx_simulate.hlsl 의 dead list).
 * 입자 칸은 수명 내내 그대로라 GPUScene 칸도 입자를 따라간다. 입자 칸 → GPUScene 칸은 GT 가 그 프레임의 배치 구성에서 찾아 실었다(`GPUSceneBuilder::appendVFXMeshEmitter` — 불투명 구간만).
 * 살아 있는 칸은 C++ `VFXComponent::writeMeshEntries` 와 같은 식(크기 × 입자 위치 이동 × 방출기 행렬, 바운드 = 메시 반지름 × 크기 × 방출기 배율, 색 RGBA8)이다.
 * 빈 칸은 kInstanceFlagEmpty 를 켠다 — 컬링(gpu_cull.hlsl)이 빼서 간접 인자의 인스턴스 수가 살아 있는(보이는) 수다(언리얼 Niagara · 유니티 VFX Graph 처럼 CPU 는 수를 모른다).
 * 행렬 · 바운드 · 색 · 플래그만 바꾸고 나머지 칸(배치 번호 · 머티리얼 · 블렌드 · 스프라이트 UV)은 CPU 가 올린 그대로 둔다.
 * 인스턴스 애니메이션과 컬링 **앞에** 돈다 — 컬링이 이 바운드 · 플래그로 거르고 간접 인자를 만든다.
 *
 * 바인딩 계약: 읽기 t0 매개변수(g_VFXParams), t1 렌더 입력(g_VFXParticles), t2 간접 인자(살아 있는 수 · 빈 칸 스택 · 살아 있는 칸 목록, DX11 은 RAW), 쓰기 u0 인스턴스(g_VFXInstancesRW).
 * 루트 상수 g_VFXMeshParamBase. C++: VFXGPUSimulation::recordDispatches — 메시 방출기마다 dispatchCompute( 풀 / 64, 1, 1 ).
 */

struct GPUParticle
{
    float4 _positionSize;
    float4 _color;
    float4 _rotationLife;
};

SW_DECLARE_STRUCTURED_BUFFER( uint4, g_VFXParams, 0 );
SW_DECLARE_STRUCTURED_BUFFER( GPUParticle, g_VFXParticles, 1 );
#if defined( DX11 )
SW_DECLARE_BYTE_ADDRESS_BUFFER( g_VFXArgs, 2 );
#else
SW_DECLARE_STRUCTURED_BUFFER( uint, g_VFXArgs, 2 );
#endif
SW_DECLARE_RW_STRUCTURED_BUFFER( InstanceData, g_VFXInstancesRW, 0 );

SW_ROOT_CONSTANTS_BEGIN
{
    uint g_VFXMeshParamBase; // 이 방출기 메시 구간의 첫 행
};
SW_ROOT_CONSTANTS_END

uint loadArgsWord( uint word )
{
#if defined( DX11 )
    return g_VFXArgs.Load( word * 4u );
#else
    return g_VFXArgs[word];
#endif
}

uint4 loadMeshRow( uint row )
{
    return g_VFXParams[SW_ROOT( g_VFXMeshParamBase ) + row];
}

/** @brief 색을 RGBA8 unorm 으로 묶습니다(r 이 아래 바이트) — C++ GPUSpriteInstanceData 의 반올림과 같다. */
uint packColor( float4 color )
{
    const uint4 bytes = uint4( saturate( color ) * 255.0f + 0.5f );
    return bytes.x | ( bytes.y << 8u ) | ( bytes.z << 16u ) | ( bytes.w << 24u );
}

SW_NUMTHREADS( SW_VFX_MESH_INSTANCE_THREAD_COUNT, 1, 1 )
void CSMain( uint3 dispatchThreadID : SV_DispatchThreadID )
{
    const uint4 header   = loadMeshRow( SW_VFX_MESH_ROW_HEADER );
    const uint  poolSize = header.x;
    const uint  thread   = dispatchThreadID.x;
    if ( thread >= poolSize )
        return;
    const uint aliveCount = loadArgsWord( SW_VFX_ARGS_WORD_ALIVE );
    const bool bAlive     = thread < aliveCount;
    const uint poolSlot   = bAlive ? loadArgsWord( SW_VFX_ARGS_WORD_ALIVE_SLOT_FIRST( poolSize ) + thread ) : loadArgsWord( SW_VFX_ARGS_WORD_DEAD_FIRST + thread - aliveCount );
    if ( poolSlot >= poolSize )
        return; // 칸은 풀 안이다(vfx_simulate.hlsl 이 재생 시작에 스택을 채운다) — 넘으면 쓰지 않는다
    const uint slot = loadMeshRow( SW_VFX_MESH_ROW_FIRST_SLOT + poolSlot / 4u )[poolSlot % 4u];
    if ( slot == SW_VFX_INVALID_SLOT )
        return; // 이번 프레임 배치에 실리지 않은 칸

    const float4 world0   = asfloat( loadMeshRow( SW_VFX_MESH_ROW_WORLD ) );
    const float4 world1   = asfloat( loadMeshRow( SW_VFX_MESH_ROW_WORLD + 1u ) );
    const float4 world2   = asfloat( loadMeshRow( SW_VFX_MESH_ROW_WORLD + 2u ) );
    const float4 world3   = asfloat( loadMeshRow( SW_VFX_MESH_ROW_WORLD + 3u ) );
    InstanceData instance = g_VFXInstancesRW[slot];
    if ( bAlive )
    {
        const GPUParticle particle = g_VFXParticles[thread];
        const float       size     = particle._positionSize.w;
        const float3      local    = particle._positionSize.xyz;
        // 크기 × 이동 × 방출기 행렬(행벡터 규약) — 0~2 행은 축에 크기를 곱한 것, 3 행은 입자 위치를 옮긴 점이다.
        instance._world[0]     = world0 * size;
        instance._world[1]     = world1 * size;
        instance._world[2]     = world2 * size;
        instance._world[3]     = local.x * world0 + local.y * world1 + local.z * world2 + world3;
        instance._boundsCenter = instance._world[3].xyz;
        instance._boundsRadius = asfloat( header.z ) * size * asfloat( header.w );
        instance._tint         = packColor( particle._color );
        instance._instanceFlags &= ~kInstanceFlagEmpty;
    }
    else
    {
        // 빈 칸 — 컬링이 플래그로 뺀다. 크기 0 행렬도 두는 것은 컬링 없는 길(DX11 은 간접 인자를 구조 버퍼로 못 써 `_bGPUCulling` 이 0, 진단 gv_gpuCulling=0)이 칸을 모두 그리기 때문이다.
        instance._world[0]     = float4( 0.0f, 0.0f, 0.0f, 0.0f );
        instance._world[1]     = float4( 0.0f, 0.0f, 0.0f, 0.0f );
        instance._world[2]     = float4( 0.0f, 0.0f, 0.0f, 0.0f );
        instance._world[3]     = world3;
        instance._boundsCenter = world3.xyz;
        instance._boundsRadius = 0.0f;
        instance._instanceFlags |= kInstanceFlagEmpty;
    }
    g_VFXInstancesRW[slot] = instance;
}
