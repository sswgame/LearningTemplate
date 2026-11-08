/**
 * depth_of_field.hlsl — 피사계 심도 패스(RenderPassType::DepthOfField).
 *
 * 입력은 HDR 색(SourceColor)과 장면 깊이(SceneDepth), 출력은 같은 형식의 색이다. 렌즈 값은 PassCB 의 g_CameraLens 이고 엔진이 뷰마다 채운다
 * (FramePassExecutor::applyViewPassConstants — 카메라의 CameraDepthOfFieldValues 에서 얇은 렌즈 식으로 착란원 배율을 구한다).
 *
 * 방식: 픽셀마다 착란원 반지름(px) = y · |1 - 초점 거리 / 깊이| 를 z 로 자른다. 그 반지름의 원판에 탭 16 개(안쪽 고리 6 · 바깥 고리 10)를 놓고 색을 모은다.
 * 가까운 탭(앞의 물체)은 **자기** 반지름이 이 픽셀까지 닿을 때만 센다 — 초점이 맞은 앞 물체가 뒤의 흐린 배경으로 번져 후광이 생기지 않게 한다.
 * 흐린 앞 물체가 초점이 맞은 뒤 물체 위로 번지는 것(앞 번짐)은 중심 반지름이 0 이라 모으지 않는다 — 산란(scatter) 방식이 아니라서 생기는 한계다.
 *
 * 끈 뷰(조리개 0 · 직교 · 후처리 끔)는 이 패스를 기록하지 않고 출력 이름이 원본을 가리킨다(RenderPassTraitFlag::kBypassesToSource) — 여기서는 늘 흐린다.
 */
#if defined( SW_PASS_COMPUTE )
    #define SW_RW_SLOT0_GL_FORMAT "rgba16f" // 출력(DepthOfFieldColor) — GL 은 묶은 포맷과 선언이 같아야 한다
#endif
#include "binding.hlsli"
#include "fullscreen.hlsli"

static const uint  kTapCount      = 16u;
static const uint  kInnerTapCount = 6u;
static const float kInnerRadius   = 0.5f;

/** @brief 단위 원판 위 탭 하나의 자리 — 안쪽 고리(반지름 0.5) 6 개, 바깥 고리(반지름 1) 10 개. 바깥 고리는 반 칸 돌려 안쪽과 겹치지 않는다. */
float2 computeTapOffset( uint tapIndex )
{
    const bool  bInner    = tapIndex < kInnerTapCount;
    const float ringCount = bInner ? (float)kInnerTapCount : (float)( kTapCount - kInnerTapCount );
    const float ringIndex = bInner ? (float)tapIndex : ( (float)( tapIndex - kInnerTapCount ) + 0.5f );
    const float angle     = ringIndex * ( 2.0f * kPi / ringCount );
    const float radius    = bInner ? kInnerRadius : 1.0f;
    return float2( cos( angle ), sin( angle ) ) * radius;
}

/**
 * @brief 화면 UV 의 뷰 깊이(카메라 앞축 거리, m)입니다. 깊이 버퍼 값을 역뷰-투영으로 되돌린 동차 좌표의 w 의 역수가 클립 w(= 뷰 깊이)다.
 * @details 원근 투영에서 (ndc, depth, 1) · 역행렬 = 월드 / 클립 w 이므로 별도의 근 · 원 평면 상수가 필요 없다.
 */
float computeViewDepth( float2 uv )
{
    const float  deviceDepth = PassTexture::sampleDepthPoint( uv ).r;
    const float2 ndcXy       = uv * float2( 2.0f, -2.0f ) + float2( -1.0f, 1.0f );
    const float4 world       = mul( float4( ndcXy, deviceDepth, 1.0f ), g_InvViewProj );
    return 1.0f / max( abs( world.w ), 1e-6f );
}

/** @brief 그 깊이의 착란원 반지름(px)입니다. 상한(g_CameraLens.z)으로 자른다. */
float computeBlurRadius( float viewDepth )
{
    const float focusRatio = g_CameraLens.x / max( viewDepth, 1e-4f );
    return min( g_CameraLens.y * abs( 1.0f - focusRatio ), g_CameraLens.z );
}

#if defined( SW_PASS_COMPUTE )
    #include "post_compute.hlsli"

// 컴퓨트 구현 — 같은 탭 16 개 · 같은 무게다. 그룹(출력 8x8)이 사방 kDofApron 텍셀을 더한 타일에 (색 rgb, 뷰 깊이)를 한 번 읽어 두고, 탭이 타일 안이면 거기서 읽는다.
// 픽셀 구현은 탭마다 색(바이리니어)과 깊이(점)를 샘플러로 다시 읽어 이웃 픽셀끼리 같은 텍셀을 거듭 읽는다. 착란원 상한(g_CameraLens.z)이 apron 보다 크면 그 탭은 원본에서 직접 읽는다.
static const uint  kDofGroupSize = 8u;
static const int   kDofApron     = 16;
static const uint  kDofTileSize  = kDofGroupSize + 2u * (uint)kDofApron;
groupshared float4 s_arrDofTile[kDofTileSize * kDofTileSize];

/** @brief 텍셀 하나의 (색 rgb, 뷰 깊이)입니다. 깊이는 텍셀 중심에서 복원한다. */
float4 loadColorDepth( int2 texel )
{
    const float deviceDepth = PostCompute::loadDeviceDepth( texel );
    return float4( PostCompute::loadSource( texel ).rgb, PostCompute::computeViewDepth( float2( texel ) + 0.5f, deviceDepth ) );
}

/** @brief 타일 안이면 타일에서, 밖이면 원본에서 (색, 뷰 깊이)를 읽습니다. */
float4 fetchColorDepth( int2 texel, int2 tileOrigin )
{
    const int2 local   = texel - tileOrigin;
    const bool bInTile = all( local >= int2( 0, 0 ) ) && all( local < int2( (int)kDofTileSize, (int)kDofTileSize ) );
    return bInTile ? s_arrDofTile[(uint)local.y * kDofTileSize + (uint)local.x] : loadColorDepth( texel );
}

/** @brief 그 깊이의 착란원 반지름(px)입니다 — 픽셀 구현의 computeBlurRadius 와 같은 식을 루트 상수의 렌즈 값으로 쓴다. */
float computeBlurRadiusWith( float viewDepth, float3 lens )
{
    const float focusRatio = lens.x / max( viewDepth, 1e-4f );
    return min( lens.y * abs( 1.0f - focusRatio ), lens.z );
}

SW_NUMTHREADS( 8, 8, 1 )
void CSMain( uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID, uint groupIndex : SV_GroupIndex )
{
    const int2 tileOrigin = int2( groupID.xy * kDofGroupSize ) - int2( kDofApron, kDofApron );
    for ( uint slot = groupIndex; slot < kDofTileSize * kDofTileSize; slot += kDofGroupSize * kDofGroupSize )
    {
        s_arrDofTile[slot] = loadColorDepth( tileOrigin + int2( slot % kDofTileSize, slot / kDofTileSize ) );
    }
    GroupMemoryBarrierWithGroupSync();

    const uint2 outputTexel = groupID.xy * kDofGroupSize + groupThreadID.xy;
    if ( PostCompute::isInsideOutput( outputTexel ) == false )
        return;
    const float3 lens        = PostCompute::getParams();
    const float4 center      = fetchColorDepth( int2( outputTexel ), tileOrigin );
    const float  centerDepth = center.w;
    const float  centerBlur  = computeBlurRadiusWith( centerDepth, lens );
    const float2 centerPixel = float2( outputTexel ) + 0.5f;

    float3 colorSum  = center.rgb;
    float  weightSum = 1.0f;
    SW_UNROLL for ( uint tapIndex = 0u; tapIndex < kTapCount; ++tapIndex )
    {
        const float2 offsetPixels = computeTapOffset( tapIndex ) * centerBlur;
        const float2 tapPixel     = centerPixel + offsetPixels;
        // 색은 바이리니어(픽셀 구현의 선형 샘플러), 깊이는 점(sampleDepthPoint — 탭이 떨어진 텍셀)이다.
        const float2 bilinearBase = tapPixel - 0.5f;
        const int2   baseTexel    = int2( floor( bilinearBase ) );
        const float2 fraction     = bilinearBase - floor( bilinearBase );
        const float3 color00      = fetchColorDepth( baseTexel, tileOrigin ).rgb;
        const float3 color10      = fetchColorDepth( baseTexel + int2( 1, 0 ), tileOrigin ).rgb;
        const float3 color01      = fetchColorDepth( baseTexel + int2( 0, 1 ), tileOrigin ).rgb;
        const float3 color11      = fetchColorDepth( baseTexel + int2( 1, 1 ), tileOrigin ).rgb;
        const float3 tapColor     = lerp( lerp( color00, color10, fraction.x ), lerp( color01, color11, fraction.x ), fraction.y );
        const float  tapDepth     = fetchColorDepth( int2( floor( tapPixel ) ), tileOrigin ).w;
        const float  tapBlur      = computeBlurRadiusWith( tapDepth, lens );
        const bool   bNearer      = tapDepth < centerDepth;
        const float  weight       = bNearer ? saturate( tapBlur - length( offsetPixels ) + 1.0f ) : 1.0f;
        colorSum += tapColor * weight;
        weightSum += weight;
    }
    PostCompute::storeOutput( outputTexel, float4( colorSum / weightSum, 1.0f ) );
}
#else
float4 PSMain( PSInput input ) : SV_TARGET
{
    const float3 source      = PassTexture::sampleSourcePoint( input._uv ).rgb;
    const float  centerDepth = computeViewDepth( input._uv );
    const float  centerBlur  = computeBlurRadius( centerDepth );

    float3 colorSum  = source;
    float  weightSum = 1.0f;
    SW_UNROLL for ( uint tapIndex = 0u; tapIndex < kTapCount; ++tapIndex )
    {
        const float2 offsetPixels = computeTapOffset( tapIndex ) * centerBlur;
        const float2 tapUv        = input._uv + offsetPixels * g_SourceTexel.xy;
        const float  tapDepth     = computeViewDepth( tapUv );
        const float  tapBlur      = computeBlurRadius( tapDepth );
        // 앞의 탭은 자기 착란원이 이 픽셀까지 닿는 만큼만 센다(1 px 에 걸쳐 부드럽게). 뒤의 탭은 이 픽셀의 착란원 안이므로 다 센다.
        const bool  bNearer = tapDepth < centerDepth;
        const float weight  = bNearer ? saturate( tapBlur - length( offsetPixels ) + 1.0f ) : 1.0f;
        colorSum += PassTexture::sampleSource( tapUv ).rgb * weight;
        weightSum += weight;
    }
    return float4( colorSum / weightSum, 1.0f );
}
#endif
