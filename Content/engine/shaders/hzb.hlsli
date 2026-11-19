/**
 * hzb.hlsli — HZB 피라미드(뷰마다 구조 버퍼 float2 하나: x 최소 깊이 · y 최대 깊이)의 배치 식. 짓는 쪽(hzb_build.hlsl)과 읽는 쪽(gpu_cull.hlsl 의 가림 판정 ·
 * screen_space_reflection.hlsl 의 hi-z trace)이 이 파일 하나를 쓴다. C++ `HZBLayout`(Engine/Renderer/Frame/View/HZBPyramid.h)과 같은 식이다(바꾸면 두 곳을 함께 고친다).
 *
 * 밉 0 은 깊이 사각형 2 x 2 픽셀마다 하나, 밉 m 의 크기는 ceil( 깊이 / 2^(m+1) )(최소 1)이고 밉은 0 부터 차례로 버퍼에 붙는다. 깊이 픽셀 p 는 밉 m 의 텍셀 p >> (m+1) 에 든다.
 * 버퍼 배치라 밉 시작 원소는 앞 밉들의 합이다(밉 수가 열여섯 안이라 고리 하나로 센다 — 상수로 싣지 않아 C++ 와 맞출 표가 하나 줄어든다).
 */

#ifndef SW_ENGINE_HZB_HLSLI
#define SW_ENGINE_HZB_HLSLI

struct HZB
{
    static uint2 computeMipSize( uint2 depthSize, uint mip );
    static uint computeMipOffset( uint2 depthSize, uint mip );
    static uint2 toPixel( float2 uv, uint2 depthSize );
    static uint selectMip( uint2 firstPixel, uint2 lastPixel, uint footprint, uint mipCount );
};

/** @brief 깊이 사각형 @p depthSize 의 밉 @p mip 크기입니다 — ceil( 깊이 / 2^(mip+1) ), 최소 1(C++ `HZBLayout::computeMipExtent`). */
static uint2 HZB::computeMipSize( uint2 depthSize, uint mip )
{
    const uint shift = mip + 1u;
    return max( ( depthSize + ( ( 1u << shift ) - 1u ) ) >> shift, uint2( 1u, 1u ) );
}

/** @brief 밉 @p mip 의 버퍼 안 첫 원소입니다(앞 밉들의 원소 수 합 — C++ `HZBLayout::Mip::_offset`). */
static uint HZB::computeMipOffset( uint2 depthSize, uint mip )
{
    uint offset = 0u;
    for ( uint level = 0u; level < mip; ++level )
    {
        const uint  shift = level + 1u;
        const uint2 size  = max( ( depthSize + ( ( 1u << shift ) - 1u ) ) >> shift, uint2( 1u, 1u ) );
        offset += size.x * size.y;
    }
    return offset;
}

/** @brief 화면 UV 를 깊이 픽셀로 옮깁니다(0 ~ 깊이 - 1 로 자른다 — C++ `HZBPyramidInternal::toPixel`). */
static uint2 HZB::toPixel( float2 uv, uint2 depthSize )
{
    return min( uint2( saturate( uv ) * float2( depthSize ) ), depthSize - uint2( 1u, 1u ) );
}

/**
 * @brief 깊이 픽셀 사각형(@p firstPixel ~ @p lastPixel)을 한 축 @p footprint 텍셀 안에 덮는 가장 고운 밉입니다(C++ `HZBLayout::selectMip`).
 * @return 맞는 밉이 없으면 @p mipCount — 부르는 쪽은 "보인다" 로 본다.
 */
static uint HZB::selectMip( uint2 firstPixel, uint2 lastPixel, uint footprint, uint mipCount )
{
    uint found = mipCount;
    for ( uint mip = 0u; mip < mipCount; ++mip )
    {
        const uint  shift = mip + 1u;
        const uint2 span  = ( lastPixel >> shift ) - ( firstPixel >> shift ) + uint2( 1u, 1u );
        found             = ( found == mipCount && span.x <= footprint && span.y <= footprint ) ? mip : found;
    }
    return found;
}

#endif // SW_ENGINE_HZB_HLSLI
