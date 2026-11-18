/**
 * deferred_tile_classify.hlsl — 디퍼드 조명 타일 분류(컴퓨트, RenderPassType::DeferredTileClassify — 언리얼 Substrate 타일 분류 · 유니티 HDRP 디퍼드 타일).
 *
 * 그룹 하나가 타일 하나(SW_DEFERRED_TILE_SIZE² px)다. 픽셀마다 G버퍼 알베도 알파의 셰이딩 모델 번호를 그룹 공유 비트에 OR 하고, 첫 스레드가 비트로 종류를 골라
 * (deferred_tile_class.hlsli) 그 종류의 간접 인자 인스턴스 수를 하나 올려 받은 자리에 타일 좌표(x | y << 16)를 적는다 — 그리지 않은 타일(번호가 모두 NONE)은 어느 목록에도 들지 않는다.
 * 조명 패스가 종류마다 drawIndirect 한 번으로 그 목록의 타일 사각형만 그린다(deferred_lighting.hlsl VSTile).
 * 간접 인자는 같은 PSO 의 지우기 디스패치(g_TileMode = 0, 그룹 하나)가 먼저 초기화한다(gpu_cull 의 되돌리기 커널과 같은 모양).
 *
 * 네이티브 bindless(DX12 · Vulkan) 전용이다 — 에뮬 백엔드(DX11 · GL · WebGPU)의 컴퓨트는 샘플 텍스처를 읽지 못해 C++ 이 PSO 를 만들지 않고(kRequiresNativeBindless)
 * 조명은 풀스크린 한 장(모든 엽)으로 그린다. 쿠킹이 모든 대상으로 컴파일해도 서도록 에뮬 몸통은 비워 둔다.
 * C++: FramePassExecutor::executeTiledLighting — 조명 패스의 렌더 패스를 열기 전.
 */
#include "binding.hlsli"
#include "deferred_tile_class.hlsli"

SW_ROOT_CONSTANTS_BEGIN
{
    uint g_TileAlbedoIndex; // G버퍼 알베도의 bindless SRV 번호
    uint g_TileWidth;       // 뷰 사각형 크기(px) — 자동 렌더 배율이면 첨부보다 작다
    uint g_TileHeight;
    uint g_TileCapacity; // 종류 하나의 타일 목록 칸 수(풀 크기의 타일 수) — 종류 c 의 목록은 c × 칸 수에서 시작한다
    uint g_TileMode;     // 0 간접 인자 지우기 · 1 분류
};
SW_ROOT_CONSTANTS_END

/** @brief 종류 하나의 간접 그리기 인자 — C++ RHIDrawIndirectCommand 와 같은 배치. */
struct DeferredTileDrawArgs
{
    uint _vertexCount;
    uint _instanceCount;
    uint _startVertex;
    uint _startInstance;
};

SW_DECLARE_RW_STRUCTURED_BUFFER( DeferredTileDrawArgs, g_DeferredTileArgsRW, 0 );
SW_DECLARE_RW_STRUCTURED_BUFFER( uint, g_DeferredTileListRW, 1 );

groupshared uint gs_TileModelMask;

static const uint kDeferredTileModeClear = 0u;

#if defined( SW_NATIVE_BINDLESS )
/** @brief 타일의 번호 비트로 종류를 고른다 — SIMPLE 이 받는 번호만 있으면 SIMPLE, 아니면 COMPLEX. */
uint selectTileClass( uint modelMask )
{
    return ( modelMask & ~SW_DEFERRED_TILE_SIMPLE_MODELS ) == 0u ? SW_DEFERRED_TILE_CLASS_SIMPLE : SW_DEFERRED_TILE_CLASS_COMPLEX;
}

SW_NUMTHREADS( SW_DEFERRED_TILE_SIZE, SW_DEFERRED_TILE_SIZE, 1 )
void CSMain( uint3 groupID : SV_GroupID, uint3 dispatchThreadID : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex )
{
    if ( SW_ROOT( g_TileMode ) == kDeferredTileModeClear )
    {
        if ( groupIndex < SW_DEFERRED_TILE_CLASS_COUNT )
        {
            DeferredTileDrawArgs args;
            args._vertexCount                = SW_DEFERRED_TILE_VERTEX_COUNT;
            args._instanceCount              = 0u;
            args._startVertex                = 0u;
            args._startInstance              = 0u;
            g_DeferredTileArgsRW[groupIndex] = args;
        }
        return;
    }
    if ( groupIndex == 0u )
        gs_TileModelMask = 0u;
    GroupMemoryBarrierWithGroupSync();
    const bool bInside = dispatchThreadID.x < SW_ROOT( g_TileWidth ) && dispatchThreadID.y < SW_ROOT( g_TileHeight );
    if ( bInside )
    {
        const uint shadingModel = SurfaceOutput::decodeShadingModel( Bindless::loadEngineTexel( SW_ROOT( g_TileAlbedoIndex ), 0u, int2( dispatchThreadID.xy ) ).a );
        if ( shadingModel != SW_SHADING_MODEL_ID_NONE )
            InterlockedOr( gs_TileModelMask, 1u << min( shadingModel, 31u ) );
    }
    GroupMemoryBarrierWithGroupSync();
    if ( groupIndex != 0u || gs_TileModelMask == 0u )
        return;
    const uint tileClass = selectTileClass( gs_TileModelMask );
    uint       slot      = 0u;
    InterlockedAdd( g_DeferredTileArgsRW[tileClass]._instanceCount, 1u, slot );
    g_DeferredTileListRW[tileClass * SW_ROOT( g_TileCapacity ) + slot] = groupID.x | ( groupID.y << 16u );
}
#else
SW_NUMTHREADS( SW_DEFERRED_TILE_SIZE, SW_DEFERRED_TILE_SIZE, 1 )
void CSMain( uint3 groupID : SV_GroupID )
{
    // 에뮬 백엔드는 이 디스패치를 쓰지 않는다(머리말) — 컴퓨트에서 샘플 텍스처를 읽을 길이 없다.
}
#endif
