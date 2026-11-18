/**
 * surface_deform_layer.hlsli — 씬의 표면 변형 층(C++ SurfaceDeformSystem)을 읽는 함수 묶음. 지면(surface_deform.hlsl)과 지형(terrain.hlsl)이 같은 식을 쓴다.
 *
 * 층은 머티리얼 칸이 아니라 패스가 거는 값이다 — 엔진 텍스처 칸 SurfaceDeform(g_SurfaceDeformIndex)과 패스 상수 g_SurfaceDeformParams(창)를
 * 씬 지오메트리 패스마다 한 번 건다(C++ FramePassExecutor::registerInstanceBuffer · FrameRenderer::updatePassConstants). 그래서 정점 셰이더와 그림자 패스가
 * 같은 오프셋을 읽는다(언리얼 런타임 가상 텍스처 변위 · 유니티 Shader.SetGlobalTexture 와 같은 자리).
 *
 * 텍스처 인코딩(C++ SurfaceDeformGrid): R = 1 - 꺼짐 / 범위, G = 1 - 솟음 / 범위 — 높이 = (R - G) × 범위, B = 1 - 젖음(젖은 발자국). 흰색이 평평 · 마름이다.
 * 비가 오면 꺼진 자리(발자국 · 바퀴 자국)에 물이 고인다 — `computePuddleFill` 이 깊이 × 젖음으로 웅덩이 양을 낸다(높이장을 고치지 않고 그림으로만).
 * 창 = (최소 x, 최소 z, 1 / 너비, 깊이 범위)이고 창 밖은 0, 가장자리는 서서히 0 으로 줄인다(창이 옮겨 갈 때 자국이 끊겨 보이지 않게).
 * 층이 없으면(찍은 적 없음 · 텍스처가 아직 올라가지 않음) 인덱스가 무효이거나 창 z 가 0 이라 높이 0 이다.
 * 밉 0 으로 읽는다 — 정점 단계에는 화면 미분이 없다.
 */

#ifndef SW_ENGINE_SURFACEDEFORMLAYER_HLSLI
#define SW_ENGINE_SURFACEDEFORMLAYER_HLSLI

// 창 가장자리에서 변형을 0 으로 줄이는 폭의 역수(창 너비 비율) — 1/32 안쪽부터 줄인다(20 m 창이면 0.6 m).
static const float kSurfaceDeformEdgeFadeScale = 32.0f;
// 이 깊이(m)만큼 꺼진 자리는 다 젖으면 물이 가득 고인다(발자국 2 cm — 더 얕으면 그만큼 덜 고인다).
static const float kSurfaceDeformPuddleDepth = 0.02f;

/** @brief 표면 변형 층 읽기입니다(파일 머리말 참고). */
struct SurfaceDeformLayer
{
    static bool isBound();
    static float sampleHeight( float2 worldXZ );
    static float sampleWetness( float2 worldXZ );
    static float computePuddleFill( float2 worldXZ, float wetness );
    static float3 tiltNormal( float3 normal, float2 worldXZ );
};

/** @brief 이번 패스에 층이 걸렸는가입니다 — 텍스처와 창이 같이 켜지고 같이 꺼진다. */
static bool SurfaceDeformLayer::isBound()
{
    return g_SurfaceDeformIndex != kInvalidIndex && g_SurfaceDeformParams.z > 0.0f;
}

/** @brief 월드 (x, z) 의 변형 높이(m, 음수 = 꺼짐)입니다. 층이 없거나 창 밖이면 0 이다. 이른 반환 없이 출구가 하나다. */
static float SurfaceDeformLayer::sampleHeight( float2 worldXZ )
{
    float height = 0.0f;
    if ( SurfaceDeformLayer::isBound() )
    {
        const float4 params = g_SurfaceDeformParams;
        const float2 uv     = ( worldXZ - params.xy ) * params.z;
        const float2 edge   = min( uv, 1.0f - uv );
        const float  fade   = saturate( min( edge.x, edge.y ) * kSurfaceDeformEdgeFadeScale );
        const float4 texel  = Bindless::sampleIndexLevelZero( SW_ENGINE_ROLE( SurfaceDeform ), saturate( uv ) );
        height              = ( texel.r - texel.g ) * params.w * fade;
    }
    return height;
}

/** @brief 월드 (x, z) 의 젖은 자국 젖음(0..1)입니다. 층이 없거나 창 밖이면 0 이다. */
static float SurfaceDeformLayer::sampleWetness( float2 worldXZ )
{
    float wetness = 0.0f;
    if ( SurfaceDeformLayer::isBound() )
    {
        const float4 params = g_SurfaceDeformParams;
        const float2 uv     = ( worldXZ - params.xy ) * params.z;
        const float2 edge   = min( uv, 1.0f - uv );
        const float  fade   = saturate( min( edge.x, edge.y ) * kSurfaceDeformEdgeFadeScale );
        const float4 texel  = Bindless::sampleIndexLevelZero( SW_ENGINE_ROLE( SurfaceDeform ), saturate( uv ) );
        wetness             = saturate( 1.0f - texel.b ) * fade;
    }
    return wetness;
}

/** @brief 비에 젖은 땅(@p wetness 0..1)에서 꺼진 자리에 고인 물의 양(0..1) — 꺼진 깊이 / kSurfaceDeformPuddleDepth × 젖음. 솟은 자리 · 평평한 자리는 0 이다. */
static float SurfaceDeformLayer::computePuddleFill( float2 worldXZ, float wetness )
{
    return saturate( -SurfaceDeformLayer::sampleHeight( worldXZ ) / kSurfaceDeformPuddleDepth ) * saturate( wetness );
}

/**
 * @brief 메시 법선 @p normal 을 변형 높이의 기울기만큼 기울입니다 — 높이장 y = h(x, z) 의 법선은 (-dh/dx, 1, -dh/dz) 쪽이고, 중앙 차분 간격은 텍셀 하나다.
 * @details 층이 없으면 @p normal 그대로다. 텍셀 크기는 창 너비 / 텍스처 한 변이다(C++ SurfaceDeformGrid::getTexelSize 와 같다).
 */
static float3 SurfaceDeformLayer::tiltNormal( float3 normal, float2 worldXZ )
{
    float3 result = normal;
    if ( SurfaceDeformLayer::isBound() )
    {
        const uint2  size     = Bindless::getEngineTextureSize( SW_ENGINE_ROLE( SurfaceDeform ) );
        const float  step     = max( 1.0f / ( g_SurfaceDeformParams.z * float( max( size.x, 1u ) ) ), 1e-3f );
        const float  left     = SurfaceDeformLayer::sampleHeight( worldXZ - float2( step, 0.0f ) );
        const float  right    = SurfaceDeformLayer::sampleHeight( worldXZ + float2( step, 0.0f ) );
        const float  back     = SurfaceDeformLayer::sampleHeight( worldXZ - float2( 0.0f, step ) );
        const float  front    = SurfaceDeformLayer::sampleHeight( worldXZ + float2( 0.0f, step ) );
        const float2 gradient = float2( right - left, front - back ) / ( 2.0f * step );
        result                = normalize( normal + float3( -gradient.x, 0.0f, -gradient.y ) );
    }
    return result;
}

#endif // SW_ENGINE_SURFACEDEFORMLAYER_HLSLI
