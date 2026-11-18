// motion_blur_neighbor_max.hlsl — 모션 블러 이웃 최대 속도(NeighborMax). 타일마다 자기와 이웃 여덟 타일의 TileMax 중 가장 긴 속도를 낸다 —
// 빠른 물체의 흐림이 자기 타일 밖(윤곽 바깥)으로 번질 수 있게 한다. McGuire 외 2012 의 NeighborMax, 언리얼 · 유니티 HDRP 모션 블러의 같은 단계다.
// 입력(VelocityTileMax)은 원본 역할(SourceColor)로 걸리고, 출력(VelocityNeighborMax)은 같은 타일 크기다. 모션 블러(motion_blur.hlsl)가 그 방향으로 흐린다.
#include "binding.hlsli"
#include "fullscreen.hlsli"

float4 PSMain( PSInput input ) : SV_TARGET
{
    const int2 tile        = int2( input._position.xy );
    const int2 tileEnd     = int2( Bindless::getEngineTextureSize( SW_ENGINE_ROLE( SourceColor ) ) ) - 1;
    float2     maxVelocity = float2( 0.0f, 0.0f );
    float      maxSpeedSq  = 0.0f;
    SW_UNROLL for ( int row = -1; row <= 1; ++row )
    {
        SW_UNROLL for ( int column = -1; column <= 1; ++column )
        {
            const int2   neighbor = clamp( tile + int2( column, row ), int2( 0, 0 ), tileEnd );
            const float2 velocity = Bindless::loadEngineTexel( SW_ENGINE_ROLE( SourceColor ), neighbor ).rg;
            const float  speedSq  = dot( velocity, velocity );
            const bool   bLonger  = speedSq > maxSpeedSq;
            maxVelocity           = bLonger ? velocity : maxVelocity;
            maxSpeedSq            = bLonger ? speedSq : maxSpeedSq;
        }
    }
    return float4( maxVelocity, 0.0f, 0.0f );
}
