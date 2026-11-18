// motion_blur_tile_max.hlsl — 모션 블러 타일 최대 속도(TileMax). 출력 픽셀 하나가 화면 kMotionBlurTileSize² 픽셀 타일 하나이고, 그 타일 안 화면 속도 중 가장 긴 것을 낸다.
// McGuire 외 2012 "A Reconstruction Filter for Plausible Motion Blur" 의 TileMax 이고, 언리얼 VelocityFlatten · 유니티 HDRP Motion Blur 의 TileMax 와 같은 자리다.
// 그리지 않은 픽셀(하늘, 깊이 1)은 속도 출력이 없어 깊이로 카메라 재투영 속도를 구한다(motion_blur.hlsl 과 같은 규칙). 길이는 한 프레임 흐림 상한으로 자른다.
// 출력 첨부(VelocityTileMax)는 파이프라인 XML 이 _resolutionDivisor 로 kMotionBlurTileSize 분의 1 크기로 만든다 — 두 값은 같아야 한다.
#include "binding.hlsli"
#include "fullscreen.hlsli"

static const int   kMotionBlurTileSize = 16;    // VelocityTileMax 의 _resolutionDivisor 와 같다
static const float kMaxBlurUV          = 0.05f; // motion_blur.hlsl 과 같다 — 한 프레임 흐림 길이의 상한(화면 폭 비율)
static const float kSkyDepth           = 1.0f;  // 깊이 클리어 값 — 아무것도 그리지 않은 픽셀

/** @brief 화면 픽셀 @p texel(풀 좌표)의 화면 속도입니다 — 하늘은 카메라 재투영, 그 밖은 속도 첨부. 상한으로 자른다. */
float2 loadPixelVelocity( int2 texel )
{
    const float  depth  = Bindless::loadEngineTexel( SW_ENGINE_ROLE( SceneDepth ), texel ).r;
    const float2 viewUv = ( float2( texel ) + 0.5f ) * g_ViewRectSize.zw / max( g_ViewRectScale.xy, 1e-5f );
    float        previousClipW;
    const float2 prevUv   = Transform::computeCameraPreviousUv( viewUv, depth, previousClipW );
    const float2 velocity = depth >= kSkyDepth ? viewUv - prevUv : Bindless::loadEngineTexel( SW_ENGINE_ROLE( Velocity ), texel ).rg;
    const float  speed    = length( velocity );
    return velocity * ( ( speed > kMaxBlurUV ) ? ( kMaxBlurUV / speed ) : 1.0f );
}

float4 PSMain( PSInput input ) : SV_TARGET
{
    const int2 tileOrigin  = int2( input._position.xy ) * kMotionBlurTileSize;
    const int2 rectEnd     = int2( g_ViewRectSize.xy ) - 1; // 사각형 밖(풀의 남는 자리)은 읽지 않는다
    float2     maxVelocity = float2( 0.0f, 0.0f );
    float      maxSpeedSq  = 0.0f;
    for ( int row = 0; row < kMotionBlurTileSize; ++row )
    {
        for ( int column = 0; column < kMotionBlurTileSize; ++column )
        {
            const int2   texel    = min( tileOrigin + int2( column, row ), rectEnd );
            const float2 velocity = loadPixelVelocity( texel );
            const float  speedSq  = dot( velocity, velocity );
            const bool   bLonger  = speedSq > maxSpeedSq;
            maxVelocity           = bLonger ? velocity : maxVelocity;
            maxSpeedSq            = bLonger ? speedSq : maxSpeedSq;
        }
    }
    return float4( maxVelocity, 0.0f, 0.0f );
}
