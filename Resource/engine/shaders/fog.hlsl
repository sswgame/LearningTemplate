/**
 * fog.hlsl — 높이 · 거리 지수 안개 패스(RenderPassType::Fog).
 *
 * 입력은 장면 깊이(SceneDepth)뿐이고, 출력은 앞 패스가 칠한 색 첨부 **위에 블렌드**한다(PSO 블렌드: 색 One, 알파 InvSrcAlpha — 프리멀티플라이).
 * 그래서 이 셰이더가 내는 값은 (안개색 × 불투명도, 불투명도)이고, 결과 = 안개색 × 불투명도 + 장면 × (1 - 불투명도)다. 장면 색을 읽지 않으므로 복사 첨부가 없다.
 * 불투명 · 하늘 · 반투명을 그린 뒤, 블룸 · 톤맵 앞에 둔다. 하늘 픽셀(깊이 1)에는 안개를 입히지 않는다(대기 산란이 자기 안개를 낸다 — RealtimeGlobalIllumination R3).
 *
 * 평범한 안개: FogMath(fogcommon.hlsli)가 시선 위 광학 두께를 구해 불투명도 = min( 최대 불투명도, 1 - exp( -광학 두께 ) )를 낸다. 색은 안개 색에
 * 방향광 쪽 시선의 산란(g_FogInscatter — 색 × 세기, 지수)을 더한 것이다. 눈은 g_InvViewProj 로 구한다 — (0, 0, 1, 0) 의 역투영이 눈의 동차 좌표(원근)이고,
 * w 가 0 이면 직교라 화면 점의 가까운 면 점이 시선 원점이다.
 *
 * 볼륨 안개(g_FogVolumeParams.x = 1, 주 시점에서 컴퓨트 volumetricfog.hlsl 이 돈 프레임): froxel 버퍼(t11, float4 = 산란 rgb, 투과율 a)에서 이 픽셀의 시선 거리까지의
 * 누적 값을 읽는다(화면 칸 쌍선형 × 거리 조각 선형). 볼륨 거리 밖은 평범한 안개 식으로 이어 붙인다. 볼륨이 안개 색을 앰비언트로 이미 포함하므로 안쪽 구간에서 평범한 안개를 더하지 않는다.
 * 표면 값 보기 모드(Normals · Depth · Overdraw)와 와이어프레임은 C++ 가 이 패스를 그리지 않는다(값 위를 덮는다). 후처리를 끈 뷰(CCTV)도 안개는 그린다 — 장면의 일이다.
 *
 * 바인딩 계약: 패스 상수 g_FogColorDensity · g_FogHeightParams · g_FogInscatter · g_FogVolumeParams, 장면 깊이(역할 SceneDepth), 볼륨 버퍼 t11(SW_SLOT_PASS_SRV, 볼륨이 켜진 때만 걸린다).
 */
#include "binding.hlsli"
#include "fogcommon.hlsli"

struct PSInput
{
    float4 _position : SV_POSITION;
    float2 _uv : TEXCOORD0;
};

SW_DECLARE_STRUCTURED_BUFFER( float4, g_FogVolume, SW_SLOT_PASS_SRV );

PSInput VSMain( VertexInput input, uint vertexID : SV_VertexID )
{
    PSInput output;
    input._position     = input._position;
    float2 clipPosition = float2( ( vertexID == 1 ) ? 3.0f : -1.0f, ( vertexID == 2 ) ? 3.0f : -1.0f );
    output._position    = float4( clipPosition, 0.0f, 1.0f );
    output._uv          = clipPosition * float2( 0.5f, -0.5f ) + 0.5f;
    return output;
}

/** @brief 볼륨 한 칸의 값입니다. @p slice 가 -1 이면 시작(산란 0, 투과율 1)이다. 칸 번호는 volumetricfog.hlsl 의 쓰기와 같다((조각 × Y + y) × X + x). */
float4 loadFogVolume( uint tileX, uint tileY, int slice )
{
    const uint   safeSlice = (uint)max( slice, 0 );
    const float4 value     = g_FogVolume[( safeSlice * SW_FOG_VOLUME_DIM_Y + tileY ) * SW_FOG_VOLUME_DIM_X + tileX];
    return slice < 0 ? float4( 0.0f, 0.0f, 0.0f, 1.0f ) : value;
}

/** @brief 화면 칸 쌍선형으로 읽은 조각 @p slice 의 누적 값입니다(조각 끝까지의 산란과 투과율). */
float4 sampleFogVolumeColumn( float2 uv, int slice )
{
    const float2 grid    = uv * float2( (float)SW_FOG_VOLUME_DIM_X, (float)SW_FOG_VOLUME_DIM_Y ) - 0.5f;
    const float2 base    = floor( grid );
    const float2 weight  = grid - base;
    const int2   tileMin = clamp( int2( base ), int2( 0, 0 ), int2( SW_FOG_VOLUME_DIM_X - 1, SW_FOG_VOLUME_DIM_Y - 1 ) );
    const int2   tileMax = clamp( int2( base ) + 1, int2( 0, 0 ), int2( SW_FOG_VOLUME_DIM_X - 1, SW_FOG_VOLUME_DIM_Y - 1 ) );
    const float4 lower   = lerp( loadFogVolume( tileMin.x, tileMin.y, slice ), loadFogVolume( tileMax.x, tileMin.y, slice ), weight.x );
    const float4 upper   = lerp( loadFogVolume( tileMin.x, tileMax.y, slice ), loadFogVolume( tileMax.x, tileMax.y, slice ), weight.x );
    return lerp( lower, upper, weight.y );
}

/**
 * @brief 이 화면 위치 · 시선 거리 @p distance 까지 누적된 산란(rgb)과 투과율(a)입니다. 조각 경계 b 의 값은 조각 b - 1 의 끝까지의 누적이라 경계 사이를 선형으로 읽는다.
 */
float4 sampleFogVolume( float2 uv, float distance, float farDistance )
{
    const float boundary = FogMath::computeSlicePosition( distance, farDistance );
    const int   lower    = (int)floor( boundary );
    const float ratio    = boundary - (float)lower;
    const int   upper    = min( lower + 1, (int)SW_FOG_VOLUME_DIM_Z );
    return lerp( sampleFogVolumeColumn( uv, lower - 1 ), sampleFogVolumeColumn( uv, upper - 1 ), ratio );
}

float4 PSMain( PSInput input ) : SV_TARGET
{
    const float  deviceDepth = PassTexture::sampleDepthPoint( input._uv ).r;
    const float2 ndcXy       = input._uv * float2( 2.0f, -2.0f ) + float2( -1.0f, 1.0f );

    // 시선 — 원근은 눈에서, 직교는 화면 점의 가까운 면 점에서 쏜다.
    const float4 nearHomogeneous    = mul( float4( ndcXy, 0.0f, 1.0f ), g_InvViewProj );
    const float4 farHomogeneous     = mul( float4( ndcXy, 1.0f, 1.0f ), g_InvViewProj );
    const float4 surfaceHomogeneous = mul( float4( ndcXy, deviceDepth, 1.0f ), g_InvViewProj );
    const float4 eyeHomogeneous     = mul( float4( 0.0f, 0.0f, 1.0f, 0.0f ), g_InvViewProj );
    const bool   bPerspective       = abs( eyeHomogeneous.w ) > 1.0e-6f;
    const float3 nearPoint          = nearHomogeneous.xyz / nearHomogeneous.w;
    const float3 farPoint           = farHomogeneous.xyz / farHomogeneous.w;
    const float3 surfacePoint       = surfaceHomogeneous.xyz / surfaceHomogeneous.w;
    const float3 origin             = bPerspective ? eyeHomogeneous.xyz / eyeHomogeneous.w : nearPoint;
    const float3 direction          = normalize( farPoint - origin );
    const float  distanceToSurface  = length( surfacePoint - origin );

    const float  sunWeight     = pow( saturate( dot( direction, -g_KeyLightDirIntensity.xyz ) ), g_FogInscatter.w );
    const float3 fogColor      = g_FogColorDensity.rgb + g_FogInscatter.rgb * sunWeight;
    const float  startDistance = g_FogHeightParams.z;
    const bool   bVolume       = g_FogVolumeParams.x > 0.5f;
    const float  farDistance   = g_FogVolumeParams.y;

    // 볼륨 안(거리 ≤ 볼륨 거리)은 froxel 값, 밖은 평범한 안개 식으로 이어 붙인다. 볼륨이 꺼졌으면 볼륨 거리를 0 으로 보아 전부 평범한 식이다.
    // 볼륨 버퍼는 켠 프레임에만 걸린다 — 꺼졌을 때 읽지 않도록 값 선택이 아니라 분기로 읽는다.
    float4 volumeValue = float4( 0.0f, 0.0f, 0.0f, 1.0f );
    SW_BRANCH if ( bVolume )
    {
        volumeValue = sampleFogVolume( input._uv, min( distanceToSurface, farDistance ), farDistance );
    }
    const float  restStart      = bVolume ? max( startDistance, farDistance ) : startDistance;
    const float  restDepth      = FogMath::computeOpticalDepth( g_FogColorDensity, g_FogHeightParams, origin, direction, restStart, distanceToSurface );
    const float  restTransmit   = exp( -restDepth );
    const float  transmittance  = volumeValue.a * restTransmit;
    const float3 inscatter      = volumeValue.rgb + volumeValue.a * fogColor * ( 1.0f - restTransmit );
    const float  opacity        = 1.0f - transmittance;
    const float  clampedOpacity = min( g_FogHeightParams.w, opacity );
    const float  inscatterScale = opacity > 1.0e-6f ? clampedOpacity / opacity : 1.0f;

    // 하늘(깊이 1)은 건드리지 않는다 — 블렌드가 (0, 0, 0, 0) 이면 장면 색 그대로다.
    const bool bSky = deviceDepth >= 1.0f;
    return bSky ? float4( 0.0f, 0.0f, 0.0f, 0.0f ) : float4( inscatter * inscatterScale, clampedOpacity );
}
