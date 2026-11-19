// hzb_build.hlsl — HZB 피라미드 짓기(컴퓨트). 디스패치 하나가 밉 하나를 짓는다: 밉 0 은 뷰의 깊이 사본(장면 깊이의 R32 사본 — 깊이 히스토리 텍스처)의
// 2 x 2 픽셀마다, 밉 m 은 밉 m-1 의 2 x 2 텍셀마다 (최소, 최대) 깊이를 같은 구조 버퍼에 쓴다(배치는 hzb.hlsli). 가장자리의 홀수 줄은 넘친 쪽을 마지막 줄로 접는다.
// 컬링(gpu_cull.hlsl)이 최대 깊이로 가림을, 화면 공간 반사가 최소 깊이로 빈 칸 건너뛰기를 한다. 16 px 밉(kTileMip)을 지을 때 되읽기용 가림 타일(밉의 최대 깊이,
// 사각형 밖 타일은 1 — 아무것도 가리지 않는다)도 함께 쓴다 — 게임 스레드의 가림 질의(OcclusionTileSnapshot)가 그 버퍼를 몇 프레임 늦게 읽는다.
// 깊이 사본은 RW 텍스처로 읽는다 — 에뮬 백엔드(DX11 · GL · WebGPU)의 컴퓨트는 샘플 텍스처를 못 읽는다(vfx_simulate.hlsl 의 깊이 충돌과 같다).
// C++: HZBOcclusion::record — 프레임 처음(지난 장면 깊이), 깊이 프리패스 뒤(2 단계 가림), 화면 공간 반사 앞(이번 장면 깊이). 밉마다 UAV 배리어를 사이에 둔다.
#define SW_RW_SLOT0_GL_FORMAT "r32f" // 슬롯 0 은 R32F 깊이 사본이다 — GL 은 묶은 포맷과 선언 포맷이 같아야 읽는다
#include "binding.hlsli"
#include "hzb.hlsli"

SW_ROOT_CONSTANTS_BEGIN
{
    uint g_HZBDepthIndex; // 깊이 사본의 RW 텍스처 번호(네이티브) · RW 슬롯 서수(에뮬 — 0)
    uint g_HZBDepthWidth; // 피라미드를 짓는 깊이 사각형(px) — 자동 렌더 배율이면 텍스처보다 작다
    uint g_HZBDepthHeight;
    uint g_HZBMip;         // 이 디스패치가 짓는 밉
    uint g_HZBTileColumns; // 가림 타일 격자(0 이면 타일을 쓰지 않는다 — 프레임 처음 짓기만 쓴다)
    uint g_HZBTileRows;
};
SW_ROOT_CONSTANTS_END

SW_DECLARE_RW_STRUCTURED_BUFFER( float2, g_HZBRW, 0 );
SW_DECLARE_RW_STRUCTURED_BUFFER( float, g_OcclusionTilesRW, 1 );

/// 텍셀 하나가 16 px 인 밉 — C++ `HZBLayout::kTileMip`.
static const uint kTileMip = 3u;

SW_NUMTHREADS( 8, 8, 1 )
void CSMain( uint3 dispatchThreadID : SV_DispatchThreadID )
{
    const uint2 texel     = dispatchThreadID.xy;
    const uint2 depthSize = uint2( SW_ROOT( g_HZBDepthWidth ), SW_ROOT( g_HZBDepthHeight ) );
    const uint  mip       = SW_ROOT( g_HZBMip );
    const uint2 size      = HZB::computeMipSize( depthSize, mip );
    const bool  bInMip    = texel.x < size.x && texel.y < size.y;
    float2      minMax    = float2( 1.0f, 0.0f );
    if ( bInMip )
    {
        if ( mip == 0u )
        {
            SW_UNROLL for ( uint corner = 0u; corner < 4u; ++corner )
            {
                const uint2 pixel = min( texel * 2u + uint2( corner & 1u, corner >> 1u ), depthSize - uint2( 1u, 1u ) );
                const float depth = Bindless::loadRwTexture2D( SW_ROOT( g_HZBDepthIndex ), pixel ).x;
                minMax            = float2( min( minMax.x, depth ), max( minMax.y, depth ) );
            }
        }
        else
        {
            const uint2 sourceSize   = HZB::computeMipSize( depthSize, mip - 1u );
            const uint  sourceOffset = HZB::computeMipOffset( depthSize, mip - 1u );
            SW_UNROLL for ( uint corner = 0u; corner < 4u; ++corner )
            {
                const uint2  source = min( texel * 2u + uint2( corner & 1u, corner >> 1u ), sourceSize - uint2( 1u, 1u ) );
                const float2 value  = g_HZBRW[sourceOffset + source.y * sourceSize.x + source.x];
                minMax              = float2( min( minMax.x, value.x ), max( minMax.y, value.y ) );
            }
        }
        g_HZBRW[HZB::computeMipOffset( depthSize, mip ) + texel.y * size.x + texel.x] = minMax;
    }
    // 타일 격자는 텍스처 크기에서, 밉은 사각형 크기에서 나온다 — 사각형 밖 타일은 아무것도 가리지 않는다(깊이 1 — 컬링 자리표와 같은 값).
    const bool bTile = mip == kTileMip && texel.x < SW_ROOT( g_HZBTileColumns ) && texel.y < SW_ROOT( g_HZBTileRows );
    if ( bTile )
        g_OcclusionTilesRW[texel.y * SW_ROOT( g_HZBTileColumns ) + texel.x] = bInMip ? minMax.y : 1.0f;
}
