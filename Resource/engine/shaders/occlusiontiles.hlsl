// occlusiontiles.hlsl — 가림 타일(컴퓨트). 뷰의 깊이 히스토리(지난 프레임 장면 깊이의 R32 사본)를 타일(한 변 g_OcclusionTileSize px)마다 최대 깊이 하나로 줄여
// 구조 버퍼에 쓴다. 컬링(gpucull.hlsl 의 가림 판정)이 경계 구를 그 프레임의 뷰-투영으로 옮겨 덮는 타일의 최대 깊이보다 멀면 그리지 않는다.
// 스레드 하나가 타일 하나를 맡아 타일의 텍셀을 모두 읽는다(16 x 16 = 256 번). 화면 끝의 남는 타일은 깊이 안쪽 텍셀만 본다.
// 깊이 히스토리는 RW 텍스처로 읽는다 — 에뮬 백엔드(DX11 · GL)의 컴퓨트는 샘플 텍스처를 못 읽는다(vfxsimulate.hlsl 의 깊이 충돌과 같다).
// C++: FrameRenderer::recordOcclusionTiles — 깊이 히스토리를 옮긴 직후, 컬링 앞의 컴퓨트 프리패스.
#include "binding.hlsli"

SW_ROOT_CONSTANTS_BEGIN
{
    uint g_OcclusionDepthIndex; // 깊이 히스토리의 RW 텍스처 번호(네이티브) · RW 슬롯 서수(에뮬 — 0)
    uint g_OcclusionDepthWidth; // 깊이 히스토리 크기(px)
    uint g_OcclusionDepthHeight;
    uint g_OcclusionColumnCount; // 타일 격자
    uint g_OcclusionRowCount;
    uint g_OcclusionTileSize; // 타일 한 변(px)
};
SW_ROOT_CONSTANTS_END

SW_DECLARE_RW_STRUCTURED_BUFFER( float, g_OcclusionTilesRW, 0 );

SW_NUMTHREADS( 8, 8, 1 )
void CSMain( uint3 dispatchThreadId : SV_DispatchThreadID )
{
    const uint column = dispatchThreadId.x;
    const uint row    = dispatchThreadId.y;
    if ( column >= SW_ROOT( g_OcclusionColumnCount ) || row >= SW_ROOT( g_OcclusionRowCount ) )
        return;
    const uint2 first    = uint2( column, row ) * SW_ROOT( g_OcclusionTileSize );
    const uint2 last     = min( first + SW_ROOT( g_OcclusionTileSize ), uint2( SW_ROOT( g_OcclusionDepthWidth ), SW_ROOT( g_OcclusionDepthHeight ) ) );
    float       maxDepth = 0.0f;
    for ( uint texelY = first.y; texelY < last.y; ++texelY )
    {
        for ( uint texelX = first.x; texelX < last.x; ++texelX )
        {
            maxDepth = max( maxDepth, Bindless::loadRwTexture2D( SW_ROOT( g_OcclusionDepthIndex ), uint2( texelX, texelY ) ).x );
        }
    }
    g_OcclusionTilesRW[row * SW_ROOT( g_OcclusionColumnCount ) + column] = maxDepth;
}
