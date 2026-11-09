/**
 * @file present_output.hlsli
 * @brief Present 패스의 출력 한 벌입니다 — 화면에 낼 색을 돌려주고, `gpu.inspect x y` 가 고른 픽셀이면 그 색을 셰이더 메시지로 적는다.
 * @details Present 를 맡는 셰이더(fullscreen_blit · post_chain — 파이프라인 XML 이 고른다)가 모두 이것으로 낸다(RenderPassType::Present 의 kGPUMessageTarget).
 *          적는 줄이 한 셰이더에만 있으면 다른 셰이더를 고른 파이프라인(포워드의 post_chain)에서 `gpu.inspect` 가 조용히 아무것도 내지 않았다.
 *          -gv_gpuMessages 변형이 아니면 메시지 매크로가 비어 색만 돌려준다.
 */
#ifndef SW_ENGINE_PRESENT_OUTPUT_HLSLI
#define SW_ENGINE_PRESENT_OUTPUT_HLSLI

#include "gpu_message.hlsli"

struct PresentOutput
{
    static float4 write( float4 svPosition, float3 color );
};

static float4 PresentOutput::write( float4 svPosition, float3 color )
{
    SW_GPU_MESSAGE_PIXEL( svPosition );
    SW_GPU_PRINT( SW_GPU_MESSAGE_ID_PRESENT_PIXEL, color.r, color.g, color.b, 0u );
    return float4( color, 1.0f );
}

#endif // SW_ENGINE_PRESENT_OUTPUT_HLSLI
