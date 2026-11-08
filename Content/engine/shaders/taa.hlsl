// taa.hlsl — 시간 누적(TAA) 해석. 이번 원본(SourceColor)과 지난 출력(기록 — C++ 가 g_GBufferAlbedo 슬롯에 바인딩한다)을 섞는다.
// 기록은 이 픽셀이 지난 프레임에 있던 화면 자리에서 읽는다(재투영). 그 자리는 불투명 지오메트리 패스가 쓴 화면 속도(SceneVelocity — 물체마다 지난 월드 행렬 · 스킨 위치,
// binding_output.hlsli)로 찾는다 — 카메라에 붙어 함께 움직이는 물체(1인칭 총 · 캐릭터)는 속도가 0 에 가까워 기록이 제자리에 있다. 속도는 3x3 에서 가장 가까운 깊이의 것을 쓴다
// (가장자리 늘이기 — 언리얼 TAA 의 velocity dilation · 유니티 URP TAA 와 같은 갈래). 그리지 않은 픽셀(하늘, 깊이 1)은 속도가 없어 깊이를 지난 뷰-투영(g_PrevViewProj)으로 다시
// 투영한다(motion_blur.hlsl 과 같은 식). 기록은 이번 원본의 3x3 이웃 최소 · 최대로 자른다(이웃 클램프 — 가려졌다 드러난 자리의 앞 그림을 잘라 낸다).
// 재투영한 자리가 화면 밖이면 이번 원본만 쓴다. 컷 · 후처리를 끈 뷰는 C++ 가 기록 슬롯에 이번 원본을 바인딩한다(섞어도 이번 그림만 남는다).
#include "binding.hlsli"
#include "fullscreen.hlsli"

static const float kHistoryWeight = 0.9f; // 기록 비율 — 화면이 그대로인 곳의 누적 정도
static const float kSkyDepth      = 1.0f; // 깊이 클리어 값 — 아무것도 그리지 않은 픽셀(속도 출력이 없다)

/** @brief 깊이로 복원한 월드 위치를 지난 뷰-투영으로 옮긴 화면 자리입니다(카메라 움직임만). @p outPrevW 는 지난 클립 w(0 이하면 카메라 뒤)입니다. */
float2 computeCameraPreviousUv( float2 uv, float depth, out float outPrevW )
{
    const float2 ndcXy    = uv * float2( 2.0f, -2.0f ) + float2( -1.0f, 1.0f );
    const float4 world    = mul( float4( ndcXy, depth, 1.0f ), g_InvViewProj );
    const float4 prevClip = mul( float4( world.xyz / world.w, 1.0f ), g_PrevViewProj );
    const float2 prevNdc  = prevClip.xy / max( abs( prevClip.w ), 1e-5f ) * sign( prevClip.w );
    outPrevW              = prevClip.w;
    return prevNdc * float2( 0.5f, -0.5f ) + 0.5f;
}

float4 PSMain( PSInput input ) : SV_TARGET
{
    const float3 current = PassTexture::sampleSourcePoint( input._uv ).rgb;

    // 이웃 범위 — 기록이 이번 그림에 없는 색(앞 그림 · 가려졌던 자리)이면 이 안으로 잘린다. 같은 3x3 에서 가장 가까운 깊이의 자리도 찾는다(속도 늘이기).
    float3 neighborMin   = current;
    float3 neighborMax   = current;
    float  closestDepth  = PassTexture::sampleDepthPoint( input._uv ).r;
    float2 closestOffset = float2( 0.0f, 0.0f );
    SW_UNROLL for ( int offsetY = -1; offsetY <= 1; ++offsetY )
    {
        SW_UNROLL for ( int offsetX = -1; offsetX <= 1; ++offsetX )
        {
            const float2 offset   = float2( offsetX, offsetY ) * g_SourceTexel.xy;
            const float3 neighbor = PassTexture::sampleSourcePoint( input._uv + offset ).rgb;
            const float  depth    = PassTexture::sampleDepthPoint( input._uv + offset ).r;
            const bool   bCloser  = depth < closestDepth;
            neighborMin           = min( neighborMin, neighbor );
            neighborMax           = max( neighborMax, neighbor );
            closestDepth          = bCloser ? depth : closestDepth;
            closestOffset         = bCloser ? offset : closestOffset;
        }
    }

    // 재투영 — 이 픽셀이 지난 프레임에 있던 화면 자리. 그린 픽셀은 화면 속도, 하늘은 카메라 재투영이다(고르기는 값 선택 — early-return 없음).
    float        prevW;
    const float  centerDepth = PassTexture::sampleDepthPoint( input._uv ).r;
    const float2 cameraUv    = computeCameraPreviousUv( input._uv, centerDepth, prevW );
    const float2 velocity    = PassTexture::sampleVelocityPoint( input._uv + closestOffset );
    const bool   bSky        = closestDepth >= kSkyDepth;
    const float2 prevUv      = bSky ? cameraUv : input._uv - velocity;
    const bool   bInFront    = bSky == false || prevW > 0.0f;
    const bool   bOnScreen   = bInFront && all( prevUv >= 0.0f ) && all( prevUv <= 1.0f );

    const float3 history = clamp( PassTexture::sampleAlbedo( prevUv ).rgb, neighborMin, neighborMax );
    // 고르기는 early-return 이 아니라 값 선택이다(GL 드라이버가 early-return 모양을 잘못 컴파일한 적이 있다 — motion_blur.hlsl).
    const float weight = bOnScreen ? kHistoryWeight : 0.0f;
    return float4( lerp( current, history, weight ), 1.0f );
}
