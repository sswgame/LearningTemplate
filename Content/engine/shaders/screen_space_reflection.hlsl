/**
 * screen_space_reflection.hlsl — 화면 공간 반사 패스(RenderPassKind::ScreenSpaceReflection).
 *
 * 디퍼드에서 불투명 · 하늘을 그린 뒤 · 반투명 앞에 돈다. G버퍼 노멀로 시선을 반사한 광선을 화면 공간(UV, NDC 깊이 — 원근에서도 화면에서 직선이다)으로 옮겨
 * HZB 피라미드(hzb.hlsli — 이번 장면 깊이로 패스 바로 앞에 지은 것, 최소 깊이)를 계층 탐색(hi-z trace)한다: 광선이 칸의 가장 가까운 면보다 앞이면 칸 경계까지 건너뛰고
 * 한 밉 굵게, 그 면에 닿으면 한 밉 곱게 내려간다. 밉 0 에서 닿은 자리는 장면 깊이(전해상도)와 두께 g_SSRParams.z 로 확인하고, 두께보다 깊으면(물체 뒤로 지나감) 칸을 넘어 잇는다.
 * 맞은 자리의 색은 장면 색 사본(SceneColorCopy — 반사가 자기 결과를 읽지 않는다)에서 읽는다(언리얼 SSR · AMD FidelityFX SSSR 의 hierarchical raymarch 와 같은 자리).
 * 출력은 장면 색 위에 프리멀티플라이로 블렌드한다(반사색 × 무게, 무게) — 안개와 같은 모양이라 출력 첨부가 장면 색 그대로이고 반투명이 그 위에 그린다.
 * 무게 = 세기 × 프레넬(Schlick, 금속도로 F0) × 거칠기 감쇠(최대 거칠기에서 0) × 화면 가장자리 · 광선 길이 감쇠. 못 맞힌 픽셀 · 하늘 · G버퍼를 그리지 않은 픽셀은 (0, 0)이라 장면 그대로다.
 * 깊이 · 노멀은 Load 로 읽는다 — 반복 안의 기울기 샘플은 DX11(FXC)이 거절한다.
 *
 * 바인딩 계약: 패스 상수 g_SSRParams(세기, 최대 거리 m, 두께 m, 최대 거칠기) · g_SSRHZBParams(xy HZB 를 지은 깊이 사각형 px, z 밉 수), HZB 피라미드 t11(SW_SLOT_PASS_SRV),
 * 입력 역할 SceneColorCopy · SceneDepth · GBufferNormal · GBufferAlbedo(필수 — 알파가 셰이딩 모델) · GBufferMaterial(선택). HZB 가 없는 프레임은 C++ 가 패스를 그리지 않는다.
 */
#include "binding.hlsli"
#include "fullscreen.hlsli"
#include "hzb.hlsli"

SW_DECLARE_STRUCTURED_BUFFER( float2, g_SSRHZB, SW_SLOT_PASS_SRV );

/// @brief 계층 탐색의 반복 상한입니다 — 칸 건너뛰기 · 밉 오르내림 한 번이 한 반복이다.
static const int kSsrTraceIterationCount = 64;
/// @brief 이보다 작은 클립 w 는 카메라 뒤로 본다(광선 끝을 가까운 평면 앞에서 자른다).
static const float kSsrMinClipW = 1e-4f;
/// @brief 칸 경계를 넘는 비켜 가기(px) — 경계 위에 멈춰 같은 칸을 다시 읽지 않게.
static const float kSsrCellCrossPixels = 0.01f;

/** @brief 장면 깊이의 @p uv 텍셀 값입니다(점, Load). */
float loadSceneDepth( float2 uv, float2 depthSize )
{
    const int2 texel = clamp( int2( uv * depthSize ), int2( 0, 0 ), int2( depthSize ) - int2( 1, 1 ) );
    return Bindless::loadEngineTexel( SW_ENGINE_ROLE( SceneDepth ), texel ).r;
}

/** @brief 한 축에서 @p origin 이 걸음 @p step 으로 0 ~ @p limit 을 벗어나는 매개변수입니다(움직이지 않으면 아주 큰 값). */
float computeExitT( float origin, float step, float limit )
{
    return step > 1e-6f ? ( limit - origin ) / step : ( step < -1e-6f ? -origin / step : 1e30f );
}

/** @brief 클립 좌표를 화면 점(UV xy, NDC 깊이 z)으로 옮깁니다. */
float3 toScreenPoint( float4 clip )
{
    return float3( clip.xy / clip.w * float2( 0.5f, -0.5f ) + 0.5f, clip.z / clip.w );
}

/**
 * @brief 월드 점 @p position 이 장면 깊이보다 뒤(두께 안)인지 봅니다. 시선 거리는 그 화면 자리의 가까운 면 점에서 잰다(원근 · 직교 모두 같은 식).
 * @return 1 맞음 · 0 앞 · -1 화면 밖이거나 두께보다 깊다
 */
int classifyRayPoint( float3 position, float2 uv, float2 depthSize )
{
    const bool   bOnScreen   = all( uv >= 0.0f ) && all( uv <= 1.0f );
    const float  sceneDepth  = loadSceneDepth( uv, depthSize );
    const float3 nearPoint   = Transform::computeWorldPositionFromDepth( uv, 0.0f );
    const float  rayDistance = distance( position, nearPoint );
    const float  sceneDist   = distance( Transform::computeWorldPositionFromDepth( uv, sceneDepth ), nearPoint );
    const float  behind      = rayDistance - sceneDist;
    const int    result      = behind < 0.0f ? 0 : ( behind <= g_SSRParams.z && sceneDepth < 1.0f ? 1 : -1 );
    return bOnScreen ? result : -1;
}

/**
 * @brief 화면 광선(시작 @p origin, 방향 @p direction — xy 깊이 픽셀, z NDC 깊이, 매개변수 t 0 ~ @p maxT)을 HZB 로 계층 탐색합니다. 머리말 참고.
 * @param outT 맞은 자리의 매개변수(못 맞히면 쓰지 않는다)
 * @return 맞았으면 true(장면 깊이 · 두께로 확인한 자리)
 */
bool traceHierarchicalZ( float3 origin, float3 direction, float maxT, float2 depthSize, out float outT )
{
    const uint2  hzbSize     = uint2( g_SSRHZBParams.xy );
    const uint   mipCount    = uint( g_SSRHZBParams.z );
    const int    topMip      = int( mipCount ) - 1;
    const float2 crossStep   = float2( direction.x >= 0.0f ? 1.0f : 0.0f, direction.y >= 0.0f ? 1.0f : 0.0f );
    const float2 inverseStep = float2( abs( direction.x ) > 1e-6f ? 1.0f / direction.x : 1e30f, abs( direction.y ) > 1e-6f ? 1.0f / direction.y : 1e30f );
    const float  pixelLength = max( abs( direction.x ), abs( direction.y ) );
    const float  crossT      = kSsrCellCrossPixels / max( pixelLength, 1e-6f );

    // 시작 칸(밉 0 — 2 x 2 px)을 벗어난 자리에서 출발한다 — 자기 면을 맞히지 않게.
    const float2 startCell = floor( origin.xy * 0.5f );
    const float2 startExit = ( ( startCell + crossStep ) * 2.0f - origin.xy ) * inverseStep;
    float        t         = min( startExit.x, startExit.y ) + crossT;
    int          mip       = 0;
    bool         bHit      = false;
    bool         bTracing  = mipCount > 0u && pixelLength > 1e-4f;
    outT                   = 0.0f;
    SW_LOOP for ( int iteration = 0; iteration < kSsrTraceIterationCount; ++iteration )
    {
        bTracing = bTracing && t <= maxT;
        if ( bTracing )
        {
            const float3 rayPoint = origin + direction * t;
            const uint   shift    = uint( mip ) + 1u;
            const float  cellSize = float( 1u << shift );
            const uint2  size     = HZB::computeMipSize( hzbSize, uint( mip ) );
            const uint2  texel    = min( uint2( max( rayPoint.xy, 0.0f ) / cellSize ), size - uint2( 1u, 1u ) );
            const float  closest  = g_SSRHZB[HZB::computeMipOffset( hzbSize, uint( mip ) ) + texel.y * size.x + texel.x].x;
            const float2 cellExit = ( ( float2( texel ) + crossStep ) * cellSize - origin.xy ) * inverseStep;
            const float  exitT    = min( cellExit.x, cellExit.y ) + crossT;
            const bool   bInFront = rayPoint.z < closest;
            const float  surfaceT = direction.z > 1e-7f ? ( closest - origin.z ) / direction.z : 1e30f;
            const bool   bReaches = bInFront == false || surfaceT < exitT;
            if ( bReaches == false )
            {
                // 칸의 가장 가까운 면보다 앞으로 칸을 벗어난다 — 건너뛰고 한 밉 굵게.
                t   = exitT;
                mip = min( mip + 1, topMip );
            }
            else if ( mip > 0 )
            {
                // 이 칸 안에서 면에 닿는다 — 그 자리까지 가서 한 밉 곱게 본다.
                t   = bInFront ? max( t, surfaceT ) : t;
                mip = mip - 1;
            }
            else
            {
                // 밉 0 에서 닿았다 — 전해상도 깊이와 두께로 확인한다. 아니면(앞이거나 물체 뒤로 지나감) 칸을 넘어 잇는다.
                const float  hitT     = bInFront ? max( t, surfaceT ) : t;
                const float3 hitPoint = origin + direction * hitT;
                const float2 hitUv    = hitPoint.xy / depthSize;
                const float3 hitWorld = Transform::computeWorldPositionFromDepth( hitUv, hitPoint.z );
                bHit                  = classifyRayPoint( hitWorld, hitUv, depthSize ) == 1;
                outT                  = hitT;
                bTracing              = bHit == false;
                t                     = exitT;
            }
        }
    }
    return bHit;
}

float4 PSMain( PSInput input ) : SV_TARGET
{
    const float2 uv        = input._uv;
    const float2 depthSize = float2( PassTexture::getViewRectSize() ); // 텍셀 번호는 사각형 기준이다(풀이 사각형보다 클 수 있다) — HZB 도 같은 사각형에서 지었다
    const float  depth     = loadSceneDepth( uv, depthSize );
    float        roughness;
    float        metallic;
    float        specular;
    float        occlusion;
    PassTexture::loadGbufferMaterial( uv, roughness, metallic, specular, occlusion );
    // 그려졌다 = 알베도 알파의 셰이딩 모델 번호가 NONE 이 아니다(G버퍼 판 3 — 옛 그려짐 표시를 대신한다).
    const bool bDrawn = SurfaceOutput::decodeShadingModel( PassTexture::sampleAlbedo( uv ).a ) != SW_SHADING_MODEL_ID_NONE;

    float4 result = float4( 0.0f, 0.0f, 0.0f, 0.0f );
    SW_BRANCH if ( bDrawn && depth < 1.0f && roughness < g_SSRParams.w )
    {
        const float3 position  = Transform::computeWorldPositionFromDepth( uv, depth );
        const float3 viewDir   = normalize( Transform::computeWorldPositionFromDepth( uv, 1.0f ) - Transform::computeWorldPositionFromDepth( uv, 0.0f ) );
        const float3 normal    = PassTexture::sampleNormal( uv );
        const float3 reflected = reflect( viewDir, normal );

        // 광선 끝(최대 거리)을 가까운 평면 앞에서 자른다 — 클립 좌표는 광선 위에서 선형이라 w 가 문턱이 되는 비율로 줄인다.
        const float4 clipStart = mul( float4( position, 1.0f ), g_ViewProj );
        float4       clipEnd   = mul( float4( position + reflected * g_SSRParams.y, 1.0f ), g_ViewProj );
        if ( clipEnd.w < kSsrMinClipW )
            clipEnd = lerp( clipStart, clipEnd, ( clipStart.w - kSsrMinClipW ) / ( clipStart.w - clipEnd.w ) );
        const float3 screenStart = toScreenPoint( clipStart );
        const float3 screenEnd   = toScreenPoint( clipEnd );
        // 화면 광선 — xy 는 깊이 픽셀, z 는 NDC 깊이. 화면(UV 0 ~ 1)과 깊이 범위(0 ~ 1)를 벗어나는 매개변수에서 자른다.
        const float3 origin    = float3( screenStart.xy * depthSize, screenStart.z );
        const float3 direction = float3( ( screenEnd.xy - screenStart.xy ) * depthSize, screenEnd.z - screenStart.z );
        const float3 limit     = float3( depthSize, 1.0f );
        const float  maxT      = min( 1.0f, min( computeExitT( origin.x, direction.x, limit.x ), min( computeExitT( origin.y, direction.y, limit.y ), computeExitT( origin.z, direction.z, limit.z ) ) ) );

        float hitT = 0.0f;
        SW_BRANCH if ( traceHierarchicalZ( origin, direction, maxT, depthSize, hitT ) )
        {
            const float3 hitPoint    = origin + direction * hitT;
            const float2 hitUv       = hitPoint.xy / depthSize;
            const float3 hitWorld    = Transform::computeWorldPositionFromDepth( hitUv, hitPoint.z );
            const float3 color       = Bindless::sampleIndex( SW_ENGINE_ROLE( SceneColorCopy ), hitUv ).rgb;
            const float2 edge        = min( hitUv, 1.0f - hitUv );
            const float  edgeFade    = saturate( min( edge.x, edge.y ) * 10.0f );
            const float  travelFade  = 1.0f - saturate( distance( hitWorld, position ) / max( g_SSRParams.y, 1e-4f ) );
            const float  roughFade   = 1.0f - saturate( roughness / max( g_SSRParams.w, 1e-4f ) );
            const float  cosine      = saturate( dot( -viewDir, normal ) );
            const float  baseReflect = lerp( 0.04f, 1.0f, metallic );
            const float  fresnel     = baseReflect + ( 1.0f - baseReflect ) * pow( 1.0f - cosine, 5.0f );
            const float  weight      = saturate( g_SSRParams.x * fresnel * roughFade * edgeFade * travelFade );
            result                   = float4( color * weight, weight );
        }
    }
    return result;
}
