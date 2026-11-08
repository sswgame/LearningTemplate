#if defined( SW_PASS_COMPUTE )
    #define SW_RW_SLOT0_GL_FORMAT "rgba16f" // 출력(BloomBright) — GL 은 묶은 포맷과 선언이 같아야 한다
#endif
#include "binding.hlsli"
#include "fullscreen.hlsli"

#include "post_bloom.hlsli"

#if defined( SW_PASS_COMPUTE )
    #include "post_compute.hlsli"

    #if !defined( SW_PASS_BLOOM_DOWNSAMPLE )
        #error post_bloom.hlsl 의 컴퓨트 구현은 BloomDownsample 패스(SW_PASS_BLOOM_DOWNSAMPLE)만 있다
    #endif

// 그룹 하나 = 출력 8x8 = 원본 16x16 + 사방 1 텍셀(18x18). 출력 텍셀 하나가 원본 4x4 블록(2x-1 .. 2x+2)의 평균이다 — 픽셀 구현의 바이리니어 네 탭(원본 텍셀 모서리)과 같은 무게다.
// 원본을 한 번만 읽어 그룹 공유 타일에 둔다: 픽셀 구현은 출력 텍셀마다 원본 16 텍셀을 샘플러로 읽고(이웃 출력과 겹친다), 여기는 타일 하나를 64 스레드가 나눠 한 번 읽는다.
static const uint  kBloomGroupSize = 8u;
static const uint  kBloomTileSize  = kBloomGroupSize * 2u + 2u;
groupshared float3 s_arrBloomTile[kBloomTileSize * kBloomTileSize];

SW_NUMTHREADS( 8, 8, 1 )
void CSMain( uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID, uint groupIndex : SV_GroupIndex )
{
    const int2 tileOrigin = int2( groupID.xy * ( kBloomGroupSize * 2u ) ) - int2( 1, 1 );
    for ( uint slot = groupIndex; slot < kBloomTileSize * kBloomTileSize; slot += kBloomGroupSize * kBloomGroupSize )
    {
        const int2 local     = int2( slot % kBloomTileSize, slot / kBloomTileSize );
        s_arrBloomTile[slot] = PostCompute::loadSource( tileOrigin + local ).rgb;
    }
    GroupMemoryBarrierWithGroupSync();

    const uint2 outputTexel = groupID.xy * kBloomGroupSize + groupThreadID.xy;
    if ( PostCompute::isInsideOutput( outputTexel ) == false )
        return;
    const uint2 blockOrigin = groupThreadID.xy * 2u;
    float3      sum         = float3( 0.0f, 0.0f, 0.0f );
    SW_UNROLL for ( uint row = 0u; row < 4u; ++row )
    {
        SW_UNROLL for ( uint column = 0u; column < 4u; ++column )
        {
            sum += s_arrBloomTile[( blockOrigin.y + row ) * kBloomTileSize + blockOrigin.x + column];
        }
    }
    const float3 blur = sum * ( 1.0f / 16.0f );
    PostCompute::storeOutput( outputTexel, float4( Bloom::computeBrightFromBlurWith( blur, PostCompute::getParams() ), 1.0f ) );
}
#elif defined( SW_PASS_BLOOM_DOWNSAMPLE )
float4 PSMain( PSInput input ) : SV_TARGET
{
    // BloomDownsample 패스 — 반해상도 출력에 밝은 부분만 낸다(합성은 Bloom 패스가 한다). 원본은 원래 해상도라 g_SourceTexel 이 원본 텍셀이다.
    // 블룸이 꺼진 뷰에서는 읽는 패스가 없어 이 패스를 기록하지 않는다(FrameViewResources::applyViewPassSelection).
    return float4( Bloom::computeBrightDownsampled( input._uv, g_SourceTexel.xy ), 1.0f );
}
#else
float4 PSMain( PSInput input ) : SV_TARGET
{
    // 본문은 post_bloom.hlsli 하나다 — 합친 체인(post_chain.hlsl)과 **같은 코드**를 쓴다.
    // 블룸은 원본을 텍셀 반 칸 비켜 읽는다 — 원본(반해상도일 수 있다)의 텍셀이다(g_SourceTexel).
    float2 texel = g_SourceTexel.xy;
    float3 color = PassTexture::sampleSourcePoint( input._uv ).rgb;
    // 후처리를 끈 뷰(CCTV · 표면 값 보기 모드) · 품질 미달이면 이 패스를 기록하지 않고 출력 이름이 원본을 가리킨다(RenderPassTraitFlag::kBypassesToSource).
    // 파이프라인이 반해상도 밝은 부분(BloomBright)을 입력으로 선언했으면 그것을 업샘플해 더하고, 아니면 여기서 구한다. 인덱스는 패스 상수라 분기가 고르다.
    const bool   bHasDownsampledBright = g_BloomBrightIndex != kInvalidIndex;
    const float3 bloomed               = bHasDownsampledBright ? Bloom::applyBright( input._uv, color ) : Bloom::apply( input._uv, texel, color );
    return float4( bloomed, 1.0f );
}
#endif
