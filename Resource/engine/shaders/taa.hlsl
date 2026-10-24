// taa.hlsl — 시간 누적(TAA) 해석. 이번 원본(SourceColor)과 지난 출력(기록 — C++ 가 g_GBufferAlbedo 슬롯에 바인딩한다)을 섞는다.
// 기록은 이 픽셀의 깊이로 월드 위치를 복원해 지난 프레임 뷰-투영(g_PrevViewProj)으로 다시 투영한 자리에서 읽고(재투영 — motionblur.hlsl 과 같은 식),
// 이번 원본의 3x3 이웃 최소 · 최대로 자른다(이웃 클램프 — 언리얼 TAA 의 히스토리 클램프 · 유니티 URP TAA 와 같은 갈래). 그래서 카메라가 움직여도 앞 그림이
// 겹치지 않고, 속도 버퍼가 없는 움직이는 물체의 기록도 클램프가 잘라 낸다. 재투영한 자리가 화면 밖이면 이번 원본만 쓴다.
// 컷 · 후처리를 끈 뷰는 C++ 가 기록 슬롯에 이번 원본을 바인딩한다(섞어도 이번 그림만 남는다).
#include "binding.hlsli"

static const float kHistoryWeight = 0.9f; // 기록 비율 — 화면이 그대로인 곳의 누적 정도

struct PSInput
{
    float4 _position : SV_POSITION;
    float2 _uv : TEXCOORD0;
};

PSInput VSMain( VertexInput input, uint vertexId : SV_VertexID )
{
    PSInput output;
    input._position     = input._position;
    float2 clipPosition = float2( ( vertexId == 1 ) ? 3.0f : -1.0f, ( vertexId == 2 ) ? 3.0f : -1.0f );
    output._position    = float4( clipPosition, 0.0f, 1.0f );
    output._uv          = clipPosition * float2( 0.5f, -0.5f ) + 0.5f;
    return output;
}

float4 PSMain( PSInput input ) : SV_TARGET
{
    const float3 current = PassTexture::sampleSourcePoint( input._uv ).rgb;

    // 이웃 범위 — 기록이 이번 그림에 없는 색(앞 그림 · 움직인 물체)이면 이 안으로 잘린다.
    float3 neighborMin = current;
    float3 neighborMax = current;
    SW_UNROLL for ( int offsetY = -1; offsetY <= 1; ++offsetY )
    {
        SW_UNROLL for ( int offsetX = -1; offsetX <= 1; ++offsetX )
        {
            const float3 neighbor = PassTexture::sampleSourcePoint( input._uv + float2( offsetX, offsetY ) * g_SourceTexel.xy ).rgb;
            neighborMin           = min( neighborMin, neighbor );
            neighborMax           = max( neighborMax, neighbor );
        }
    }

    // 재투영 — 이 픽셀이 지난 프레임에 있던 화면 자리.
    const float  depth     = PassTexture::sampleDepthPoint( input._uv ).r;
    const float2 ndcXy     = input._uv * float2( 2.0f, -2.0f ) + float2( -1.0f, 1.0f );
    const float4 world     = mul( float4( ndcXy, depth, 1.0f ), g_InvViewProj );
    const float4 prevClip  = mul( float4( world.xyz / world.w, 1.0f ), g_PrevViewProj );
    const float2 prevNdc   = prevClip.xy / max( abs( prevClip.w ), 1e-5f ) * sign( prevClip.w );
    const float2 prevUv    = prevNdc * float2( 0.5f, -0.5f ) + 0.5f;
    const bool   bOnScreen = prevClip.w > 0.0f && all( prevUv >= 0.0f ) && all( prevUv <= 1.0f );

    const float3 history = clamp( PassTexture::sampleAlbedo( prevUv ).rgb, neighborMin, neighborMax );
    // 고르기는 early-return 이 아니라 값 선택이다(GL 드라이버가 early-return 모양을 잘못 컴파일한 적이 있다 — motionblur.hlsl).
    const float weight = bOnScreen ? kHistoryWeight : 0.0f;
    return float4( lerp( current, history, weight ), 1.0f );
}
