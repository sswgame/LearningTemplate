/**
 * @file highlight_outline.hlsl
 * @brief 인스턴스 강조 외곽선(상호작용 대상 · 감각 모드)입니다. 뒤집은 껍질(inverted hull) — 정점을 화면 공간 노멀 방향으로 밀어 앞면을 컬링해 그린다.
 * @details 머티리얼과 무관한 엔진 셰이더다. 강조는 인스턴스의 `highlight` 칸(instance_data.hlsli, C++ `GPUInstanceHighlight`)이 정하고,
 *          이 패스의 방식과 다른 인스턴스는 정점을 클립 공간 밖 한 점으로 모아 그리지 않는다(shadow_depth.hlsl 과 같은 방법).
 *          - `SW_PASS_HIGHLIGHT_SENSE` 가 없으면 외곽선(Outline) 인스턴스만 깊이 테스트로 그린다 — 보이는 곳에만.
 *          - 있으면 감각(Sense) 인스턴스만 깊이 테스트 없이 그린다 — 벽 너머에서도 보인다(패스가 깊이 테스트를 끈다).
 *          두께는 화면 높이 비율이고 파이프라인 XML 의 패스 퍼뮤테이션 `SW_HIGHLIGHT_WIDTH=<값>` 이 정한다(데이터가 정본 — 없으면 컴파일 오류).
 *          정점은 모프 · 스키닝 결과를 읽으므로(MorphPool::loadVertex) 움직이는 캐릭터에서도 껍질이 따라간다.
 */
// 이 패스는 배치의 퍼뮤테이션 변형을 받지 않는 엔진 PSO 라 등장 디더(binding.hlsli 8-1)를 늘 켠다 — LOD 크로스페이드 중인 인스턴스(두 단계가 상보 디더로 겹친다)의 외곽선도 같은 픽셀만 그려야
// 두 단계 껍질이 겹쳐 두꺼워지지 않는다. 등장 비율 1 인 인스턴스는 픽셀을 버리지 않는다.
#if !defined( SW_APPEARANCE_DITHER )
    #define SW_APPEARANCE_DITHER 1
#endif
#include "binding.hlsli"

#if !defined( SW_HIGHLIGHT_WIDTH )
    #error "highlight_outline.hlsl needs SW_HIGHLIGHT_WIDTH=<screen height fraction> from the render pipeline pass permutation"
#endif

#if defined( SW_PASS_HIGHLIGHT_SENSE )
static const uint kHighlightPassStyle = kHighlightStyleSense;
#else
static const uint kHighlightPassStyle = kHighlightStyleOutline;
#endif

struct PSInput
{
    float4                 _position : SV_POSITION;
    nointerpolation float3 _color : COLOR;
    SW_APPEARANCE_VARYING
};

PSInput VSMain( VertexInput input, uint vertexID : SV_VertexID )
{
    PSInput      output;
    InstanceData instance = InstanceData::load( input._instanceSlot );
    output._color         = instance.getHighlightColor();
    SW_APPEARANCE_WRITE( output, instance )
    // 이 패스의 방식이 아닌 인스턴스(강조 없음 포함)는 그리지 않는다 — 드로우는 그대로 나가지만 래스터가 없다.
    if ( instance.getHighlightStyle() != kHighlightPassStyle )
    {
        output._position = float4( 0.0f, 0.0f, -1.0f, 1.0f );
        return output;
    }
    float3 localPosition;
    float3 localNormal;
    MorphPool::loadVertex( instance._meshBatchIndex, vertexID, input._position, input._normal, localPosition, localNormal );
    const float4 worldPosition = Transform::computeWorldPosition( localPosition, instance._world );
    const float3 worldNormal   = Transform::computeWorldNormal( localNormal, instance._world );
    float4       clipPosition  = Transform::computeClipPosition( worldPosition, g_ViewProj );
    // 화면 공간 두께: 노멀을 클립 공간에 투영해 그 방향으로 민다. xy 에 w 를 곱해 더하므로 나눗셈 뒤 두께가 거리와 무관하다.
    // 가로세로 비를 맞춰야 위아래 · 좌우 두께가 같다(g_OutlineParams.y = 1/W, .z = 1/H — FrameRendererConstants).
    const float2 clipNormal = mul( float4( worldNormal, 0.0f ), g_ViewProj ).xy;
    const float  normalLen  = length( clipNormal );
    const float2 direction  = ( normalLen > 1e-6f ) ? clipNormal / normalLen : float2( 0.0f, 0.0f );
    const float  aspect     = g_OutlineParams.y / max( g_OutlineParams.z, 1e-6f );
    clipPosition.xy += direction * float2( aspect, 1.0f ) * ( SW_HIGHLIGHT_WIDTH * 2.0f * clipPosition.w );
    output._position = clipPosition;
    return output;
}

float4 PSMain( PSInput input ) : SV_TARGET
{
    SW_APPEARANCE_CLIP( input )
    return float4( input._color, 1.0f );
}
